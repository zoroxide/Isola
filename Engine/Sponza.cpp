#include "Sponza.h"
#include "libs/stb_image.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/GltfMaterial.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {
enum TexKind { ALBEDO = 0, NORMAL = 1, METAL_ROUGH = 2 };

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec4 tangent;   // xyz, w = bitangent sign
    glm::vec2 uv;
};

// Triangle flags for the BVH
constexpr uint8_t kAlphaTested = 1;   // leaves, chains: no collision

// ---------------------------------------------------------------------------
// Block compression (runs on the loader threads; the driver's own compressor is single-threaded
// and was most of the load time). BC1/BC3 for colour, BC5 (two BC4 channels) for normal and
// roughness/metallic maps.
// ---------------------------------------------------------------------------
void encodeBC4(const unsigned char v[16], unsigned char* out) {
    int mn = 255, mx = 0;
    for (int i = 0; i < 16; ++i) { mn = std::min(mn, (int)v[i]); mx = std::max(mx, (int)v[i]); }
    out[0] = (unsigned char)mx;   // a0 > a1: 8-value mode
    out[1] = (unsigned char)mn;
    uint64_t bits = 0;
    if (mx > mn) {
        for (int i = 0; i < 16; ++i) {
            // Position between a1 (0) and a0 (7); codes: 0 -> a0, 1 -> a1, k = 2..7 -> ((8-k) a0 + (k-1) a1) / 7
            int s = ((v[i] - mn) * 14 + (mx - mn)) / (2 * (mx - mn));
            uint64_t idx = s == 7 ? 0 : s == 0 ? 1 : (uint64_t)(8 - s);
            bits |= idx << (3 * i);
        }
    }
    for (int k = 0; k < 6; ++k) out[2 + k] = (unsigned char)(bits >> (8 * k));
}

void encodeBC1(const unsigned char rgba[16][4], unsigned char* out) {
    // Endpoints along the principal axis of the block's colours (power iteration), inset slightly
    glm::vec3 px[16], mean(0.0f);
    for (int i = 0; i < 16; ++i) { px[i] = glm::vec3(rgba[i][0], rgba[i][1], rgba[i][2]); mean += px[i]; }
    mean /= 16.0f;
    float cov[6] = {};
    for (int i = 0; i < 16; ++i) {
        glm::vec3 d = px[i] - mean;
        cov[0] += d.x * d.x; cov[1] += d.x * d.y; cov[2] += d.x * d.z;
        cov[3] += d.y * d.y; cov[4] += d.y * d.z; cov[5] += d.z * d.z;
    }
    glm::vec3 axis(1.0f, 1.0f, 1.0f);
    for (int it = 0; it < 4; ++it) {
        glm::vec3 a(cov[0] * axis.x + cov[1] * axis.y + cov[2] * axis.z, cov[1] * axis.x + cov[3] * axis.y + cov[4] * axis.z,
                    cov[2] * axis.x + cov[4] * axis.y + cov[5] * axis.z);
        float l = glm::length(a);
        if (l < 1e-6f) break;
        axis = a / l;
    }
    float tmin = 1e30f, tmax = -1e30f;
    for (int i = 0; i < 16; ++i) { float t = glm::dot(px[i] - mean, axis); tmin = std::min(tmin, t); tmax = std::max(tmax, t); }
    float inset = (tmax - tmin) / 16.0f;
    glm::vec3 e0 = glm::clamp(mean + axis * (tmax - inset), 0.0f, 255.0f), e1 = glm::clamp(mean + axis * (tmin + inset), 0.0f, 255.0f);
    auto pack = [](const glm::vec3& c) {
        return (uint16_t)((((int)c.x * 31 + 127) / 255) << 11 | (((int)c.y * 63 + 127) / 255) << 5 | (((int)c.z * 31 + 127) / 255));
    };
    uint16_t c0 = pack(e0), c1 = pack(e1);
    if (c0 < c1) std::swap(c0, c1);   // c0 > c1 selects the 4-colour palette
    auto unpack = [](uint16_t c) {
        int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
        return glm::vec3((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
    };
    glm::vec3 pal[4] = {unpack(c0), unpack(c1)};
    pal[2] = (2.0f * pal[0] + pal[1]) / 3.0f;
    pal[3] = (pal[0] + 2.0f * pal[1]) / 3.0f;
    uint32_t bits = 0;
    if (c0 != c1) {
        for (int i = 0; i < 16; ++i) {
            int best = 0;
            float bestD = 1e30f;
            for (int k = 0; k < 4; ++k) {
                glm::vec3 d = px[i] - pal[k];
                float dd = glm::dot(d, d);
                if (dd < bestD) { bestD = dd; best = k; }
            }
            bits |= (uint32_t)best << (2 * i);
        }
    }
    out[0] = (unsigned char)(c0 & 255); out[1] = (unsigned char)(c0 >> 8);
    out[2] = (unsigned char)(c1 & 255); out[3] = (unsigned char)(c1 >> 8);
    for (int k = 0; k < 4; ++k) out[4 + k] = (unsigned char)(bits >> (8 * k));
}

// Compress one mip level (C = 4: BC1, or BC3 when withAlpha; C = 2: BC5)
std::vector<unsigned char> compressLevel(const std::vector<unsigned char>& px, int w, int h, int C, bool withAlpha) {
    int bw = (w + 3) / 4, bh = (h + 3) / 4;
    int blockBytes = (C == 4 && !withAlpha) ? 8 : 16;
    std::vector<unsigned char> out((size_t)bw * bh * blockBytes);
    unsigned char block[16][4];
    unsigned char chan[16];
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            for (int i = 0; i < 16; ++i) {
                int x = std::min(bx * 4 + (i & 3), w - 1), y = std::min(by * 4 + (i >> 2), h - 1);
                for (int k = 0; k < C; ++k) block[i][k] = px[((size_t)y * w + x) * C + k];
            }
            unsigned char* dst = out.data() + ((size_t)by * bw + bx) * blockBytes;
            if (C == 4) {
                if (withAlpha) {
                    for (int i = 0; i < 16; ++i) chan[i] = block[i][3];
                    encodeBC4(chan, dst);
                    dst += 8;
                }
                encodeBC1(block, dst);
            } else {
                for (int k = 0; k < 2; ++k) {
                    for (int i = 0; i < 16; ++i) chan[i] = block[i][k];
                    encodeBC4(chan, dst + 8 * k);
                }
            }
        }
    return out;
}

std::vector<unsigned char> halve(const std::vector<unsigned char>& px, int w, int h, int C) {
    int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
    std::vector<unsigned char> next((size_t)nw * nh * C);
    for (int y = 0; y < nh; ++y) for (int x = 0; x < nw; ++x) for (int k = 0; k < C; ++k) {
        int x0 = std::min(2 * x, w - 1), y0 = std::min(2 * y, h - 1), x1 = std::min(2 * x + 1, w - 1), y1 = std::min(2 * y + 1, h - 1);
        int s = px[((size_t)y0 * w + x0) * C + k] + px[((size_t)y0 * w + x1) * C + k] +
                px[((size_t)y1 * w + x0) * C + k] + px[((size_t)y1 * w + x1) * C + k];
        next[((size_t)y * nw + x) * C + k] = (unsigned char)((s + 2) / 4);
    }
    return next;
}

// Decoded image ready for upload: the full mip chain, block-compressed when `format` is set
struct Image {
    int w = 0, h = 0, c = 0;   // base level size, channels before compression
    bool alpha = false;
    glm::vec3 average{0.5f};
    GLenum format = 0;          // compressed format, or 0 for plain RGBA8 / RG8
    std::vector<std::vector<unsigned char>> levels;
};

Image decode(const std::string& path, int kind, int maxSize, bool s3tc) {
    Image im;
    int w, h, c;
    unsigned char* src = stbi_load(path.c_str(), &w, &h, &c, 4);
    if (!src) return im;
    // Keep the channels each use needs: RGBA colour, RG normal, (roughness, metallic) from glTF's G, B
    int C = kind == ALBEDO ? 4 : 2;
    std::vector<unsigned char> px((size_t)w * h * C);
    double sum[3] = {0, 0, 0};
    for (size_t p = 0; p < (size_t)w * h; ++p) {
        const unsigned char* s = src + p * 4;
        if (kind == ALBEDO) {
            for (int k = 0; k < 4; ++k) px[p * 4 + k] = s[k];
            if (s[3] < 250) im.alpha = true;
            for (int k = 0; k < 3; ++k) sum[k] += s[k];
        } else if (kind == NORMAL) {
            px[p * 2] = s[0]; px[p * 2 + 1] = s[1];
        } else {
            px[p * 2] = s[1]; px[p * 2 + 1] = s[2];
        }
    }
    stbi_image_free(src);
    im.average = glm::vec3(sum[0], sum[1], sum[2]) / float(255.0 * std::max<size_t>(1, (size_t)w * h));
    while (w > maxSize || h > maxSize) {
        px = halve(px, w, h, C);
        w = std::max(1, w / 2); h = std::max(1, h / 2);
    }
    im.w = w; im.h = h; im.c = C;
    if (kind == ALBEDO) im.format = s3tc ? (im.alpha ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : GL_COMPRESSED_RGB_S3TC_DXT1_EXT) : 0;
    else im.format = GL_COMPRESSED_RG_RGTC2;   // core since OpenGL 3.0
    for (;;) {
        im.levels.push_back(im.format ? compressLevel(px, w, h, C, im.alpha) : px);
        if (w == 1 && h == 1) break;
        px = halve(px, w, h, C);
        w = std::max(1, w / 2); h = std::max(1, h / 2);
    }
    return im;
}

GLuint upload(const Image& im, int kind, float anisotropy) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (size_t l = 0; l < im.levels.size(); ++l) {
        int w = std::max(1, im.w >> l), h = std::max(1, im.h >> l);
        if (im.format)
            glCompressedTexImage2D(GL_TEXTURE_2D, (GLint)l, im.format, w, h, 0, (GLsizei)im.levels[l].size(), im.levels[l].data());
        else
            glTexImage2D(GL_TEXTURE_2D, (GLint)l, im.c == 4 ? GL_RGBA8 : GL_RG8, w, h, 0, im.c == 4 ? GL_RGBA : GL_RG,
                         GL_UNSIGNED_BYTE, im.levels[l].data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)im.levels.size() - 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    if (GLEW_EXT_texture_filter_anisotropic && kind != METAL_ROUGH) {
        GLfloat maxAniso = 1.0f;
        glGetFloatv(0x84FF /*GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT*/, &maxAniso);
        glTexParameterf(GL_TEXTURE_2D, 0x84FE, std::max(1.0f, std::min(anisotropy, maxAniso)));
    }
    return tex;
}

GLuint solidTexture(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    unsigned char px[4] = {r, g, b, a};
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}

uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16;
    return x;
}

// Runs fn(i) for i in [0, count) on `threads` threads
template <class Fn> void parallelFor(int count, int threads, Fn&& fn) {
    std::atomic<int> next{0};
    auto work = [&]() { for (int i; (i = next++) < count;) fn(i); };
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) pool.emplace_back(work);
    work();
    for (auto& t : pool) t.join();
}
}

void Sponza::clear() {
    if (worker_.joinable()) worker_.join();
    baking_ = bakeDone_ = false;
    pending_.reset();
    current_ = Bake();
    for (auto& t : textureCache_) glDeleteTextures(1, &t.second);
    textureCache_.clear();
    if (white_) glDeleteTextures(1, &white_);
    if (flatNormal_) glDeleteTextures(1, &flatNormal_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (shadowTex_) glDeleteTextures(1, &shadowTex_);
    if (shadowFbo_) glDeleteFramebuffers(1, &shadowFbo_);
    if (volTex_[0]) glDeleteTextures(4, volTex_);
    white_ = flatNormal_ = vbo_ = ebo_ = vao_ = shadowTex_ = shadowFbo_ = 0;
    for (GLuint& t : volTex_) t = 0;
    materials_.clear();
    parts_.clear();
    shadowBuilt_ = volReady_ = false;
}

bool Sponza::load(const std::string& gltfPath, int maxTextureSize, float anisotropy) {
    if (loaded()) return true;
    auto t0 = std::chrono::steady_clock::now();
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(gltfPath, aiProcess_Triangulate | aiProcess_PreTransformVertices |
                                                           aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        std::cerr << "Sponza: could not load " << gltfPath << ": " << importer.GetErrorString() << "\n";
        return false;
    }
    std::string dir = gltfPath.substr(0, gltfPath.find_last_of("/\\") + 1);

    // --- Materials: collect the textures, decode them on all cores, then upload ---
    struct Request { std::string path; int kind; Image image; };
    std::vector<Request> requests;
    auto request = [&](const aiMaterial* m, aiTextureType type, int kind) -> int {
        aiString p;
        if (m->GetTexture(type, 0, &p) != AI_SUCCESS) return -1;
        std::string path = dir + p.C_Str();
        for (size_t i = 0; i < requests.size(); ++i)
            if (requests[i].path == path && requests[i].kind == kind) return (int)i;
        requests.push_back({path, kind, {}});
        return (int)requests.size() - 1;
    };
    std::vector<glm::ivec3> matTex(scene->mNumMaterials);
    materials_.resize(scene->mNumMaterials);
    for (unsigned i = 0; i < scene->mNumMaterials; ++i) {
        const aiMaterial* m = scene->mMaterials[i];
        Material& M = materials_[i];
        int a = request(m, aiTextureType_BASE_COLOR, ALBEDO);
        if (a < 0) a = request(m, aiTextureType_DIFFUSE, ALBEDO);
        matTex[i] = glm::ivec3(a, request(m, aiTextureType_NORMALS, NORMAL),
                               request(m, aiTextureType_GLTF_METALLIC_ROUGHNESS, METAL_ROUGH));
        aiColor4D base(1, 1, 1, 1);
        if (m->Get(AI_MATKEY_BASE_COLOR, base) != AI_SUCCESS) m->Get(AI_MATKEY_COLOR_DIFFUSE, base);
        M.baseColor = glm::vec4(base.r, base.g, base.b, base.a);
        m->Get(AI_MATKEY_METALLIC_FACTOR, M.metallic);
        m->Get(AI_MATKEY_ROUGHNESS_FACTOR, M.roughness);
        aiString mode;
        if (m->Get(AI_MATKEY_GLTF_ALPHAMODE, mode) == AI_SUCCESS) M.mask = std::string(mode.C_Str()) == "MASK";
        m->Get(AI_MATKEY_GLTF_ALPHACUTOFF, M.cutoff);
    }
    stbi_set_flip_vertically_on_load(false);
    int threads = std::max(1u, std::thread::hardware_concurrency());
    bool s3tc = GLEW_EXT_texture_compression_s3tc;
    parallelFor((int)requests.size(), threads, [&](int i) {
        requests[i].image = decode(requests[i].path, requests[i].kind, maxTextureSize, s3tc);
    });
    std::vector<GLuint> uploaded(requests.size(), 0);
    for (size_t i = 0; i < requests.size(); ++i) {
        if (requests[i].image.levels.empty()) {
            std::cerr << "Sponza: missing texture " << requests[i].path << "\n";
            continue;
        }
        uploaded[i] = upload(requests[i].image, requests[i].kind, anisotropy);
        requests[i].image.levels.clear();   // free the CPU copy (the average is still needed below)
        textureCache_.push_back({requests[i].path, uploaded[i]});
    }
    white_ = solidTexture(255, 255, 255, 255);
    flatNormal_ = solidTexture(128, 128, 255, 255);
    for (size_t i = 0; i < materials_.size(); ++i) {
        Material& M = materials_[i];
        glm::ivec3 t = matTex[i];
        M.albedo = t.x >= 0 && uploaded[t.x] ? uploaded[t.x] : white_;
        M.normal = t.y >= 0 && uploaded[t.y] ? uploaded[t.y] : 0;
        M.metalRough = t.z >= 0 && uploaded[t.z] ? uploaded[t.z] : 0;
        M.average = glm::vec3(M.baseColor) * (t.x >= 0 ? requests[t.x].image.average : glm::vec3(1.0f));
    }

    // --- Geometry: one vertex / index buffer, one part per glTF primitive ---
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    std::vector<glm::vec3> triPos;
    std::vector<int> triMat;
    std::vector<uint8_t> triFlags;
    bmin_ = glm::vec3(1e30f);
    bmax_ = glm::vec3(-1e30f);
    for (unsigned mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* mesh = scene->mMeshes[mi];
        uint32_t base = (uint32_t)verts.size();
        glm::vec3 mn(1e30f), mx(-1e30f);
        for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
            Vertex V;
            V.pos = glm::vec3(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z);
            V.normal = mesh->mNormals ? glm::vec3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z) : glm::vec3(0, 1, 0);
            V.tangent = glm::vec4(1, 0, 0, 1);
            if (mesh->mTangents && mesh->mBitangents) {
                glm::vec3 t(mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z);
                glm::vec3 b(mesh->mBitangents[v].x, mesh->mBitangents[v].y, mesh->mBitangents[v].z);
                V.tangent = glm::vec4(t, glm::dot(glm::cross(V.normal, t), b) < 0.0f ? -1.0f : 1.0f);
            }
            // Assimp flips glTF's V on import; undo it (images are uploaded top row first, as glTF expects)
            V.uv = mesh->mTextureCoords[0] ? glm::vec2(mesh->mTextureCoords[0][v].x, 1.0f - mesh->mTextureCoords[0][v].y) : glm::vec2(0);
            mn = glm::min(mn, V.pos);
            mx = glm::max(mx, V.pos);
            verts.push_back(V);
        }
        Part part;
        part.firstIndex = (GLint)indices.size();
        part.material = (int)mesh->mMaterialIndex;
        bool mask = materials_[part.material].mask;
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue;
            for (int k = 0; k < 3; ++k) {
                indices.push_back(base + face.mIndices[k]);
                triPos.push_back(verts[base + face.mIndices[k]].pos);
            }
            triMat.push_back(part.material);
            triFlags.push_back(mask ? kAlphaTested : 0);
        }
        part.count = (GLsizei)(indices.size() - part.firstIndex);
        part.center = (mn + mx) * 0.5f;
        part.radius = glm::length(mx - mn) * 0.5f;
        if (part.count) parts_.push_back(part);
        bmin_ = glm::min(bmin_, mn);
        bmax_ = glm::max(bmax_, mx);
    }
    // Opaque first (fills depth early), alpha-tested last; grouped by material to save texture binds
    std::sort(parts_.begin(), parts_.end(), [&](const Part& a, const Part& b) {
        bool ma = materials_[a.material].mask, mb = materials_[b.material].mask;
        return ma != mb ? !ma : a.material < b.material;
    });

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    glBindVertexArray(0);

    // --- Gameplay / light baking structure ---
    bvh_.build(std::move(triPos), std::move(triMat), std::move(triFlags));

    // Light volume covering the building: ~0.6 m cells
    volCell_ = glm::vec3(0.6f);
    volMin_ = bmin_ - volCell_ * 0.5f;
    volRes_ = glm::ivec3(glm::ceil((bmax_ - bmin_ + volCell_) / volCell_)) + 1;

    // Spawn: on the floor at the east end of the courtyard, looking west along the nave
    spawn_ = glm::vec3(0.0f, 1.7f, 0.0f);
    spawnYaw_ = 180.0f;
    for (float f = 0.72f; f > 0.0f; f -= 0.04f) {
        float x = bmin_.x + (bmax_.x - bmin_.x) * f, z = (bmin_.z + bmax_.z) * 0.5f;
        float g = groundAt(x, z, bmin_.y + 1.0f);
        if (g < -1e8f) continue;
        glm::vec3 eye(x, g + 1.7f, z), pushed = eye;
        collide(pushed, 1.7f);
        if (glm::length(pushed - eye) < 0.01f) { spawn_ = eye; break; }
    }

    float secs = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "Sponza: " << bvh_.triangleCount() << " triangles, " << materials_.size() << " materials, "
              << requests.size() << " textures (" << maxTextureSize << " px max) loaded in " << secs << " s\n";
    return true;
}

void Sponza::setAnisotropy(float amount) {
    if (!GLEW_EXT_texture_filter_anisotropic) return;
    GLfloat maxAniso = 1.0f;
    glGetFloatv(0x84FF /*GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT*/, &maxAniso);
    for (const Material& m : materials_)
        for (GLuint t : {m.albedo, m.normal}) {
            if (!t || t == white_) continue;
            glBindTexture(GL_TEXTURE_2D, t);
            glTexParameterf(GL_TEXTURE_2D, 0x84FE, std::max(1.0f, std::min(amount, maxAniso)));
        }
}

// ===========================================================================
// Rendering
// ===========================================================================
void Sponza::draw(GLuint program, const glm::mat4& view, const glm::mat4& proj, float indirect) const {
    drawCalls_ = 0;
    if (!loaded() || !program) return;
    glm::mat4 vp = proj * view;
    // Frustum planes (Gribb-Hartmann) for culling parts by their bounding spheres
    glm::vec4 planes[6];
    glm::mat4 m = glm::transpose(vp);
    planes[0] = m[3] + m[0]; planes[1] = m[3] - m[0];
    planes[2] = m[3] + m[1]; planes[3] = m[3] - m[1];
    planes[4] = m[3] + m[2]; planes[5] = m[3] - m[2];
    for (auto& p : planes) p /= glm::length(glm::vec3(p));

    glUseProgram(program);
    auto U = [&](const char* n) { return glGetUniformLocation(program, n); };
    glUniformMatrix4fv(U("viewProj"), 1, GL_FALSE, &vp[0][0]);
    glUniform1i(U("albedoTex"), 0);
    glUniform1i(U("normalTex"), 1);
    glUniform1i(U("metalRoughTex"), 5);
    // Light volume on units 2, 4, 11, 12 (free while the island isn't drawn)
    const int volUnits[4] = {2, 4, 11, 12};
    const char* volNames[4] = {"skyVis", "bounceR", "bounceG", "bounceB"};
    for (int i = 0; i < 4; ++i) {
        glUniform1i(U(volNames[i]), volUnits[i]);
        glActiveTexture(GL_TEXTURE0 + volUnits[i]);
        glBindTexture(GL_TEXTURE_3D, volTex_[i]);
    }
    glUniform1i(U("hasVolume"), volReady_ ? 1 : 0);
    glUniform3fv(U("volMin"), 1, &volMin_.x);
    glUniform3fv(U("volCell"), 1, &volCell_.x);
    glm::vec3 res(volRes_);
    glUniform3fv(U("volRes"), 1, &res.x);
    glUniform1f(U("indirectStrength"), indirect);
    GLint locBase = U("baseColorFactor"), locMetal = U("metallicFactor"), locRough = U("roughnessFactor");
    GLint locMask = U("alphaMask"), locCut = U("alphaCutoff"), locHasN = U("hasNormalTex"), locHasMR = U("hasMetalRough");

    glBindVertexArray(vao_);
    int bound = -1;
    for (const Part& p : parts_) {
        bool visible = true;
        for (const auto& pl : planes)
            if (glm::dot(glm::vec3(pl), p.center) + pl.w < -p.radius) { visible = false; break; }
        if (!visible) continue;
        if (p.material != bound) {
            const Material& M = materials_[p.material];
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, M.albedo);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, M.normal ? M.normal : flatNormal_);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, M.metalRough ? M.metalRough : white_);
            glUniform4fv(locBase, 1, &M.baseColor.x);
            glUniform1f(locMetal, M.metalRough ? M.metallic : 0.0f);
            glUniform1f(locRough, M.roughness);
            glUniform1i(locMask, M.mask ? 1 : 0);
            glUniform1f(locCut, M.cutoff);
            glUniform1i(locHasN, M.normal ? 1 : 0);
            glUniform1i(locHasMR, M.metalRough ? 1 : 0);
            bound = p.material;
        }
        glDrawElements(GL_TRIANGLES, p.count, GL_UNSIGNED_INT, (void*)(sizeof(uint32_t) * p.firstIndex));
        ++drawCalls_;
    }
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void Sponza::setShadowResolution(int res) {
    if (res == shadowRes_) return;
    shadowRes_ = res;
    if (shadowTex_) glDeleteTextures(1, &shadowTex_);
    if (shadowFbo_) glDeleteFramebuffers(1, &shadowFbo_);
    shadowTex_ = shadowFbo_ = 0;
    shadowBuilt_ = false;
}

void Sponza::buildShadow(GLuint program, const glm::vec3& sunDir) {
    if (!loaded() || !program) return;
    glm::vec3 sun = glm::normalize(sunDir);
    if (sun.y <= 0.0f) { shadowBuilt_ = false; return; }
    if (shadowBuilt_ && glm::dot(sun, shadowSun_) > 0.999999f) return;

    GLint prevFbo, viewport[4], prevProgram;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    if (!shadowTex_) {
        glGenTextures(1, &shadowTex_);
        glBindTexture(GL_TEXTURE_2D, shadowTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowRes_, shadowRes_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        const float border[] = {1, 1, 1, 1};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        glGenFramebuffers(1, &shadowFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Sponza: could not create the sun shadow framebuffer\n";
            glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
            glDeleteFramebuffers(1, &shadowFbo_);
            glDeleteTextures(1, &shadowTex_);
            shadowFbo_ = shadowTex_ = 0;
            return;
        }
    }
    // Orthographic light volume fitted tightly around the building's bounds
    glm::vec3 center = (bmin_ + bmax_) * 0.5f;
    glm::vec3 up = std::fabs(sun.y) > 0.98f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    glm::mat4 lightView = glm::lookAt(center + sun * 100.0f, center, up);
    glm::vec3 lmn(1e30f), lmx(-1e30f);
    for (int i = 0; i < 8; ++i) {
        glm::vec3 c((i & 1) ? bmax_.x : bmin_.x, (i & 2) ? bmax_.y : bmin_.y, (i & 4) ? bmax_.z : bmin_.z);
        glm::vec3 l = glm::vec3(lightView * glm::vec4(c, 1.0f));
        lmn = glm::min(lmn, l);
        lmx = glm::max(lmx, l);
    }
    shadowMatrix_ = glm::ortho(lmn.x, lmx.x, lmn.y, lmx.y, -lmx.z - 1.0f, -lmn.z + 1.0f) * lightView;

    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, shadowRes_, shadowRes_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "lightViewProj"), 1, GL_FALSE, &shadowMatrix_[0][0]);
    glUniform1i(glGetUniformLocation(program, "albedoTex"), 0);
    GLint locMask = glGetUniformLocation(program, "alphaMask"), locCut = glGetUniformLocation(program, "alphaCutoff");
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    int bound = -1;
    for (const Part& p : parts_) {
        const Material& M = materials_[p.material];
        if (p.material != bound) {
            glBindTexture(GL_TEXTURE_2D, M.albedo);
            glUniform1i(locMask, M.mask ? 1 : 0);
            glUniform1f(locCut, M.cutoff);
            bound = p.material;
        }
        glDrawElements(GL_TRIANGLES, p.count, GL_UNSIGNED_INT, (void*)(sizeof(uint32_t) * p.firstIndex));
    }
    glBindVertexArray(0);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glUseProgram(prevProgram);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    shadowSun_ = sun;
    shadowBuilt_ = true;
}

void Sponza::bindShadow(GLuint program, int unit, float strength, bool enabled) const {
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "villageShadowTex"), unit);
    glUniform1i(glGetUniformLocation(program, "hasVillageShadow"), enabled && shadowBuilt_ ? 1 : 0);
    glUniform1f(glGetUniformLocation(program, "villageShadowStrength"), strength);
    glUniformMatrix4fv(glGetUniformLocation(program, "villageLightViewProj"), 1, GL_FALSE, &shadowMatrix_[0][0]);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glActiveTexture(GL_TEXTURE0);
}

// ===========================================================================
// Baked indirect light
// ===========================================================================
// For every cell of the volume, cast rays in all directions:
//  - rays that leave the building see the sky -> sky visibility
//  - rays that hit a sunlit surface pick up sunlight reflected by it (albedo * N.L) -> bounce light
// Both are projected onto L1 spherical harmonics, stored as (b, a) so that irradiance towards a
// normal n is max(a + dot(b, n), 0), normalised so an open sky gives 1.
void Sponza::bake(const glm::vec3& sunDir, const Bake* prev, Bake& out, int threads) const {
    const int kRays = 48;
    glm::vec3 dirs[kRays];
    for (int i = 0; i < kRays; ++i) {   // spherical Fibonacci
        float y = 1.0f - (i + 0.5f) * 2.0f / kRays, r = std::sqrt(std::max(0.0f, 1.0f - y * y));
        float phi = i * 2.39996323f;
        dirs[i] = glm::vec3(std::cos(phi) * r, y, std::sin(phi) * r);
    }
    glm::vec3 toSun = glm::normalize(-sunDir);
    bool sunUp = toSun.y > 0.0f;
    int cells = volRes_.x * volRes_.y * volRes_.z;
    out.sun = sunDir;
    out.sky.assign(cells, glm::vec4(0.0f));
    out.r.assign(cells, glm::vec4(0.0f));
    out.g.assign(cells, glm::vec4(0.0f));
    out.b.assign(cells, glm::vec4(0.0f));
    std::vector<uint8_t> valid(cells, 0);

    parallelFor(volRes_.z, threads, [&](int z) {
        for (int y = 0; y < volRes_.y; ++y)
            for (int x = 0; x < volRes_.x; ++x) {
                int idx = (z * volRes_.y + y) * volRes_.x + x;
                glm::vec3 p = volMin_ + (glm::vec3(x, y, z) + 0.5f) * volCell_;
                // A random rotation per cell turns ray-pattern banding into noise the filtering hides
                uint32_t h = hash32((uint32_t)idx * 2654435761u + 17u);
                float a = (h & 0xffff) / 65535.0f * 6.2831853f, ca = std::cos(a), sa = std::sin(a);
                float tilt = ((h >> 16) / 65535.0f - 0.5f) * 0.3f;
                float sky = 0.0f; glm::vec3 skyDir(0.0f);
                glm::vec3 bounce(0.0f), bx(0.0f), by(0.0f), bz(0.0f);
                int back = 0;
                for (int i = 0; i < kRays; ++i) {
                    glm::vec3 d0 = dirs[i];
                    glm::vec3 d(ca * d0.x - sa * d0.z, d0.y, sa * d0.x + ca * d0.z);
                    d = glm::normalize(d + glm::vec3(0.0f, tilt * (1.0f - std::fabs(d.y)), 0.0f));
                    TriangleBvh::Hit hit;
                    if (!bvh_.intersect(p, d, 80.0f, hit)) {
                        // Open sky above; below the horizon the light comes off the (unmodelled) ground
                        float L = d.y > 0.0f ? 1.0f : 0.25f;
                        sky += L; skyDir += L * d;
                        continue;
                    }
                    glm::vec3 n = bvh_.normal(hit.tri);
                    bool twoSided = materials_[bvh_.id(hit.tri)].mask;
                    if (glm::dot(n, d) > 0.0f) {
                        if (!twoSided) { ++back; continue; }
                        n = -n;
                    }
                    const glm::vec3& albedo = materials_[bvh_.id(hit.tri)].average;
                    glm::vec3 hp = p + d * hit.t + n * 0.01f;
                    // Light leaving the hit surface: direct sun, plus (from the previous bake) the sky and
                    // bounce light that reaches it - each bake adds one more bounce
                    glm::vec3 L(0.0f);
                    if (sunUp) {
                        float ndl = glm::dot(n, toSun);
                        if (ndl > 0.0f && !bvh_.occluded(hp, toSun, 100.0f)) L += albedo * ndl;
                    }
                    if (prev) {
                        glm::vec3 q = hp + n * 0.3f;
                        auto irr = [&](const std::vector<glm::vec4>& v) {
                            glm::vec4 c = sample(v, q);
                            return std::max(c.w + glm::dot(glm::vec3(c), n), 0.0f);
                        };
                        L += albedo * glm::vec3(irr(prev->r), irr(prev->g), irr(prev->b));
                        float skyLight = glm::dot(albedo, glm::vec3(0.3f, 0.59f, 0.11f)) * irr(prev->sky);
                        sky += skyLight; skyDir += skyLight * d;
                    }
                    bounce += L;
                    bx += L.r * d; by += L.g * d; bz += L.b * d;
                }
                // Cells inside walls and columns mostly see back faces: filled from neighbours below
                valid[idx] = back < kRays / 4;
                float inv = 1.0f / kRays;
                out.sky[idx] = glm::vec4(skyDir * 2.0f * inv, sky * inv);
                out.r[idx] = glm::vec4(bx * 2.0f * inv, bounce.r * inv);
                out.g[idx] = glm::vec4(by * 2.0f * inv, bounce.g * inv);
                out.b[idx] = glm::vec4(bz * 2.0f * inv, bounce.b * inv);
            }
    });

    // Dilate valid cells into invalid ones so trilinear filtering near walls doesn't pull in darkness
    for (int pass = 0; pass < 3; ++pass) {
        std::vector<uint8_t> nextValid = valid;
        for (int z = 0; z < volRes_.z; ++z)
            for (int y = 0; y < volRes_.y; ++y)
                for (int x = 0; x < volRes_.x; ++x) {
                    int idx = (z * volRes_.y + y) * volRes_.x + x;
                    if (valid[idx]) continue;
                    glm::vec4 s(0.0f), r(0.0f), g(0.0f), b(0.0f);
                    int n = 0;
                    for (int dz = -1; dz <= 1; ++dz) for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                        int X = x + dx, Y = y + dy, Z = z + dz;
                        if (X < 0 || Y < 0 || Z < 0 || X >= volRes_.x || Y >= volRes_.y || Z >= volRes_.z) continue;
                        int j = (Z * volRes_.y + Y) * volRes_.x + X;
                        if (!valid[j]) continue;
                        s += out.sky[j]; r += out.r[j]; g += out.g[j]; b += out.b[j];
                        ++n;
                    }
                    if (!n) continue;
                    out.sky[idx] = s / float(n); out.r[idx] = r / float(n);
                    out.g[idx] = g / float(n); out.b[idx] = b / float(n);
                    nextValid[idx] = 1;
                }
        valid.swap(nextValid);
    }
}

glm::vec4 Sponza::sample(const std::vector<glm::vec4>& vol, const glm::vec3& p) const {
    if (vol.empty()) return glm::vec4(0.0f);
    glm::vec3 f = (p - volMin_) / volCell_ - 0.5f;
    f = glm::clamp(f, glm::vec3(0.0f), glm::vec3(volRes_ - 1) - 0.001f);
    glm::ivec3 i = glm::ivec3(f);
    glm::vec3 t = f - glm::vec3(i);
    auto at = [&](int x, int y, int z) {
        x = std::min(x, volRes_.x - 1); y = std::min(y, volRes_.y - 1); z = std::min(z, volRes_.z - 1);
        return vol[(z * volRes_.y + y) * volRes_.x + x];
    };
    glm::vec4 c00 = glm::mix(at(i.x, i.y, i.z), at(i.x + 1, i.y, i.z), t.x);
    glm::vec4 c10 = glm::mix(at(i.x, i.y + 1, i.z), at(i.x + 1, i.y + 1, i.z), t.x);
    glm::vec4 c01 = glm::mix(at(i.x, i.y, i.z + 1), at(i.x + 1, i.y, i.z + 1), t.x);
    glm::vec4 c11 = glm::mix(at(i.x, i.y + 1, i.z + 1), at(i.x + 1, i.y + 1, i.z + 1), t.x);
    return glm::mix(glm::mix(c00, c10, t.y), glm::mix(c01, c11, t.y), t.z);
}

glm::vec4 Sponza::lightAt(const glm::vec3& p) const {
    if (!volReady_) return glm::vec4(1.0f);
    return glm::vec4(sample(current_.sky, p).w, sample(current_.r, p).w, sample(current_.g, p).w, sample(current_.b, p).w);
}

void Sponza::uploadBake(const Bake& b) {
    if (!volTex_[0]) glGenTextures(4, volTex_);
    const std::vector<glm::vec4>* data[4] = {&b.sky, &b.r, &b.g, &b.b};
    for (int i = 0; i < 4; ++i) {
        glBindTexture(GL_TEXTURE_3D, volTex_[i]);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, volRes_.x, volRes_.y, volRes_.z, 0, GL_RGBA, GL_FLOAT, data[i]->data());
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_3D, 0);
    bakedSun_ = b.sun;
    volReady_ = true;
}

void Sponza::updateIndirect(const glm::vec3& sunDir) {
    if (!loaded()) return;
    if (bakeDone_) {   // a background bake finished: show it
        worker_.join();
        uploadBake(*pending_);
        current_ = std::move(*pending_);
        pending_.reset();
        bakeDone_ = false;
        baking_ = false;
    }
    int threads = std::max(1u, std::thread::hardware_concurrency());
    if (!volReady_) {
        // Sky + sun, then two more bounces
        auto t0 = std::chrono::steady_clock::now();
        for (int bounce = 0; bounce < 3; ++bounce) {
            Bake b;
            bake(sunDir, bounce ? &current_ : nullptr, b, threads);
            current_ = std::move(b);
        }
        uploadBake(current_);
        std::cout << "Sponza: baked indirect light (" << volRes_.x << "x" << volRes_.y << "x" << volRes_.z << " cells, 3 bounces) in "
                  << std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count() << " s\n";
        return;
    }
    // Re-bake when the sun has moved by more than ~1.5 degrees, or to add the next bounce for a
    // sun that moved recently (one bake at a time, off the render thread)
    bool moved = glm::dot(glm::normalize(sunDir), glm::normalize(bakedSun_)) < 0.99966f;
    if (!baking_ && (moved || settleBakes_ > 0)) {
        settleBakes_ = moved ? 2 : settleBakes_ - 1;
        baking_ = true;
        pending_ = std::make_unique<Bake>();
        worker_ = std::thread([this, sunDir, threads]() {
            bake(sunDir, &current_, *pending_, std::max(1, threads - 1));
            bakeDone_ = true;
        });
    }
}

// ===========================================================================
// Gameplay queries
// ===========================================================================
float Sponza::groundAt(float x, float z, float feetY) const {
    if (!loaded()) return -1e9f;
    // Several rays down over the foot area, so cracks between floor tiles don't swallow the player
    const float stepUp = 0.45f;
    float best = -1e9f;
    const glm::vec2 offs[5] = {{0, 0}, {0.15f, 0}, {-0.15f, 0}, {0, 0.15f}, {0, -0.15f}};
    for (const auto& o : offs) {
        glm::vec3 origin(x + o.x, feetY + stepUp, z + o.y);
        TriangleBvh::Hit hit;
        if (!bvh_.intersect(origin, glm::vec3(0, -1, 0), 50.0f, hit, kAlphaTested)) continue;
        if (std::fabs(bvh_.normal(hit.tri).y) < 0.55f) continue;   // too steep to stand on
        best = std::max(best, origin.y - hit.t);
    }
    return best;
}

void Sponza::collide(glm::vec3& eye, float eyeHeight) const {
    if (!loaded()) return;
    // The body is three spheres stacked above the feet; the lowest clears steps up to ~0.25 m.
    // Walls push horizontally only, so sliding along them never lifts or sinks the player.
    const float radius = 0.3f;
    const float heights[3] = {0.55f, 1.0f, 1.45f};
    for (int iter = 0; iter < 3; ++iter) {
        bool moved = false;
        for (float hgt : heights) {
            glm::vec3 c(eye.x, eye.y - eyeHeight + hgt, eye.z);
            glm::vec3 ext(radius);
            bvh_.query(c - ext, c + ext, [&](int tri) {
                glm::vec3 n = bvh_.normal(tri);
                if (std::fabs(n.y) > 0.75f) return;   // floors and ceilings are handled by groundAt / headroom
                glm::vec3 cc(eye.x, c.y, eye.z);
                glm::vec3 cp = TriangleBvh::closestPoint(cc, bvh_.vertex(tri, 0), bvh_.vertex(tri, 1), bvh_.vertex(tri, 2));
                glm::vec3 delta = cc - cp;
                float d = glm::length(delta);
                if (d >= radius) return;
                glm::vec2 push(delta.x, delta.z);
                float pl = glm::length(push);
                if (pl < 1e-4f) {
                    push = glm::vec2(n.x, n.z);
                    if (glm::dot(glm::vec3(n.x, 0, n.z), delta) < 0.0f) push = -push;
                    pl = glm::length(push);
                    if (pl < 1e-4f) return;
                }
                push *= (radius - d) / pl;
                eye.x += push.x;
                eye.z += push.y;
                moved = true;
            }, kAlphaTested);
        }
        if (!moved) break;
    }
}

float Sponza::headroom(const glm::vec3& eye) const {
    TriangleBvh::Hit hit;
    if (loaded() && bvh_.intersect(eye, glm::vec3(0, 1, 0), 5.0f, hit, kAlphaTested)) return hit.t;
    return 1e9f;
}
