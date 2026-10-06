#include "Textures.h"
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <iostream>
#include <glm/glm.hpp>
#include "libs/stb_image.h"

namespace {
// Periodic value noise: the lattice wraps every `period` cells so the texture tiles seamlessly
float lattice(int x, int y, int period, uint32_t seed) {
    x = ((x % period) + period) % period;
    y = ((y % period) + period) % period;
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}

float periodicNoise(float x, float y, int period, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    float u = fx * fx * fx * (fx * (fx * 6 - 15) + 10), v = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
    float a = lattice(xi, yi, period, seed), b = lattice(xi + 1, yi, period, seed);
    float c = lattice(xi, yi + 1, period, seed), d = lattice(xi + 1, yi + 1, period, seed);
    return (a + (b - a) * u) * (1 - v) + (c + (d - c) * u) * v;
}

float periodicFbm(float x, float y, int basePeriod, int octaves, uint32_t seed) {
    float sum = 0, amp = 0.5f, norm = 0;
    int period = basePeriod;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * periodicNoise(x * period, y * period, period, seed + o * 31u);
        norm += amp; amp *= 0.5f; period *= 2;
    }
    return sum / norm;
}

GLuint upload(const std::vector<unsigned char>& px, int size) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return t;
}

unsigned char toByte(float v) { v = v < 0 ? 0 : (v > 1 ? 1 : v); return (unsigned char)(v * 255.0f + 0.5f); }
} // namespace

GLuint Textures::createNoise(int size) {
    std::vector<unsigned char> px((size_t)size * size * 4);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        float u = x / (float)size, v = y / (float)size;
        unsigned char* p = &px[((size_t)y * size + x) * 4];
        // Stretch the contrast a bit: fbm values cluster around 0.5
        for (int c = 0; c < 4; ++c) {
            float n = periodicFbm(u, v, 4 << c, 4, 11u + c * 101u);
            p[c] = toByte((n - 0.5f) * 1.8f + 0.5f);
        }
    }
    return upload(px, size);
}

GLuint Textures::createWaterDetail(int size) {
    std::vector<float> h((size_t)size * size);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        float u = x / (float)size, v = y / (float)size;
        // Mix of rounded ripples; ridged component gives sharper wave crests
        float a = periodicFbm(u, v, 8, 5, 7u);
        float r = 1.0f - std::fabs(periodicFbm(u, v, 6, 4, 19u) * 2.0f - 1.0f);
        h[(size_t)y * size + x] = a * 0.6f + r * r * 0.4f;
    }

    std::vector<unsigned char> px((size_t)size * size * 4);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        auto H = [&](int xx, int yy) { return h[(size_t)((yy + size) % size) * size + (xx + size) % size]; };
        float dx = (H(x + 1, y) - H(x - 1, y)) * size * 0.05f;
        float dy = (H(x, y + 1) - H(x, y - 1)) * size * 0.05f;
        float len = std::sqrt(dx * dx + dy * dy + 1.0f);
        unsigned char* p = &px[((size_t)y * size + x) * 4];
        p[0] = toByte(-dx / len * 0.5f + 0.5f);
        p[1] = toByte(-dy / len * 0.5f + 0.5f);
        p[2] = toByte(periodicFbm(x / (float)size, y / (float)size, 16, 4, 77u) * 1.6f - 0.3f);
        p[3] = toByte(H(x, y));
    }

    return upload(px, size);
}

// Upload a texture array with a CPU-built mip chain. When `compressed` is supported the driver
// compresses on upload (4-8x less memory bandwidth, which is what limits integrated GPUs).
static void uploadArray(GLuint tex, std::vector<std::vector<unsigned char>>& layers, int W, int H, int C,
                        GLenum compressed, GLenum plain, GLenum srcFormat, float aniso) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    int levels = 1 + (int)std::floor(std::log2((float)std::max(W, H)));
    GLenum internal = compressed ? compressed : plain;
    for (int attempt = 0; attempt < 2; ++attempt) {
        while (glGetError() != GL_NO_ERROR) {}
        std::vector<std::vector<unsigned char>> cur = layers;
        int w = W, h = H;
        for (int l = 0; l < levels; ++l) {
            glTexImage3D(GL_TEXTURE_2D_ARRAY, l, internal, w, h, (GLsizei)layers.size(), 0, srcFormat, GL_UNSIGNED_BYTE, nullptr);
            for (size_t i = 0; i < cur.size(); ++i)
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, l, 0, 0, (GLint)i, w, h, 1, srcFormat, GL_UNSIGNED_BYTE, cur[i].data());
            if (w == 1 && h == 1) { levels = l + 1; break; }
            int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
            for (auto& img : cur) {   // 2x2 box filter
                std::vector<unsigned char> next((size_t)nw * nh * C);
                for (int y = 0; y < nh; ++y) for (int x = 0; x < nw; ++x) for (int c = 0; c < C; ++c) {
                    int x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1), y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
                    int sum = img[((size_t)y0 * w + x0) * C + c] + img[((size_t)y0 * w + x1) * C + c]
                            + img[((size_t)y1 * w + x0) * C + c] + img[((size_t)y1 * w + x1) * C + c];
                    next[((size_t)y * nw + x) * C + c] = (unsigned char)((sum + 2) / 4);
                }
                img.swap(next);
            }
            w = nw; h = nh;
        }
        if (glGetError() == GL_NO_ERROR || internal == plain) break;
        std::cerr << "Textures: texture compression unavailable, using uncompressed textures\n";
        internal = plain;   // retry without compression
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, levels - 1);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    GLfloat maxAniso = 0.0f;
    glGetFloatv(0x84FF /*GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT*/, &maxAniso);
    if (maxAniso > 1.0f) glTexParameterf(GL_TEXTURE_2D_ARRAY, 0x84FE, glm::min(aniso, maxAniso));
}


GLuint Textures::loadMaterialArray(const std::string& dir, const char* const* names, int count, const char* suffix,
                                   bool normals, float anisotropy) {
    std::vector<std::vector<unsigned char>> layers;
    int W = 0, H = 0;
    stbi_set_flip_vertically_on_load(false);
    for (int i = 0; i < count; ++i) {
        int w = 0, h = 0, c = 0;
        unsigned char* img = nullptr;
        for (const char* ext : { ".jpg", ".png" }) {
            std::string path = dir + "/" + names[i] + suffix + ext;
            img = stbi_load(path.c_str(), &w, &h, &c, 3);
            if (img) break;
        }
        if (!img || (i > 0 && (w != W || h != H))) {
            std::cerr << "Textures: missing or wrong-size " << dir << "/" << names[i] << suffix << "\n";
            if (img) stbi_image_free(img);
            return 0;
        }
        W = w; H = h;
        if (normals) {
            std::vector<unsigned char> rg((size_t)w * h * 2);
            for (size_t p = 0; p < (size_t)w * h; ++p) { rg[p * 2] = img[p * 3]; rg[p * 2 + 1] = img[p * 3 + 1]; }
            layers.push_back(std::move(rg));
        } else {
            layers.emplace_back(img, img + (size_t)w * h * 3);
        }
        stbi_image_free(img);
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (normals)
        uploadArray(tex, layers, W, H, 2, GL_COMPRESSED_RG_RGTC2, GL_RG8, GL_RG, anisotropy);
    else
        uploadArray(tex, layers, W, H, 3, GLEW_EXT_texture_compression_s3tc ? GL_COMPRESSED_RGB_S3TC_DXT1_EXT : 0,
                    GL_RGB8, GL_RGB, anisotropy);
    return tex;
}

void Textures::setAnisotropy(GLuint tex, GLenum target, float amount) {
    if (!tex || !GLEW_EXT_texture_filter_anisotropic) return;
    GLfloat maxAniso = 1.0f;
    glGetFloatv(0x84FF /*GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT*/, &maxAniso);
    glBindTexture(target, tex);
    glTexParameterf(target, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY_EXT*/, std::max(1.0f, std::min(amount, maxAniso)));
}
