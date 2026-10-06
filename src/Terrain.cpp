#include "Terrain.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <vector>
#include <random>
#include <algorithm>
#include <GLFW/glfw3.h>
#include <thread>
#include <cstdint>
#include <iostream>
#include "libs/stb_image.h"
#include "Textures.h"

Terrain::~Terrain() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (flatVBO_) glDeleteBuffers(1, &flatVBO_);
    if (flatEBO_) glDeleteBuffers(1, &flatEBO_);
    if (flatVAO_) glDeleteVertexArrays(1, &flatVAO_);
    if (flatTex_) glDeleteTextures(1, &flatTex_);
    if (heightTex_) glDeleteTextures(1, &heightTex_);
    for (auto& p : patches_) {
        if (p.vbo) glDeleteBuffers(1, &p.vbo);
        if (p.ebo) glDeleteBuffers(1, &p.ebo);
        if (p.vao) glDeleteVertexArrays(1, &p.vao);
    }
    if (chunkInstVBO_) glDeleteBuffers(1, &chunkInstVBO_);
    if (waterVBO_) glDeleteBuffers(1, &waterVBO_);
    if (waterEBO_) glDeleteBuffers(1, &waterEBO_);
    if (matAlbedo_) glDeleteTextures(1, &matAlbedo_);
    if (minimapTex_) glDeleteTextures(1, &minimapTex_);
    if (matNormal_) glDeleteTextures(1, &matNormal_);
    if (procTexture_) glDeleteTextures(1, &procTexture_);
    if (waterVAO_) glDeleteVertexArrays(1, &waterVAO_);
}

void Terrain::buildProcedural(const std::vector<float>& interleaved, const std::vector<unsigned int>& indices) {
    // Cleanup flat if switching
    isFlat_ = false;
    if (flatVAO_) { glDeleteVertexArrays(1, &flatVAO_); flatVAO_ = 0; }
    if (flatVBO_) { glDeleteBuffers(1, &flatVBO_); flatVBO_ = 0; }
    if (flatEBO_) { glDeleteBuffers(1, &flatEBO_); flatEBO_ = 0; }

    if (vao_) { glDeleteVertexArrays(1, &vao_); }
    if (vbo_) { glDeleteBuffers(1, &vbo_); }
    if (ebo_) { glDeleteBuffers(1, &ebo_); }
    glGenVertexArrays(1, &vao_); glGenBuffers(1, &vbo_); glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, interleaved.size() * sizeof(float), interleaved.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);
    indexCount_ = (unsigned int)indices.size();

    GLsizei stride = 8 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
    glBindVertexArray(0);
}

bool Terrain::buildFlat(const std::string& texturePath) {
    // Cleanup procedural if switching
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (ebo_) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }

    // Load texture externally ideally; here, assume texture loaded elsewhere and bound at draw time.
    // We'll create quad buffers only; texture ID must be set via external binding.

    struct FlatVertex { glm::vec3 pos; glm::vec3 normal; glm::vec2 uv; };
    // Use tile_ to improve texture quality by repeating across the large plane
    FlatVertex verts[4] = {
        { {-1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f,      0.0f} },
        { { 1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {tile_,     0.0f} },
        { { 1.0f, 0.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {tile_,     tile_} },
        { {-1.0f, 0.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f,      tile_} },
    };
    unsigned int indices[6] = { 0, 1, 2, 0, 2, 3 };

    if (flatVAO_) { glDeleteVertexArrays(1, &flatVAO_); }
    if (flatVBO_) { glDeleteBuffers(1, &flatVBO_); }
    if (flatEBO_) { glDeleteBuffers(1, &flatEBO_); }
    glGenVertexArrays(1, &flatVAO_);
    glGenBuffers(1, &flatVBO_);
    glGenBuffers(1, &flatEBO_);
    glBindVertexArray(flatVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, flatVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, flatEBO_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FlatVertex), (void*)offsetof(FlatVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(FlatVertex), (void*)offsetof(FlatVertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(FlatVertex), (void*)offsetof(FlatVertex, uv));
    glBindVertexArray(0);

    // Load texture for flat terrain
    if (flatTex_) { glDeleteTextures(1, &flatTex_); flatTex_ = 0; }
    int w=0,h=0,c=0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(texturePath.c_str(), &w, &h, &c, 0);
    if (data) {
        glGenTextures(1, &flatTex_);
        glBindTexture(GL_TEXTURE_2D, flatTex_);
        GLenum format = (c == 4) ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // Try anisotropic filtering if supported for better quality at grazing angles
        GLfloat maxAniso = 0.0f;
        glGetFloatv(0x84FF /*GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT*/, &maxAniso);
        if (maxAniso > 1.0f) {
            GLfloat aniso = glm::min(8.0f, maxAniso);
            glTexParameterf(GL_TEXTURE_2D, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY_EXT*/, aniso);
        }
        stbi_image_free(data);
    }
    isFlat_ = true;
    return true;
}

bool Terrain::loadProceduralTexture(const std::string& texturePath) {
    if (procTexture_) { glDeleteTextures(1, &procTexture_); procTexture_ = 0; }
    int w=0,h=0,c=0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(texturePath.c_str(), &w, &h, &c, 0);
    if (!data) return false;
    glGenTextures(1, &procTexture_);
    glBindTexture(GL_TEXTURE_2D, procTexture_);
    GLenum format = (c == 4) ? GL_RGBA : GL_RGB;
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLfloat maxAniso = 0.0f;
    glGetFloatv(0x84FF /*GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT*/, &maxAniso);
    if (maxAniso > 1.0f) {
        GLfloat aniso = glm::min(8.0f, maxAniso);
        glTexParameterf(GL_TEXTURE_2D, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY_EXT*/, aniso);
    }
    stbi_image_free(data);
    return true;
}

void Terrain::draw(GLuint shaderProgram, const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos) {
    glUseProgram(shaderProgram);
    glUniform3fv(glGetUniformLocation(shaderProgram, "viewPos"), 1, &cameraPos.x);
    if (isFlat_) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, flatTex_);
        glm::mat4 M = model * glm::scale(glm::mat4(1.0f), flatScale_);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "model"), 1, GL_FALSE, &M[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "mvp"), 1, GL_FALSE, &(proj * view * M)[0][0]);
        glBindVertexArray(flatVAO_);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    } else {
        const TerrainParams& P = params_;
        auto U = [&](const char* n) { return glGetUniformLocation(shaderProgram, n); };
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, procTexture_);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D_ARRAY, matAlbedo_);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D_ARRAY, matNormal_);
        glActiveTexture(GL_TEXTURE0);
        glUniform1i(U("matAlbedo"), 4);
        glUniform1i(U("matNormal"), 5);
        glUniform1i(U("hasMaterials"), matAlbedo_ ? 1 : 0);
        glUniform1i(U("customGrass"), procTexture_ ? 1 : 0);
        glUniform1i(U("shadeMode"), 1);
        glUniform1f(U("waterY"), P.waterEnabled ? waterY_ : -1e9f);
        glUniform1f(U("beachWidth"), P.beachWidth * heightScale_);
        glUniform1f(U("rockSlope"), P.rockSlope);
        glUniform1f(U("snowLine"), P.snowLine * heightScale_);
        glUniform1f(U("snowBlend"), std::max(P.snowBlend * heightScale_, 0.01f));
        glUniform1f(U("texScale"), 1.0f / std::max(P.textureScale, 0.05f));
        glUniform3fv(U("grassTint"), 1, &P.grassTint.x);
        glUniform3fv(U("sandTint"), 1, &P.sandTint.x);
        glUniform3fv(U("rockTint"), 1, &P.rockTint.x);
        glUniform3fv(U("snowTint"), 1, &P.snowTint.x);
        glUniform3fv(U("waterShallow"), 1, &P.waterShallow.x);
        glUniform3fv(U("waterDeep"), 1, &P.waterDeep.x);
        drawChunks(shaderProgram, view, proj, cameraPos);
    }
}

// ---------------------------------------------------------------------------
// Chunked LOD terrain
// ---------------------------------------------------------------------------
void Terrain::buildPatches() {
    for (int l = 0; l < kLods; ++l) {
        const int step = 1 << l, cells = kChunkCells / step, n = cells + 1;
        std::vector<float> v;   // local grid x, local grid z, skirt flag
        std::vector<unsigned> idx;
        for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) v.insert(v.end(), { (float)(i * step), (float)(j * step), 0.0f });
        for (int j = 0; j < cells; ++j) for (int i = 0; i < cells; ++i) {
            unsigned a = j * n + i, b = a + 1, c = a + n, d = c + 1;
            idx.insert(idx.end(), { a, c, d, a, d, b });
        }
        // Skirts: a strip hanging down from every edge hides cracks between neighbouring LODs
        auto skirt = [&](int i0, int j0, int di, int dj) {
            unsigned base = (unsigned)(v.size() / 3);
            for (int k = 0; k < n; ++k) {
                int i = i0 + di * k, j = j0 + dj * k;
                v.insert(v.end(), { (float)(i * step), (float)(j * step), 1.0f });
            }
            for (int k = 0; k < cells; ++k) {
                unsigned top0 = (j0 + dj * k) * n + (i0 + di * k), top1 = (j0 + dj * (k + 1)) * n + (i0 + di * (k + 1));
                unsigned bot0 = base + k, bot1 = base + k + 1;
                idx.insert(idx.end(), { top0, bot0, bot1, top0, bot1, top1 });
            }
        };
        skirt(0, 0, 1, 0); skirt(0, cells, 1, 0); skirt(0, 0, 0, 1); skirt(cells, 0, 0, 1);
        Patch& p = patches_[l];
        glGenVertexArrays(1, &p.vao); glGenBuffers(1, &p.vbo); glGenBuffers(1, &p.ebo);
        if (!chunkInstVBO_) glGenBuffers(1, &chunkInstVBO_);
        glBindVertexArray(p.vao);
        glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, chunkInstVBO_);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0); glEnableVertexAttribArray(1);
        glVertexAttribDivisor(1, 1);
        glBindVertexArray(0);
        p.count = (GLsizei)idx.size();
    }
}

void Terrain::buildChunks() {
    chunks_.clear();
    const int N = size_;
    for (int gz = 0; gz < N - 1; gz += kChunkCells)
        for (int gx = 0; gx < N - 1; gx += kChunkCells) {
            Chunk c{ gx, gz, 1e9f, -1e9f };
            for (int z = gz; z <= std::min(gz + kChunkCells, N - 1); ++z)
                for (int x = gx; x <= std::min(gx + kChunkCells, N - 1); ++x) {
                    float h = heights_[z * N + x];
                    c.minY = std::min(c.minY, h); c.maxY = std::max(c.maxY, h);
                }
            c.minY -= 12.0f; // room for skirts
            chunks_.push_back(c);
        }
}

void Terrain::drawChunks(GLuint prog, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos) {
    glm::mat4 VP = proj * view;
    glm::vec4 rows[4];
    for (int i = 0; i < 4; ++i) rows[i] = glm::vec4(VP[0][i], VP[1][i], VP[2][i], VP[3][i]);
    glm::vec4 planes[5] = { rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1], rows[3] - rows[1], rows[3] + rows[2] };
    const float half = getHalfExtent();
    const float chunkWorld = kChunkCells * scale_;

    struct Vis { float d; int lod; float gx, gz; };
    static std::vector<Vis> vis;
    vis.clear();
    // Deep sea floor is hidden by the (opaque) deep ocean when the camera is above water
    const float hiddenBelow = (params_.waterEnabled && cameraPos.y > waterY_ + 0.5f) ? waterY_ - 14.0f : -1e9f;
    for (const Chunk& c : chunks_) {
        if (c.maxY < hiddenBelow) continue;
        glm::vec3 mn(c.gx * scale_ - half, c.minY, c.gz * scale_ - half);
        glm::vec3 mx = mn + glm::vec3(chunkWorld, 0, chunkWorld);
        mx.y = c.maxY;
        bool inside = true;
        for (auto& p : planes) {
            glm::vec3 pv(p.x > 0 ? mx.x : mn.x, p.y > 0 ? mx.y : mn.y, p.z > 0 ? mx.z : mn.z);
            if (glm::dot(glm::vec3(p), pv) + p.w < 0.0f) { inside = false; break; }
        }
        if (!inside) continue;
        glm::vec3 closest = glm::clamp(cameraPos, mn, mx);
        float d = glm::length(closest - cameraPos);
        int lod = 0;
        float threshold = lodDistance_;
        while (lod < kLods - 1 && d > threshold) { ++lod; threshold *= 2.0f; }
        vis.push_back({ d, lod, (float)c.gx, (float)c.gz });
    }
    // Front-to-back so hidden pixels are rejected by the depth test before shading
    std::sort(vis.begin(), vis.end(), [](const Vis& a, const Vis& b) { return a.d < b.d; });
    std::vector<float> inst;
    int offsets[kLods + 1] = {};
    for (int l = 0; l < kLods; ++l) {
        offsets[l] = (int)(inst.size() / 2);
        for (const Vis& v : vis) if (v.lod == l) { inst.push_back(v.gx); inst.push_back(v.gz); }
    }
    offsets[kLods] = (int)(inst.size() / 2);
    if (inst.empty()) return;
    glBindBuffer(GL_ARRAY_BUFFER, chunkInstVBO_);
    glBufferData(GL_ARRAY_BUFFER, inst.size() * sizeof(float), inst.data(), GL_STREAM_DRAW);

    auto U = [&](const char* n) { return glGetUniformLocation(prog, n); };
    glUniformMatrix4fv(U("viewProj"), 1, GL_FALSE, &VP[0][0]);
    glUniform1i(U("gridN"), size_);
    glUniform1f(U("gridScale"), scale_);
    glUniform1f(U("halfExtent"), half);
    glUniform1i(U("heightTex"), 2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, heightTex_);
    glActiveTexture(GL_TEXTURE0);
    drawnTriangles_ = 0;
    for (int l = 0; l < kLods; ++l) {
        int n = offsets[l + 1] - offsets[l];
        if (!n) continue;
        glUniform1f(U("skirtDepth"), (1 << l) * scale_ * 2.0f + 1.0f);
        glBindVertexArray(patches_[l].vao);
        glBindBuffer(GL_ARRAY_BUFFER, chunkInstVBO_);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)(offsets[l] * 2 * sizeof(float)));
        glDrawElementsInstanced(GL_TRIANGLES, patches_[l].count, GL_UNSIGNED_INT, 0, n);
        drawnTriangles_ += patches_[l].count / 3 * n;
    }
    glBindVertexArray(0);
}


// Wave set shared with water_vert.glsl (keep both in sync): direction offset from the
// wind (radians), wavelength and amplitude relative to the main swell.
static const int   kWaves = 5;
static const float kWaveAng[kWaves] = { 0.0f, 0.55f, -0.45f, 1.05f, -1.2f };
static const float kWaveLen[kWaves] = { 1.0f, 0.61f, 0.41f, 0.27f, 0.17f };
static const float kWaveAmp[kWaves] = { 1.0f, 0.55f, 0.36f, 0.22f, 0.14f };

float Terrain::getWaterSurfaceAt(float wx, float wz, float t) const {
    if (!params_.waterEnabled) return -1e9f;
    const TerrainParams& P = params_;
    float half = getHalfExtent();
    float depth = 100.0f;
    if (std::fabs(wx) < half && std::fabs(wz) < half) depth = waterY_ - getHeightAt(wx, wz);
    float atten = 0.12f + 0.88f * glm::smoothstep(0.0f, 10.0f, depth);
    float a0 = P.waveHeight * 0.5f * atten;
    float base = glm::radians(P.windAngle);
    float y = waterY_;
    for (int i = 0; i < kWaves; ++i) {
        float ang = base + kWaveAng[i];
        float L = std::max(P.waveLength * kWaveLen[i], 0.5f);
        float k = 6.2831853f / L, c = std::sqrt(9.81f / k);
        float f = k * (std::cos(ang) * wx + std::sin(ang) * wz - c * t * P.waveSpeed);
        y += a0 * kWaveAmp[i] * std::sin(f);
    }
    return y;
}

// Camera-centred grid: dense near the middle, stretching to the horizon at the edges
void Terrain::buildWaterGrid() {
    const int N = 256;               // cells per side
    const float inner = 40.0f;       // ~0.3 m spacing at the centre
    const float R = 6000.0f;         // reaches far past the island
    auto warp = [&](float u) { float a = std::fabs(u); return (u < 0 ? -1.0f : 1.0f) * (inner * a + (R - inner) * a * a * a); };
    std::vector<float> v; v.reserve((size_t)(N + 1) * (N + 1) * 3);
    for (int z = 0; z <= N; ++z) for (int x = 0; x <= N; ++x) {
        v.push_back(warp(x * 2.0f / N - 1.0f)); v.push_back(0.0f); v.push_back(warp(z * 2.0f / N - 1.0f));
    }
    std::vector<unsigned> idx; idx.reserve((size_t)N * N * 6);
    for (int z = 0; z < N; ++z) for (int x = 0; x < N; ++x) {
        unsigned tl = z * (N + 1) + x, tr = tl + 1, bl = tl + N + 1, br = bl + 1;
        idx.insert(idx.end(), { tl, bl, br, tl, br, tr });
    }
    glGenVertexArrays(1, &waterVAO_); glGenBuffers(1, &waterVBO_); glGenBuffers(1, &waterEBO_);
    glBindVertexArray(waterVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, waterVBO_);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(float), v.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, waterEBO_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0); glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    waterIndexCount_ = (GLsizei)idx.size();
}

void Terrain::drawWater(GLuint prog, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos, float time) {
    if (!params_.waterEnabled || isFlat_ || heights_.empty()) return;
    if (!waterVAO_) buildWaterGrid();
    const TerrainParams& P = params_;
    auto U = [&](const char* n) { return glGetUniformLocation(prog, n); };
    glUseProgram(prog);
    // Snap the grid origin so vertices don't slide over the waves as the camera moves
    const float snap = 0.25f;
    glm::vec2 origin = glm::floor(glm::vec2(cameraPos.x, cameraPos.z) / snap) * snap;
    glm::mat4 VP = proj * view;
    glUniformMatrix4fv(U("viewProj"), 1, GL_FALSE, &VP[0][0]);
    glUniform2fv(U("gridOrigin"), 1, &origin.x);
    glUniform3fv(U("viewPos"), 1, &cameraPos.x);
    glUniform1f(U("time"), time);
    glUniform1f(U("waterY"), waterY_);
    glUniform1f(U("terrainHalf"), getHalfExtent());
    glUniform1f(U("waveHeight"), P.waveHeight);
    glUniform1f(U("waveLength"), P.waveLength);
    glUniform1f(U("choppiness"), P.choppiness);
    glUniform1f(U("windAngle"), glm::radians(P.windAngle));
    glUniform1f(U("waveSpeed"), P.waveSpeed);
    glUniform1f(U("waterOpacity"), P.waterOpacity);
    glUniform3fv(U("waterShallow"), 1, &P.waterShallow.x);
    glUniform3fv(U("waterDeep"), 1, &P.waterDeep.x);
    glUniform3fv(U("fogColor"), 1, &P.fogColor.x);
    glUniform1f(U("fogDensity"), P.fogDensity);
    glUniform1i(U("heightTex"), 2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, heightTex_);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindVertexArray(waterVAO_);
    glDrawElements(GL_TRIANGLES, waterIndexCount_, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Terrain::buildMinimap(int res, const std::vector<glm::vec3>* trees) {
    if (heights_.empty()) return;
    const int N = size_;
    const TerrainParams& P = params_;
    auto H = [&](int x, int z) { return heights_[glm::clamp(z, 0, N - 1) * N + glm::clamp(x, 0, N - 1)]; };
    const glm::vec3 sun = glm::normalize(glm::vec3(-1.0f, 1.3f, -1.0f)); // light from the north-west
    const float beach = P.beachWidth * heightScale_ + 0.4f;
    const float snow = P.snowLine * heightScale_;
    std::vector<unsigned char> img((size_t)res * res * 4);
    for (int y = 0; y < res; ++y) for (int x = 0; x < res; ++x) {
        int hx = x * (N - 1) / (res - 1), hz = y * (N - 1) / (res - 1);
        float h = H(hx, hz);
        int s = std::max(1, N / res);
        float dx = (H(hx + s, hz) - H(hx - s, hz)) / (2.0f * s * scale_);
        float dz = (H(hx, hz + s) - H(hx, hz - s)) / (2.0f * s * scale_);
        glm::vec3 n = glm::normalize(glm::vec3(-dx, 1.0f, -dz));
        glm::vec3 c;
        float depth = waterY_ - h;
        if (P.waterEnabled && depth > 0.0f) {
            // Ocean: light turquoise shallows fading to deep blue, with a soft relief of the sea floor
            float t = glm::smoothstep(0.0f, 14.0f, depth);
            c = glm::mix(glm::vec3(0.42f, 0.80f, 0.82f), glm::vec3(0.06f, 0.20f, 0.42f), t);
            c *= 0.9f + 0.15f * glm::dot(n, sun);
            if (depth < 0.5f) c = glm::mix(c, glm::vec3(0.92f, 0.96f, 0.96f), 0.6f); // surf line
        } else {
            float above = h - waterY_;
            float slope = 1.0f - n.y;
            glm::vec3 grassLow(0.40f, 0.58f, 0.24f), grassHigh(0.30f, 0.44f, 0.20f);
            c = glm::mix(grassLow, grassHigh, glm::clamp(above / (heightScale_ * 0.6f), 0.0f, 1.0f));
            if (P.waterEnabled && above < beach) c = glm::vec3(0.86f, 0.78f, 0.56f);
            c = glm::mix(c, glm::vec3(0.52f, 0.47f, 0.42f), glm::smoothstep(P.rockSlope, P.rockSlope + 0.12f, slope));
            c = glm::mix(c, glm::vec3(0.95f, 0.96f, 0.98f),
                         glm::smoothstep(snow, snow + 2.0f, h) * (1.0f - glm::smoothstep(0.35f, 0.7f, slope)));
            // Hill shading makes ridges and valleys readable at a glance
            c *= 0.45f + 0.75f * glm::clamp(glm::dot(n, sun), 0.0f, 1.0f);
        }
        unsigned char* p = &img[((size_t)y * res + x) * 4];
        p[0] = (unsigned char)(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f);
        p[1] = (unsigned char)(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f);
        p[2] = (unsigned char)(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f);
        p[3] = 255;
    }
    // Trees: small dark-green crowns with a lit top-left edge
    if (trees) {
        float half = getHalfExtent();
        for (const glm::vec3& t : *trees) {
            float cx = (t.x / (2 * half) + 0.5f) * (res - 1), cz = (t.y / (2 * half) + 0.5f) * (res - 1);
            float r = std::max(0.9f, t.z / (2 * half) * res);
            for (int y = (int)(cz - r - 1); y <= (int)(cz + r + 1); ++y)
                for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); ++x) {
                    if (x < 0 || y < 0 || x >= res || y >= res) continue;
                    float d = std::sqrt((x - cx) * (x - cx) + (y - cz) * (y - cz)) / r;
                    if (d > 1.0f) continue;
                    float lit = glm::clamp(0.75f - ((x - cx) + (y - cz)) / r * 0.25f, 0.4f, 1.0f);
                    unsigned char* p = &img[((size_t)y * res + x) * 4];
                    float a = 0.85f * (1.0f - d * d * 0.5f);
                    p[0] = (unsigned char)(p[0] * (1 - a) + 38 * lit * a);
                    p[1] = (unsigned char)(p[1] * (1 - a) + 78 * lit * a);
                    p[2] = (unsigned char)(p[2] * (1 - a) + 34 * lit * a);
                }
        }
    }
    if (!minimapTex_) glGenTextures(1, &minimapTex_);
    glBindTexture(GL_TEXTURE_2D, minimapTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, res, res, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

bool Terrain::loadMaterials(const std::string& dir) {
    // Albedo: RGB (DXT1 compressed). Normals: only X/Y are stored (RGTC2); the shader rebuilds Z.
    static const char* names[6] = { "grass", "grass2", "rock", "sand", "snow", "path" };
    if (matAlbedo_) glDeleteTextures(1, &matAlbedo_);
    if (matNormal_) glDeleteTextures(1, &matNormal_);
    matAlbedo_ = Textures::loadMaterialArray(dir, names, 6, "_albedo", false, 4.0f);
    matNormal_ = matAlbedo_ ? Textures::loadMaterialArray(dir, names, 6, "_normal", true, 2.0f) : 0;
    if (!matAlbedo_ || !matNormal_) {
        std::cerr << "Terrain: could not load materials from " << dir << " (using flat colours)\n";
        if (matAlbedo_) { glDeleteTextures(1, &matAlbedo_); matAlbedo_ = 0; }
        if (matNormal_) { glDeleteTextures(1, &matNormal_); matNormal_ = 0; }
        return false;
    }
    return true;
}

void Terrain::editHeights(glm::vec2 wmin, glm::vec2 wmax, const std::function<float(float, float, float)>& fn) {
    if (heights_.empty()) return;
    const int N = size_;
    const float half = getHalfExtent();
    int x0 = std::max(0, (int)std::floor((wmin.x + half) / scale_)), x1 = std::min(N - 1, (int)std::ceil((wmax.x + half) / scale_));
    int z0 = std::max(0, (int)std::floor((wmin.y + half) / scale_)), z1 = std::min(N - 1, (int)std::ceil((wmax.y + half) / scale_));
    for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
        float& h = heights_[(size_t)z * N + x];
        h = fn(x * scale_ - half, z * scale_ - half, h);
    }
    glBindTexture(GL_TEXTURE_2D, heightTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, N, N, 0, GL_RED, GL_FLOAT, heights_.data());
    buildChunks();
}

// --- Presets ---
static const char* kPresetNames[] = { "Big Island", "Archipelago", "Mountains", "Rolling Hills", "Plains", "Mesa / Canyons", "Alpine Peaks" };

const char* const* TerrainParams::presetNames(int& count) {
    count = (int)(sizeof(kPresetNames) / sizeof(kPresetNames[0]));
    return kPresetNames;
}

TerrainParams TerrainParams::preset(int id) {
    TerrainParams p; // defaults are the "Big Island" look
    auto mainland = [&]() { p.islandStrength = 0.0f; p.waterLevel = 0.28f; p.frequency = 0.0045f; };
    switch (id) {
    case 1: // Archipelago: many islands with channels between them
        p.islandRadius = 0.30f; p.coastNoise = 1.3f; p.frequency = 0.005f; p.ridgeAmount = 0.45f;
        p.waterLevel = 0.24f; p.beachWidth = 0.03f; break;
    case 2: // Mountains
        mainland(); p.ridgeAmount = 0.7f; p.heightPower = 1.6f; break;
    case 3: // Rolling hills
        mainland(); p.frequency = 0.003f; p.octaves = 5; p.ridgeAmount = 0.05f; p.warpStrength = 40.0f;
        p.heightPower = 1.1f; p.waterLevel = 0.18f; p.snowLine = 2.0f; p.rockSlope = 0.45f; break;
    case 4: // Plains
        mainland(); p.frequency = 0.0025f; p.octaves = 4; p.ridgeAmount = 0.0f; p.warpStrength = 20.0f;
        p.heightPower = 1.0f; p.waterLevel = 0.12f; p.snowLine = 3.0f; p.rockSlope = 0.6f; break;
    case 5: // Mesa / canyons
        mainland(); p.frequency = 0.004f; p.ridgeAmount = 0.2f; p.heightPower = 1.3f; p.terraceStrength = 0.9f;
        p.terraceSteps = 6; p.waterLevel = 0.05f; p.snowLine = 3.0f; p.rockSlope = 0.2f;
        p.sandTint = {1.1f, 0.8f, 0.6f}; p.rockTint = {1.35f, 0.8f, 0.6f};
        p.grassTint = {1.1f, 0.9f, 0.6f}; p.fogColor = {0.85f, 0.75f, 0.65f}; break;
    case 6: // Alpine island
        p.frequency = 0.0045f; p.ridgeAmount = 1.0f; p.heightPower = 2.0f; p.warpStrength = 90.0f;
        p.islandRadius = 0.5f; p.waterLevel = 0.12f; p.snowLine = 0.55f; p.snowBlend = 0.1f; break;
    default: break;
    }
    return p;
}

// --- Noise helpers (seeded gradient/Perlin noise) ---
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float fade5(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
static inline float smooth01(float e0, float e1, float x) {
    float t = glm::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t);
}
static inline unsigned hash2(int x, int y, unsigned seed) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16);
}
static float gradNoise(float x, float y, unsigned seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float xf = x - xi, yf = y - yi;
    auto g = [&](int ix, int iy, float dx, float dy) {
        float a = (hash2(ix, iy, seed) & 1023u) * (6.2831853f / 1024.0f);
        return std::cos(a) * dx + std::sin(a) * dy;
    };
    float u = fade5(xf), v = fade5(yf);
    float n = lerpf(lerpf(g(xi, yi, xf, yf), g(xi + 1, yi, xf - 1, yf), u),
                    lerpf(g(xi, yi + 1, xf, yf - 1), g(xi + 1, yi + 1, xf - 1, yf - 1), u), v);
    return n * 1.4142f; // roughly [-1, 1]
}

namespace {
struct HeightGen {
    const TerrainParams& p; unsigned seed; float ox, oy;
    explicit HeightGen(const TerrainParams& params) : p(params), seed((unsigned)params.seed) {
        ox = (hash2(1, 7, seed) & 0xffff) * 0.37f; oy = (hash2(9, 3, seed) & 0xffff) * 0.37f;
    }
    float fbm(float x, float y) const {
        float total = 0, amp = 1, freq = 1, norm = 0;
        for (int i = 0; i < p.octaves; ++i) {
            total += amp * gradNoise(x * freq, y * freq, seed + i * 101u); norm += amp;
            amp *= p.persistence; freq *= p.lacunarity;
        }
        return total / norm; // ~[-1,1]
    }
    // Ridged multifractal: sharp crests, with detail concentrated on the slopes
    float ridged(float x, float y) const {
        float total = 0, amp = 1, freq = 1, norm = 0, weight = 1;
        for (int i = 0; i < p.octaves; ++i) {
            float n = 1.0f - std::fabs(gradNoise(x * freq, y * freq, seed + 977u + i * 131u));
            n *= n; n *= weight;
            weight = glm::clamp(n * 2.0f, 0.0f, 1.0f);
            total += n * amp; norm += amp; amp *= p.persistence; freq *= p.lacunarity;
        }
        return total / norm; // ~[0,1]
    }
    // Raw (un-normalized) height at grid coordinate
    float raw(float gx, float gz) const {
        float x = gx + ox, y = gz + oy;
        if (p.warpStrength > 0.0f) {
            float wx = gradNoise(x * p.frequency * 2.0f + 5.2f, y * p.frequency * 2.0f + 1.3f, seed + 11u);
            float wy = gradNoise(x * p.frequency * 2.0f + 9.7f, y * p.frequency * 2.0f + 3.1f, seed + 23u);
            x += wx * p.warpStrength; y += wy * p.warpStrength;
        }
        float fx = x * p.frequency, fy = y * p.frequency;
        float base = fbm(fx, fy) * 0.5f + 0.5f;
        float ridge = ridged(fx, fy);
        // Ridges appear only in some regions so the land has both plains and ranges
        float mask = smooth01(0.30f, 0.70f, gradNoise(fx * 0.35f + 40.0f, fy * 0.35f + 40.0f, seed + 55u) * 0.5f + 0.5f);
        return lerpf(base, ridge, p.ridgeAmount * (0.35f + 0.65f * mask));
    }
};
} // namespace

// Simple particle-based hydraulic erosion (carves valleys, deposits sediment)
static void erode(std::vector<float>& h, int N, const TerrainParams& P) {
    if (P.erosionIterations <= 0) return;
    std::mt19937 rng((unsigned)P.seed * 2654435761u + 17u);
    std::uniform_real_distribution<float> U(0.0f, 1.0f);
    const float inertia = 0.05f, capacityK = 4.0f, minSlope = 0.01f, evaporate = 0.012f, gravity = 4.0f;
    const int maxLife = 40, R = 3;

    // Precompute brush (erosion is spread over a small disk to avoid pits)
    std::vector<int> bx, bz; std::vector<float> bw; float wsum = 0;
    for (int dz = -R; dz <= R; ++dz) for (int dx = -R; dx <= R; ++dx) {
        float d = std::sqrt((float)(dx * dx + dz * dz)); if (d > R) continue;
        bx.push_back(dx); bz.push_back(dz); bw.push_back(1.0f - d / R); wsum += 1.0f - d / R;
    }
    for (auto& w : bw) w /= wsum;

    auto sample = [&](float x, float z, float& gx, float& gz) {
        int xi = (int)x, zi = (int)z; float fx = x - xi, fz = z - zi;
        float h00 = h[zi * N + xi], h10 = h[zi * N + xi + 1], h01 = h[(zi + 1) * N + xi], h11 = h[(zi + 1) * N + xi + 1];
        gx = (h10 - h00) * (1 - fz) + (h11 - h01) * fz;
        gz = (h01 - h00) * (1 - fx) + (h11 - h10) * fx;
        return h00 * (1 - fx) * (1 - fz) + h10 * fx * (1 - fz) + h01 * (1 - fx) * fz + h11 * fx * fz;
    };

    for (int it = 0; it < P.erosionIterations; ++it) {
        float x = U(rng) * (N - 3) + 1, z = U(rng) * (N - 3) + 1;
        float dx = 0, dz = 0, speed = 1, water = 1, sediment = 0;
        for (int life = 0; life < maxLife; ++life) {
            int xi = (int)x, zi = (int)z;
            if (xi < 1 || zi < 1 || xi >= N - 2 || zi >= N - 2) break;
            float fx = x - xi, fz = z - zi, gx, gz;
            float hOld = sample(x, z, gx, gz);
            dx = dx * inertia - gx * (1 - inertia); dz = dz * inertia - gz * (1 - inertia);
            float len = std::sqrt(dx * dx + dz * dz);
            if (len < 1e-6f) { float a = U(rng) * 6.2831853f; dx = std::cos(a); dz = std::sin(a); len = 1; }
            dx /= len; dz /= len;
            x += dx; z += dz;
            if ((int)x < 1 || (int)z < 1 || (int)x >= N - 2 || (int)z >= N - 2) break;
            float gx2, gz2; float hNew = sample(x, z, gx2, gz2);
            float dh = hNew - hOld;
            float cap = std::max(-dh * speed * water * capacityK, minSlope);
            if (sediment > cap || dh > 0) {
                float amt = (dh > 0) ? std::min(dh, sediment) : (sediment - cap) * P.depositStrength;
                sediment -= amt;
                h[zi * N + xi] += amt * (1 - fx) * (1 - fz); h[zi * N + xi + 1] += amt * fx * (1 - fz);
                h[(zi + 1) * N + xi] += amt * (1 - fx) * fz; h[(zi + 1) * N + xi + 1] += amt * fx * fz;
            } else {
                float amt = std::min((cap - sediment) * P.erosionStrength, -dh);
                for (size_t k = 0; k < bw.size(); ++k) {
                    int cx = xi + bx[k], cz = zi + bz[k];
                    if (cx < 0 || cz < 0 || cx >= N || cz >= N) continue;
                    float take = amt * bw[k]; float& c = h[cz * N + cx];
                    float t = std::min(take, c); c -= t; sediment += t;
                }
            }
            speed = std::sqrt(std::max(0.0f, speed * speed + (-dh) * gravity));
            water *= (1 - evaporate);
        }
    }
}

// Thermal erosion: slopes steeper than the talus angle shed material downhill
static void thermal(std::vector<float>& h, int N, int passes, float talus) {
    for (int pass = 0; pass < passes; ++pass) {
        for (int z = 1; z < N - 1; ++z) for (int x = 1; x < N - 1; ++x) {
            float c = h[z * N + x]; float maxD = 0; int mi = -1;
            static const int off[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
            for (int k = 0; k < 4; ++k) {
                float d = c - h[(z + off[k][1]) * N + x + off[k][0]];
                if (d > maxD) { maxD = d; mi = k; }
            }
            if (mi >= 0 && maxD > talus) {
                float mv = (maxD - talus) * 0.4f;
                h[z * N + x] -= mv; h[(z + off[mi][1]) * N + x + off[mi][0]] += mv;
            }
        }
    }
}

// Every below-sea-level cell that the ocean (connected to the map border) can't reach would
// render as a lake. Raise those basins just above the water line and smooth them into the land.
static void fillInlandBasins(std::vector<float>& h, int N, float W) {
    std::vector<uint8_t> ocean(N * N, 0);
    std::vector<int> stack;
    auto seed = [&](int i) { if (!ocean[i] && h[i] < W) { ocean[i] = 1; stack.push_back(i); } };
    for (int i = 0; i < N; ++i) { seed(i); seed((N - 1) * N + i); seed(i * N); seed(i * N + N - 1); }
    while (!stack.empty()) {
        int i = stack.back(); stack.pop_back();
        int x = i % N, z = i / N;
        for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
            int nx = x + dx, nz = z + dz;
            if (nx >= 0 && nz >= 0 && nx < N && nz < N) seed(nz * N + nx);
        }
    }
    const float floorH = W + 0.025f; // high enough to be grass, not a sandy puddle
    std::vector<uint8_t> filled(N * N, 0);
    for (int i = 0; i < N * N; ++i)
        if (!ocean[i] && h[i] < floorH && h[i] < W + 1e-6f) { h[i] = floorH; filled[i] = 1; }
    // Soften the filled flats into the surrounding slopes
    for (int pass = 0; pass < 4; ++pass) {
        std::vector<float> src = h;
        for (int z = 1; z < N - 1; ++z) for (int x = 1; x < N - 1; ++x) {
            int i = z * N + x;
            if (!filled[i]) continue;
            float sum = 0;
            for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) sum += src[(z + dz) * N + x + dx];
            h[i] = std::max(floorH, sum / 9.0f);
        }
    }
}

void Terrain::generateProcedural(int size, float scale, float heightScale, float textureTile) {
    generateProcedural(size, scale, heightScale, textureTile, params_);
}

void Terrain::generateProcedural(int size, float scale, float heightScale, float textureTile, const TerrainParams& params) {
    size_ = std::max(size, 4); scale_ = scale; heightScale_ = heightScale; tile_ = textureTile; params_ = params;
    const int N = size_;
    const TerrainParams& P = params_;
    // 1. Raw noise (multithreaded), normalized to 0..1 so the settings behave the same at any seed
    HeightGen gen(P);
    std::vector<float> h(N * N);
    {
        unsigned nt = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < nt; ++t)
            pool.emplace_back([&, t]() {
                for (int z = (int)t; z < N; z += (int)nt) for (int x = 0; x < N; ++x) h[z * N + x] = gen.raw((float)x, (float)z);
            });
        for (auto& th : pool) th.join();
    }
    float lo = 1e9f, hi = -1e9f;
    for (float v : h) { lo = std::min(lo, v); hi = std::max(hi, v); }
    float range = std::max(hi - lo, 1e-6f);

    // 2. Shaping: redistribution curve, terraces, island
    const float W = P.waterLevel;
    for (int z = 0; z < N; ++z) for (int x = 0; x < N; ++x) {
        float v = (h[z * N + x] - lo) / range;
        v = std::pow(glm::clamp(v, 0.0f, 1.0f), std::max(P.heightPower, 0.05f));
        if (P.terraceStrength > 0.0f && P.terraceSteps > 1) {
            float s = v * P.terraceSteps, f = std::floor(s);
            float stepped = (f + smooth01(0.35f, 0.65f, s - f)) / P.terraceSteps;
            v = lerpf(v, stepped, glm::clamp(P.terraceStrength, 0.0f, 1.0f));
        }
        if (P.islandStrength > 0.0f) {
            float nx = (x / (float)(N - 1)) * 2 - 1, nz = (z / (float)(N - 1)) * 2 - 1;
            // Distance from the centre, distorted by low-frequency noise for bays and peninsulas.
            // Scaled by map size (not noise frequency) so the island keeps its shape at any resolution.
            float cf = 2.5f / N;
            float cn = gen.fbm(x * cf + 31.7f, z * cf + 17.3f) * 0.75f + gen.fbm(x * cf * 3.0f + 5.1f, z * cf * 3.0f + 9.4f) * 0.25f;
            float dd = std::sqrt(nx * nx + nz * nz) + cn * P.coastNoise;
            float land = 1.0f - smooth01(P.islandRadius, P.islandRadius + 0.35f, dd);
            land *= 1.0f - smooth01(0.86f, 0.97f, std::max(std::fabs(nx), std::fabs(nz))); // ocean all around
            // Beaches and low hills along the coast, rising to the tallest mountains inland
            float inland = smooth01(0.5f, 0.9f, land);
            float landH = W + 0.012f + v * (1.0f - W) * (0.06f + 0.94f * inland * std::sqrt(inland));
            float seaH = W - P.seaDepth + v * 0.04f;
            float isl = lerpf(seaH, landH, smooth01(0.0f, 0.65f, land));
            v = lerpf(v, isl, glm::clamp(P.islandStrength, 0.0f, 1.0f));
        }
        h[z * N + x] = v;
    }

    // 3. Erosion (in normalized units)
    thermal(h, N, P.thermalIterations, 0.6f / N * 3.0f);
    erode(h, N, P);
    if (P.waterEnabled && P.fillLakes) fillInlandBasins(h, N, W);

    // 4. To world space
    heights_.resize(N * N);
    for (int i = 0; i < N * N; ++i) heights_[i] = h[i] * heightScale_;
    waterY_ = P.waterLevel * heightScale_;

    // 5. Chunked LOD mesh: only per-chunk bounds are needed (geometry is shared patches)
    isFlat_ = false;
    if (flatVAO_) { glDeleteVertexArrays(1, &flatVAO_); flatVAO_ = 0; }
    if (flatVBO_) { glDeleteBuffers(1, &flatVBO_); flatVBO_ = 0; }
    if (flatEBO_) { glDeleteBuffers(1, &flatEBO_); flatEBO_ = 0; }
    if (!patches_[0].vao) buildPatches();
    buildChunks();

    // 6. Height texture (used by the water shader for depth / shoreline foam)
    if (!heightTex_) glGenTextures(1, &heightTex_);
    glBindTexture(GL_TEXTURE_2D, heightTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, N, N, 0, GL_RED, GL_FLOAT, heights_.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 7. Minimap for the HUD
    buildMinimap(512);
}

float Terrain::getHeightAt(float wx, float wz) const {
    if (isFlat_ || heights_.empty()) return 0.0f; // flat plane at Y=0
    const int N = size_;
    float half = (N - 1) * 0.5f * scale_;
    float x = glm::clamp((wx + half) / scale_, 0.0f, (float)(N - 1) - 0.001f);
    float z = glm::clamp((wz + half) / scale_, 0.0f, (float)(N - 1) - 0.001f);
    int xi = (int)x, zi = (int)z; float fx = x - xi, fz = z - zi;
    float h = lerpf(lerpf(heights_[zi * N + xi], heights_[zi * N + xi + 1], fx),
                    lerpf(heights_[(zi + 1) * N + xi], heights_[(zi + 1) * N + xi + 1], fx), fz);
    return h;
}
