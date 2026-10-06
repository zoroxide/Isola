#include "Foliage.h"
#include "Terrain.h"
#include "libs/stb_image.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static float hash21(float x, float y) {
    float h = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
    return h - std::floor(h);
}
static float valueNoise(float x, float y) {
    float xi = std::floor(x), yi = std::floor(y), xf = x - xi, yf = y - yi;
    float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
    float a = hash21(xi, yi), b = hash21(xi + 1, yi), c = hash21(xi, yi + 1), d = hash21(xi + 1, yi + 1);
    return (a + (b - a) * u) + ((c + (d - c) * u) - (a + (b - a) * u)) * v;
}
static float fbm2(float x, float y) {
    float s = 0, a = 0.5f;
    for (int i = 0; i < 4; ++i) { s += a * valueNoise(x, y); x *= 2.03f; y *= 2.03f; a *= 0.5f; }
    return s / 0.9375f;
}

Foliage::~Foliage() {
    for (auto& m : meshes_) {
        if (m.vbo) glDeleteBuffers(1, &m.vbo);
        if (m.ebo) glDeleteBuffers(1, &m.ebo);
        if (m.vao) glDeleteVertexArrays(1, &m.vao);
    }
    GLuint bufs[] = { instanceVBO_, impQuadVBO_, impInstVBO_, grassEBO_, grassFarEBO_ };
    for (GLuint b : bufs) if (b) glDeleteBuffers(1, &b);
    GLuint vaos[] = { grassVAO_, impVAO_, grassFarVAO_ };
    for (GLuint v : vaos) if (v) glDeleteVertexArrays(1, &v);
    GLuint texs[] = { texArray_, canopyTex_, canopyHeightTex_, impAlbedo_, impNormal_ };
    for (GLuint t : texs) if (t) glDeleteTextures(1, &t);
}

// ---------------------------------------------------------------------------
// Procedural tree meshes. Units are metres; instances are scaled/rotated on the GPU.
// ---------------------------------------------------------------------------
namespace {
using TV = std::vector<float>;
struct Builder {
    struct V { glm::vec3 p, n; glm::vec2 uv; float layer, bend, flutter, ao; };
    std::vector<V> v;
    std::vector<unsigned> idx;
    float height = 10.0f;

    float bendAt(float y) const { float t = glm::clamp(y / height, 0.0f, 1.0f); return t * t; }

    // Tapered bark cylinder from a to b
    void cylinder(glm::vec3 a, glm::vec3 b, float r0, float r1, int sides, int segs, float uvRepeat) {
        glm::vec3 axis = b - a;
        float len = glm::length(axis);
        glm::vec3 d = axis / len;
        glm::vec3 t = glm::normalize(glm::cross(std::fabs(d.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), d));
        glm::vec3 bt = glm::cross(d, t);
        unsigned base = (unsigned)v.size();
        for (int s = 0; s <= segs; ++s) {
            float f = s / (float)segs;
            glm::vec3 c = a + axis * f;
            float r = glm::mix(r0, r1, f);
            for (int k = 0; k <= sides; ++k) {
                float ang = k / (float)sides * glm::two_pi<float>();
                glm::vec3 n = t * std::cos(ang) + bt * std::sin(ang);
                glm::vec3 p = c + n * r;
                // Darker (occluded) near the ground and inside the crown
                float ao = glm::mix(0.55f, 0.9f, glm::clamp(p.y / (height * 0.4f), 0.0f, 1.0f));
                v.push_back({ p, n, glm::vec2(k / (float)sides * uvRepeat * 0.5f, f * len * 0.35f), 0.0f, bendAt(p.y), 0.0f, ao });
            }
        }
        for (int s = 0; s < segs; ++s) for (int k = 0; k < sides; ++k) {
            unsigned i0 = base + s * (sides + 1) + k, i1 = i0 + 1, i2 = i0 + sides + 1, i3 = i2 + 1;
            idx.insert(idx.end(), { i0, i2, i1, i1, i2, i3 });
        }
    }

    // A textured card. `from` is the attachment point, `dir` the length axis (u), `side` the width axis (v).
    // Normals are bent towards `volumeCenter` outward so the crown shades like a volume, not flat planes.
    void card(glm::vec3 from, glm::vec3 dir, glm::vec3 side, float len, float width, float layer,
              glm::vec2 uv0, glm::vec2 uv1, glm::vec3 volumeCenter, float volumeRadius, bool centred) {
        glm::vec3 n = glm::normalize(glm::cross(dir, side));
        glm::vec3 origin = centred ? from - dir * len * 0.5f : from;
        glm::vec3 corners[4] = { origin - side * width * 0.5f, origin + dir * len - side * width * 0.5f,
                                 origin + dir * len + side * width * 0.5f, origin + side * width * 0.5f };
        glm::vec2 uvs[4] = { {uv0.x, uv0.y}, {uv1.x, uv0.y}, {uv1.x, uv1.y}, {uv0.x, uv1.y} };
        unsigned base = (unsigned)v.size();
        for (int i = 0; i < 4; ++i) {
            glm::vec3 p = corners[i];
            glm::vec3 radial = p - volumeCenter;
            float rl = glm::length(radial);
            glm::vec3 vn = glm::normalize(glm::mix(n, rl > 1e-3f ? radial / rl : n, 0.75f));
            float ao = glm::clamp(0.35f + 0.65f * rl / std::max(volumeRadius, 0.1f), 0.35f, 1.0f);
            // Leaves attached to the trunk barely flutter; outer tips flutter most
            float flutter = centred ? 1.0f : (i == 0 || i == 3 ? 0.15f : 1.0f);
            v.push_back({ p, vn, uvs[i], layer, bendAt(p.y), flutter, ao });
        }
        idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
    }
};

glm::vec3 randomUnit(std::mt19937& rng) {
    std::uniform_real_distribution<float> U(-1.0f, 1.0f);
    for (;;) { glm::vec3 p(U(rng), U(rng), U(rng)); float l = glm::length(p); if (l > 0.1f && l <= 1.0f) return p / l; }
}

// Fir: straight trunk and whorls of drooping branch cards, a narrow cone silhouette
void buildConifer(Builder& b, std::mt19937& rng) {
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const float H = 12.0f + U(rng) * 3.0f;
    b.height = H;
    b.cylinder({0, -0.5f, 0}, {0, H, 0}, 0.32f, 0.03f, 8, 6, 2.0f);
    const float y0 = 0.2f * H, maxR = 0.27f * H;
    glm::vec3 axisCenter(0, H * 0.5f, 0);
    float az = U(rng) * 6.28f;
    for (float y = y0; y < H * 0.93f; y += 0.45f + U(rng) * 0.25f) {
        float t = (y - y0) / (H - y0);
        float L = maxR * std::pow(1.0f - t, 0.9f) + 0.5f;
        int count = 5 + (int)(U(rng) * 2);
        az += 0.7f + U(rng) * 0.5f;
        for (int i = 0; i < count; ++i) {
            float a = az + i / (float)count * 6.2832f + (U(rng) - 0.5f) * 0.4f;
            glm::vec3 dir = glm::normalize(glm::vec3(std::cos(a), -0.18f - 0.25f * (1.0f - t), std::sin(a)));
            glm::vec3 across = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
            glm::vec3 up = glm::cross(across, dir);
            for (int k = 0; k < 2; ++k) { // two cards rolled +-35 deg around the branch -> volume from any angle
                float roll = (k == 0 ? 0.6f : -0.6f);
                glm::vec3 side = across * std::cos(roll) + up * std::sin(roll);
                glm::vec3 center(0, y, 0);
                b.card(glm::vec3(0, y, 0), dir, side, L, L * 0.8f, 1.0f, {0.02f, 0.0f}, {0.84f, 1.0f},
                       center - glm::vec3(0, L * 0.3f, 0), L, false);
            }
        }
    }
    // Leader at the top: two crossed upward cards
    for (int k = 0; k < 2; ++k) {
        glm::vec3 side = k == 0 ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
        b.card({0, H * 0.86f, 0}, {0, 1, 0}, side, H * 0.16f, H * 0.1f, 1.0f, {0.02f, 0.0f}, {0.84f, 1.0f},
               glm::vec3(0, H * 0.8f, 0), H * 0.1f, false);
    }
    (void)axisCenter;
}

// Broadleaf: trunk splitting into a few limbs that carry a rounded crown of leaf clusters
void buildBroadleaf(Builder& b, std::mt19937& rng) {
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const float H = 9.0f + U(rng) * 2.5f;
    b.height = H;
    glm::vec3 lean(U(rng) - 0.5f, 0, U(rng) - 0.5f);
    glm::vec3 top = glm::vec3(0, H * 0.42f, 0) + lean * 0.8f;
    b.cylinder({0, -0.5f, 0}, top, 0.36f, 0.24f, 8, 4, 2.0f);
    glm::vec3 crown = top + glm::vec3(0, H * 0.3f, 0);
    glm::vec3 radii(H * 0.36f, H * 0.27f, H * 0.36f);
    int limbs = 4 + (int)(U(rng) * 2);
    std::vector<glm::vec3> ends;
    for (int i = 0; i < limbs; ++i) {
        float a = i / (float)limbs * 6.2832f + U(rng) * 0.6f;
        glm::vec3 end = crown + glm::vec3(std::cos(a) * radii.x * 0.55f, (U(rng) - 0.2f) * radii.y * 0.6f, std::sin(a) * radii.z * 0.55f);
        b.cylinder(top, end, 0.2f, 0.06f, 6, 3, 1.0f);
        ends.push_back(end);
    }
    int clusters = 90 + (int)(U(rng) * 20);
    for (int i = 0; i < clusters; ++i) {
        glm::vec3 dir = randomUnit(rng);
        dir.y = dir.y * 0.8f + 0.15f;
        dir = glm::normalize(dir);
        float r = 0.55f + 0.45f * std::sqrt(U(rng));
        glm::vec3 c = crown + dir * radii * r;
        glm::vec3 n = glm::normalize(glm::mix(dir, randomUnit(rng), 0.45f));
        glm::vec3 t = glm::normalize(glm::cross(n, std::fabs(n.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
        float spin = U(rng) * 6.28f;
        glm::vec3 u = t * std::cos(spin) + glm::cross(n, t) * std::sin(spin);
        glm::vec3 w = glm::cross(n, u);
        float size = 2.6f + U(rng) * 1.1f;
        b.card(c, u, w, size, size, 2.0f, {0, 0}, {1, 1}, crown, glm::length(radii) * 0.6f, true);
    }
}

// Acacia: leaning trunk, spreading limbs, flat umbrella-shaped crown
void buildAcacia(Builder& b, std::mt19937& rng) {
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const float H = 7.0f + U(rng) * 2.0f;
    b.height = H;
    glm::vec3 lean(U(rng) - 0.5f, 0, U(rng) - 0.5f);
    glm::vec3 fork = glm::vec3(0, H * 0.45f, 0) + lean * 1.4f;
    b.cylinder({0, -0.5f, 0}, fork, 0.28f, 0.2f, 7, 4, 2.0f);
    glm::vec3 crown = fork + glm::vec3(0, H * 0.4f, 0);
    float R = H * 0.6f;
    for (int i = 0; i < 3; ++i) {
        float a = i / 3.0f * 6.2832f + U(rng);
        glm::vec3 end = crown + glm::vec3(std::cos(a) * R * 0.5f, -H * 0.05f, std::sin(a) * R * 0.5f);
        b.cylinder(fork, end, 0.16f, 0.05f, 6, 3, 1.0f);
    }
    int clusters = 70 + (int)(U(rng) * 16);
    for (int i = 0; i < clusters; ++i) {
        float a = U(rng) * 6.2832f, r = std::sqrt(U(rng)) * R;
        glm::vec3 c = crown + glm::vec3(std::cos(a) * r, (U(rng) - 0.5f) * H * 0.12f + (1.0f - r / R) * H * 0.06f, std::sin(a) * r);
        glm::vec3 n = glm::normalize(glm::mix(glm::vec3(0, 1, 0), randomUnit(rng), 0.35f));
        glm::vec3 t = glm::normalize(glm::cross(n, glm::vec3(1, 0, 0)));
        float spin = U(rng) * 6.28f;
        glm::vec3 u = t * std::cos(spin) + glm::cross(n, t) * std::sin(spin);
        float size = 2.8f + U(rng) * 1.1f;
        b.card(c, u, glm::cross(n, u), size, size, 3.0f, {0, 0}, {1, 1}, crown - glm::vec3(0, H * 0.15f, 0), R, true);
    }
}
} // namespace

void Foliage::buildMesh(Mesh& m, const std::vector<TreeVertex>& v, const std::vector<unsigned>& idx) {
    glGenVertexArrays(1, &m.vao); glGenBuffers(1, &m.vbo); glGenBuffers(1, &m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(TreeVertex), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
    const GLsizei st = sizeof(TreeVertex);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(TreeVertex, p)); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(TreeVertex, n)); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, st, (void*)offsetof(TreeVertex, uv)); glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, st, (void*)offsetof(TreeVertex, layer)); glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, st, (void*)offsetof(TreeVertex, bend)); glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, st, (void*)offsetof(TreeVertex, ao)); glEnableVertexAttribArray(5);
    // Per-instance data (position+yaw, scale/phase/tint) comes from instanceVBO_, bound per draw
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(6);
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float))); glEnableVertexAttribArray(7);
    glVertexAttribDivisor(6, 1); glVertexAttribDivisor(7, 1);
    glBindVertexArray(0);
    m.count = (GLsizei)idx.size();
}

bool Foliage::init(const std::string& dir, const FoliagePrograms& programs) {
    prog_ = programs;

    // Texture array: every layer is 1024x1024 RGBA
    const char* files[4] = { "bark.jpg", "conifer_branch.png", "broadleaf_cluster.png", "acacia_cluster.png" };
    const int S = 1024;
    glGenTextures(1, &texArray_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
    // DXT5 keeps the leaf alpha and cuts memory bandwidth 4x (the driver compresses on upload)
    GLenum fmt = GLEW_EXT_texture_compression_s3tc ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : GL_RGBA8;
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, fmt, S, S, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    if (glGetError() != GL_NO_ERROR) { fmt = GL_RGBA8; glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, fmt, S, S, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr); }
    bool ok = true;
    stbi_set_flip_vertically_on_load(false);
    for (int i = 0; i < 4; ++i) {
        int w = 0, h = 0, c = 0;
        std::string path = dir + "/" + files[i];
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 4);
        if (!data || w != S || h != S) {
            std::cerr << "Foliage: missing or wrong-size texture " << path << "\n";
            ok = false;
            std::vector<unsigned char> fallback((size_t)S * S * 4, i == 0 ? 90 : 60);
            for (size_t p = 3; p < fallback.size(); p += 4) fallback[p] = 255;
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, S, S, 1, GL_RGBA, GL_UNSIGNED_BYTE, fallback.data());
        } else {
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, S, S, 1, GL_RGBA, GL_UNSIGNED_BYTE, data);
        }
        if (data) stbi_image_free(data);
    }
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glGenBuffers(1, &instanceVBO_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
    glBufferData(GL_ARRAY_BUFFER, 8 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    // 3 species x 3 random variants
    for (int s = 0; s < kSpecies; ++s) for (int k = 0; k < kVariants; ++k) {
        std::mt19937 rng(1000u + s * 97u + k * 13u);
        Builder b;
        if (s == 0) buildConifer(b, rng); else if (s == 1) buildBroadleaf(b, rng); else buildAcacia(b, rng);
        std::vector<TreeVertex> v; v.reserve(b.v.size());
        float maxR = 0.0f, maxY = 0.0f;
        for (auto& x : b.v) {
            v.push_back({ x.p, x.n, x.uv, x.layer, x.bend, x.flutter, x.ao });
            maxR = std::max(maxR, glm::length(glm::vec2(x.p.x, x.p.z)));
            maxY = std::max(maxY, x.p.y);
        }
        Mesh& m = meshes_[s * kVariants + k];
        buildMesh(m, v, b.idx);
        m.height = b.height;
        m.radius = s == 0 ? b.height * 0.27f : (s == 1 ? b.height * 0.4f : b.height * 0.62f);
        m.tile = std::max(2.0f * maxR, maxY + 0.5f) * 1.04f;   // square impostor frame
    }

    // Impostor billboard quad + instance buffer
    const float quad[8] = { -0.5f, 0.0f, 0.5f, 0.0f, -0.5f, 1.0f, 0.5f, 1.0f };
    glGenVertexArrays(1, &impVAO_); glGenBuffers(1, &impQuadVBO_); glGenBuffers(1, &impInstVBO_);
    glBindVertexArray(impVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, impQuadVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, impInstVBO_);
    glBufferData(GL_ARRAY_BUFFER, 8 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float))); glEnableVertexAttribArray(2);
    glVertexAttribDivisor(1, 1); glVertexAttribDivisor(2, 1);
    glBindVertexArray(0);
    bakeImpostors();

    // Grass index patterns for a clump: near blades have 3 segments + tip (7 vertices),
    // far blades 2 segments + tip (5 vertices). The vertex shader derives the blade from gl_VertexID.
    auto makeGrass = [&](GLuint& vao, GLuint& ebo, int verts) {
        std::vector<unsigned> gi;
        for (int b = 0; b < kBladesPerClump; ++b) {
            unsigned o = b * verts;
            for (int s = 0; s + 2 < verts; s += 2) {
                gi.insert(gi.end(), { o + s, o + s + 1, o + s + 2 });
                if (s + 3 < verts) gi.insert(gi.end(), { o + s + 2, o + s + 1, o + s + 3 });
            }
        }
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, gi.size() * sizeof(unsigned), gi.data(), GL_STATIC_DRAW);
        glBindVertexArray(0);
    };
    makeGrass(grassVAO_, grassEBO_, 7);
    makeGrass(grassFarVAO_, grassFarEBO_, 5);
    return ok;
}

// Render every tree variant from kViews directions into an atlas (albedo + tree-space normal)
void Foliage::bakeImpostors() {
    if (!prog_.treeBake) return;
    const int W = kViews * kTile, H = kMeshes * kTile;
    auto makeTex = [&](GLuint& t) {
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 5); // keep tiles from bleeding into each other
    };
    makeTex(impAlbedo_);
    makeTex(impNormal_);
    GLuint fbo, depth;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, impAlbedo_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, impNormal_, 0);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, H);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    GLenum bufs[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, bufs);
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    glViewport(0, 0, W, H);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    GLuint p = prog_.treeBake;
    glUseProgram(p);
    auto U = [&](const char* n) { return glGetUniformLocation(p, n); };
    glUniform1f(U("time"), 0.0f);
    glUniform1f(U("windStrength"), 0.0f);
    glUniform2f(U("windDir"), 1.0f, 0.0f);
    glUniform1i(U("foliageTex"), 7);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
    glActiveTexture(GL_TEXTURE0);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 60), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    for (int m = 0; m < kMeshes; ++m) {
        const Mesh& mesh = meshes_[m];
        float S = mesh.tile;
        glm::mat4 proj = glm::ortho(-S * 0.5f, S * 0.5f, -0.5f, S - 0.5f, 1.0f, 120.0f);
        glm::mat4 VP = proj * view;
        glUniformMatrix4fv(U("viewProj"), 1, GL_FALSE, glm::value_ptr(VP));
        for (int k = 0; k < kViews; ++k) {
            // View k looks at the tree from tree-space azimuth theta_k: rotate the tree by -theta_k
            float theta = k * glm::two_pi<float>() / kViews;
            float inst[8] = { 0, 0, 0, -theta, 1.0f, 0.0f, 1.0f, 0.0f };
            glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
            glBufferData(GL_ARRAY_BUFFER, sizeof(inst), inst, GL_STREAM_DRAW);
            glUniform1f(U("bakeYaw"), -theta);
            glViewport(k * kTile, m * kTile, kTile, kTile);
            glBindVertexArray(mesh.vao);
            glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
            glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
            glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
            glDrawElementsInstanced(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, 0, 1);
        }
    }
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteRenderbuffers(1, &depth);
    glDeleteFramebuffers(1, &fbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glBindTexture(GL_TEXTURE_2D, impAlbedo_); glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, impNormal_); glGenerateMipmap(GL_TEXTURE_2D);
}

// ---------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------
void Foliage::generate(const Terrain& terrain, const FoliageParams& params, int seed) {
    trees_.clear();
    dots_.clear();
    const TerrainParams& P = terrain.params();
    const float half = terrain.getHalfExtent();
    const float waterY = terrain.getWaterY();
    const float H = terrain.getHeightScale();
    const float beachTop = waterY + P.beachWidth * H + 1.5f;
    const float treeLine = std::min(P.snowLine * H - 10.0f, waterY + (H - waterY) * 0.8f);
    std::mt19937 rng((unsigned)seed * 7919u + 3u);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);

    if (params.treesEnabled) {
        const float spacing = 6.5f;
        float offs = (float)(seed % 1000) * 0.137f;
        for (float z = -half + spacing; z < half - spacing; z += spacing)
            for (float x = -half + spacing; x < half - spacing; x += spacing) {
                float px = x + (U(rng) - 0.5f) * spacing * 0.9f, pz = z + (U(rng) - 0.5f) * spacing * 0.9f;
                float h = terrain.getHeightAt(px, pz);
                if (clearing_.z > 0 && glm::length(glm::vec2(px - clearing_.x, pz - clearing_.y)) < clearing_.z) continue;
                if (h < beachTop || h > treeLine) continue;
                float dx = terrain.getHeightAt(px + 1.5f, pz) - terrain.getHeightAt(px - 1.5f, pz);
                float dz = terrain.getHeightAt(px, pz + 1.5f) - terrain.getHeightAt(px, pz - 1.5f);
                float slope = 1.0f - 1.0f / std::sqrt(1.0f + (dx * dx + dz * dz) / 9.0f);
                if (slope > 0.3f) continue;
                // Forests grow in patches; a few lone trees dot the open meadows
                float forest = fbm2(px * 0.006f + offs, pz * 0.006f - offs);
                float chance = params.treeDensity * glm::smoothstep(0.45f, 0.62f, forest) * 0.9f + 0.015f * params.treeDensity;
                chance *= 1.0f - glm::smoothstep(0.12f, 0.3f, slope);
                if (U(rng) > chance) continue;
                // Species: firs up high, acacias near the coast, broadleaf in between
                float alt = (h - waterY) / std::max(treeLine - waterY, 1.0f);
                float r = U(rng);
                int species = 1;
                if (r < glm::smoothstep(0.35f, 0.65f, alt)) species = 0;
                else if (alt < 0.2f && U(rng) < 0.55f) species = 2;
                Tree t;
                t.mesh = species * kVariants + (int)(U(rng) * kVariants) % kVariants;
                t.scale = 0.75f + U(rng) * 0.5f;
                t.pos = glm::vec3(px, h, pz);
                t.yaw = U(rng) * 6.2832f;
                t.phase = U(rng) * 6.2832f;
                t.tint = 0.85f + U(rng) * 0.3f;
                trees_.push_back(t);
                dots_.push_back(glm::vec3(px, pz, meshes_[t.mesh].radius * t.scale * 0.7f));
            }
    }

    // Trees planted by the village (plaza, gardens)
    for (const glm::vec4& p : planted_) {
        Tree t;
        int species = glm::clamp((int)p.w, 0, kSpecies - 1);
        t.mesh = species * kVariants + (int)(U(rng) * kVariants) % kVariants;
        t.scale = 0.7f + U(rng) * 0.25f;
        t.pos = glm::vec3(p);
        t.yaw = U(rng) * 6.2832f;
        t.phase = U(rng) * 6.2832f;
        t.tint = 0.95f + U(rng) * 0.15f;
        trees_.push_back(t);
        dots_.push_back(glm::vec3(t.pos.x, t.pos.z, meshes_[t.mesh].radius * t.scale * 0.7f));
    }

    // Collision grid
    gridHalf_ = half;
    gridN_ = std::max(1, (int)std::ceil(2.0f * half / cellSize_));
    grid_.assign((size_t)gridN_ * gridN_, {});
    for (int i = 0; i < (int)trees_.size(); ++i) {
        int cx = glm::clamp((int)((trees_[i].pos.x + half) / cellSize_), 0, gridN_ - 1);
        int cz = glm::clamp((int)((trees_[i].pos.z + half) / cellSize_), 0, gridN_ - 1);
        grid_[(size_t)cz * gridN_ + cx].push_back(i);
    }

    // Canopy shade (ground AO under crowns) and crown-top heights (for sun shadows), 1024^2 over the island
    const int R = 1024;
    std::vector<float> shade((size_t)R * R, 0.0f), crown((size_t)R * R, -1000.0f);
    for (const auto& t : trees_) {
        const Mesh& m = meshes_[t.mesh];
        int species = t.mesh / kVariants;
        float rad = m.radius * t.scale * 1.1f, Ht = m.height * t.scale;
        float cx = (t.pos.x / (2 * half) + 0.5f) * R, cz = (t.pos.z / (2 * half) + 0.5f) * R;
        float rp = rad / (2 * half) * R;
        int x0 = std::max(0, (int)(cx - rp)), x1 = std::min(R - 1, (int)(cx + rp));
        int z0 = std::max(0, (int)(cz - rp)), z1 = std::min(R - 1, (int)(cz + rp));
        for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
            float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (z + 0.5f - cz) * (z + 0.5f - cz)) / std::max(rp, 0.5f);
            if (d >= 1.0f) continue;
            float& s = shade[(size_t)z * R + x];
            s = 1.0f - (1.0f - s) * (1.0f - 0.7f * (1.0f - d * d));
            // Crown silhouette: cone for firs, dome for broadleaf, flat umbrella for acacias
            float top = species == 0 ? Ht * (1.0f - 0.85f * d)
                      : species == 1 ? Ht * (0.72f + 0.27f * std::sqrt(1.0f - d * d))
                                     : Ht * (0.92f - 0.1f * d * d);
            float& c = crown[(size_t)z * R + x];
            c = std::max(c, t.pos.y + top);
        }
    }
    auto makeTex = [&](GLuint& tex, GLenum internal, GLenum type, const void* data) {
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, internal, R, R, 0, GL_RED, type, data);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    std::vector<unsigned char> px((size_t)R * R);
    for (size_t i = 0; i < px.size(); ++i) px[i] = (unsigned char)(glm::clamp(shade[i], 0.0f, 1.0f) * 255.0f);
    makeTex(canopyTex_, GL_R8, GL_UNSIGNED_BYTE, px.data());
    makeTex(canopyHeightTex_, GL_R16F, GL_FLOAT, crown.data());
}

void Foliage::resolveCollision(glm::vec3& pos, float playerRadius) const {
    if (grid_.empty()) return;
    int cx = (int)((pos.x + gridHalf_) / cellSize_), cz = (int)((pos.z + gridHalf_) / cellSize_);
    for (int z = cz - 1; z <= cz + 1; ++z) for (int x = cx - 1; x <= cx + 1; ++x) {
        if (x < 0 || z < 0 || x >= gridN_ || z >= gridN_) continue;
        for (int i : grid_[(size_t)z * gridN_ + x]) {
            const Tree& t = trees_[i];
            float r = trunkRadius_[t.mesh / kVariants] * t.scale + playerRadius;
            glm::vec2 d(pos.x - t.pos.x, pos.z - t.pos.z);
            float l = glm::length(d);
            if (l < r && pos.y < t.pos.y + meshes_[t.mesh].height * t.scale * 0.45f + 1.7f) {
                glm::vec2 push = l > 1e-4f ? d / l : glm::vec2(1, 0);
                pos.x = t.pos.x + push.x * r;
                pos.z = t.pos.z + push.y * r;
            }
        }
    }
}

void Foliage::bindCanopyShade(GLuint prog, int unit, float terrainHalf) const {
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "canopyShade"), unit);
    glUniform1i(glGetUniformLocation(prog, "hasCanopyShade"), canopyTex_ && !trees_.empty() ? 1 : 0);
    glUniform1f(glGetUniformLocation(prog, "canopyHalf"), terrainHalf);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, canopyTex_);
    glActiveTexture(GL_TEXTURE0);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void Foliage::draw(const Terrain& terrain, const FoliageParams& params, const glm::mat4& view,
                   const glm::mat4& proj, const glm::vec3& cameraPos, float time) {
    drawnMesh_ = drawnImpostors_ = 0;
    if (terrain.minimapTexture() == 0) return; // no procedural terrain (flat mode)
    glm::mat4 VP = proj * view;
    glDisable(GL_CULL_FACE);
    if (params.treesEnabled && !trees_.empty()) drawTrees(terrain, params, VP, cameraPos, time);
    if (params.grassEnabled) drawGrass(terrain, params, VP, cameraPos, time);
}

static void setWind(GLuint prog, const Terrain& terrain, const FoliageParams& params, const glm::mat4& VP) {
    auto U = [&](const char* n) { return glGetUniformLocation(prog, n); };
    float wa = glm::radians(terrain.params().windAngle);
    glUniformMatrix4fv(U("viewProj"), 1, GL_FALSE, glm::value_ptr(VP));
    glUniform2f(U("windDir"), std::cos(wa), std::sin(wa));
    glUniform1f(U("windStrength"), params.windStrength);
}

static void extractPlanes(const glm::mat4& VP, glm::vec4 planes[5]) {
    glm::vec4 rows[4];
    for (int i = 0; i < 4; ++i) rows[i] = glm::vec4(VP[0][i], VP[1][i], VP[2][i], VP[3][i]);
    planes[0] = rows[3] + rows[0]; planes[1] = rows[3] - rows[0];
    planes[2] = rows[3] + rows[1]; planes[3] = rows[3] - rows[1];
    planes[4] = rows[3] + rows[2];
    for (int i = 0; i < 5; ++i) planes[i] /= glm::length(glm::vec3(planes[i]));
}

void Foliage::drawTrees(const Terrain& terrain, const FoliageParams& params, const glm::mat4& VP,
                        const glm::vec3& cameraPos, float time) {
    glm::vec4 planes[5];
    extractPlanes(VP, planes);

    static std::vector<float> meshData[kMeshes];
    static std::vector<float> impData;
    for (auto& d : meshData) d.clear();
    impData.clear();
    const float maxD2 = params.treeDistance * params.treeDistance;
    const float lod = params.treeDetailDistance, band = 20.0f;
    for (const Tree& t : trees_) {
        const Mesh& m = meshes_[t.mesh];
        glm::vec3 c = t.pos + glm::vec3(0, m.height * t.scale * 0.5f, 0);
        float r = m.tile * 0.75f * t.scale;
        glm::vec3 d = c - cameraPos;
        float d2 = glm::dot(d, d);
        if (d2 > maxD2) continue;
        bool inside = true;
        for (auto& p : planes) if (glm::dot(glm::vec3(p), c) + p.w < -r) { inside = false; break; }
        if (!inside) continue;
        // 0 = full mesh, 1 = billboard; dithered cross-fade in between
        float fade = glm::clamp((std::sqrt(d2) - (lod - band * 0.5f)) / band, 0.0f, 1.0f);
        if (fade < 1.0f) meshData[t.mesh].insert(meshData[t.mesh].end(), { t.pos.x, t.pos.y, t.pos.z, t.yaw, t.scale, t.phase, t.tint, fade });
        if (fade > 0.0f) impData.insert(impData.end(), { t.pos.x, t.pos.y, t.pos.z, t.yaw, t.scale, t.tint, fade, (float)t.mesh });
    }

    // Full meshes
    std::vector<float> all;
    size_t offsets[kMeshes];
    for (int i = 0; i < kMeshes; ++i) { offsets[i] = all.size(); all.insert(all.end(), meshData[i].begin(), meshData[i].end()); }
    if (!all.empty()) {
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
        glBufferData(GL_ARRAY_BUFFER, all.size() * sizeof(float), all.data(), GL_STREAM_DRAW);
        glUseProgram(prog_.tree);
        setWind(prog_.tree, terrain, params, VP);
        glUniform1i(glGetUniformLocation(prog_.tree, "foliageTex"), 7);
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texArray_);
        glActiveTexture(GL_TEXTURE0);
        for (int i = 0; i < kMeshes; ++i) {
            GLsizei n = (GLsizei)(meshData[i].size() / 8);
            if (!n) continue;
            drawnMesh_ += n;
            glBindVertexArray(meshes_[i].vao);
            glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
            glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(offsets[i] * sizeof(float)));
            glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)((offsets[i] + 4) * sizeof(float)));
            glDrawElementsInstanced(GL_TRIANGLES, meshes_[i].count, GL_UNSIGNED_INT, 0, n);
        }
    }

    // Billboards
    if (!impData.empty() && prog_.impostor) {
        drawnImpostors_ = (int)(impData.size() / 8);
        GLuint p = prog_.impostor;
        glUseProgram(p);
        setWind(p, terrain, params, VP);
        auto U = [&](const char* n) { return glGetUniformLocation(p, n); };
        float tiles[kMeshes];
        for (int i = 0; i < kMeshes; ++i) tiles[i] = meshes_[i].tile;
        glUniform1fv(U("tileSize"), kMeshes, tiles);
        glUniform1i(U("views"), kViews);
        glUniform1i(U("rows"), kMeshes);
        glUniform1f(U("atlasTexel"), 1.0f / kTile);
        glUniform1i(U("impAlbedo"), 11);
        glUniform1i(U("impNormal"), 12);
        glActiveTexture(GL_TEXTURE11); glBindTexture(GL_TEXTURE_2D, impAlbedo_);
        glActiveTexture(GL_TEXTURE12); glBindTexture(GL_TEXTURE_2D, impNormal_);
        glActiveTexture(GL_TEXTURE0);
        glBindBuffer(GL_ARRAY_BUFFER, impInstVBO_);
        glBufferData(GL_ARRAY_BUFFER, impData.size() * sizeof(float), impData.data(), GL_STREAM_DRAW);
        glBindVertexArray(impVAO_);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, drawnImpostors_);
    }
    glBindVertexArray(0);
    (void)time;
}

// Grass: clumps of blades on a world-aligned grid, drawn per visible tile.
// Near ring: dense clumps of 6 thin blades; far ring: sparser clumps of 3 wider blades.
void Foliage::drawGrass(const Terrain& terrain, const FoliageParams& params, const glm::mat4& VP,
                        const glm::vec3& cameraPos, float time) {
    const TerrainParams& P = terrain.params();
    const float H = terrain.getHeightScale();
    GLuint p = prog_.grass;
    glUseProgram(p);
    setWind(p, terrain, params, VP);
    auto U = [&](const char* nm) { return glGetUniformLocation(p, nm); };
    glUniform1f(U("radius"), params.grassRadius);
    glUniform1f(U("bladeHeight"), params.grassHeight);
    glUniform1f(U("terrainHalf"), terrain.getHalfExtent());
    glUniform1f(U("waterY"), terrain.getWaterY());
    glUniform1f(U("grassBottom"), terrain.getWaterY() + P.beachWidth * H + 0.6f);
    glUniform1f(U("rockSlope"), P.rockSlope);
    glUniform1f(U("snowLine"), P.snowLine * H);
    glUniform1f(U("texScale"), 1.0f / std::max(P.textureScale, 0.05f));
    glUniform3fv(U("grassTint"), 1, &P.grassTint.x);
    glUniform1i(U("flowers"), params.flowers ? 1 : 0);
    glUniform1i(U("heightTex"), 2);
    glUniform1i(U("matAlbedo"), 4);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, terrain.heightTexture());
    glActiveTexture(GL_TEXTURE0);
    glm::vec4 planes[5];
    extractPlanes(VP, planes);
    const float dens = std::max(params.grassDensity, 0.2f);
    struct Ring { float spacing, inner, outer, tile, width; int blades, verts; GLuint vao; };
    float nearR = std::min(12.0f, params.grassRadius);
    Ring rings[2] = { { 0.24f / std::sqrt(dens), 0.0f, nearR, 6.0f, 1.0f, kBladesPerClump, 7, grassVAO_ },
                      { 0.5f / std::sqrt(dens), nearR, params.grassRadius, 14.0f, 1.35f, 3, 5, grassFarVAO_ } };
    for (const Ring& ring : rings) {
        if (ring.outer <= ring.inner) continue;
        glBindVertexArray(ring.vao);
        glUniform1i(U("bladeVerts"), ring.verts);
        int cells = std::max(1, (int)std::round(ring.tile / ring.spacing));
        float tile = cells * ring.spacing;
        glUniform1f(U("spacing"), ring.spacing);
        glUniform1i(U("cells"), cells);
        glUniform1f(U("innerRadius"), ring.inner);
        glUniform1f(U("ringRadius"), ring.outer);
        glUniform1f(U("widthScale"), ring.width);
        glUniform1i(U("activeBlades"), ring.blades);
        int t0x = (int)std::floor((cameraPos.x - ring.outer) / tile), t1x = (int)std::floor((cameraPos.x + ring.outer) / tile);
        int t0z = (int)std::floor((cameraPos.z - ring.outer) / tile), t1z = (int)std::floor((cameraPos.z + ring.outer) / tile);
        for (int tz = t0z; tz <= t1z; ++tz) for (int tx = t0x; tx <= t1x; ++tx) {
            glm::vec2 mn(tx * tile, tz * tile), mx = mn + glm::vec2(tile);
            // Skip tiles outside the ring
            glm::vec2 cam(cameraPos.x, cameraPos.z);
            float dNear = glm::length(glm::clamp(cam, mn, mx) - cam);
            float dFar = glm::length(glm::max(glm::abs(mn - cam), glm::abs(mx - cam)));
            if (dNear > ring.outer || dFar < ring.inner) continue;
            // Frustum test with the terrain height range of the tile
            float hc = terrain.getHeightAt((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
            float h0 = std::min({ hc, terrain.getHeightAt(mn.x, mn.y), terrain.getHeightAt(mx.x, mx.y),
                                  terrain.getHeightAt(mn.x, mx.y), terrain.getHeightAt(mx.x, mn.y) }) - 2.0f;
            float h1 = std::max({ hc, terrain.getHeightAt(mn.x, mn.y), terrain.getHeightAt(mx.x, mx.y),
                                  terrain.getHeightAt(mn.x, mx.y), terrain.getHeightAt(mx.x, mn.y) }) + 3.0f;
            if (h1 < terrain.getWaterY()) continue;
            glm::vec3 bmin(mn.x, h0, mn.y), bmax(mx.x, h1, mx.y);
            bool inside = true;
            for (auto& pl : planes) {
                glm::vec3 pv(pl.x > 0 ? bmax.x : bmin.x, pl.y > 0 ? bmax.y : bmin.y, pl.z > 0 ? bmax.z : bmin.z);
                if (glm::dot(glm::vec3(pl), pv) + pl.w < 0.0f) { inside = false; break; }
            }
            if (!inside) continue;
            glUniform2f(U("tileOrigin"), mn.x, mn.y);
            glDrawElementsInstanced(GL_TRIANGLES, ring.blades * (ring.verts - 2) * 3, GL_UNSIGNED_INT, 0, cells * cells);
        }
    }
    glBindVertexArray(0);
    (void)time;
}
