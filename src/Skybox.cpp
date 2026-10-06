#include "Skybox.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include "libs/stb_image.h"

Skybox::~Skybox() {
    if (skyVBO_) glDeleteBuffers(1, &skyVBO_);
    if (skyVAO_) glDeleteVertexArrays(1, &skyVAO_);
    if (cubemap_) glDeleteTextures(1, &cubemap_);
}

bool Skybox::setCubemap(GLuint texID) {
    cubemap_ = texID; return true;
}

void Skybox::setShader(GLuint prog) { skyShader_ = prog; }

void Skybox::initFullscreenTriangle() {
    float skyVerts[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f
    };
    glGenVertexArrays(1, &skyVAO_);
    glGenBuffers(1, &skyVBO_);
    glBindVertexArray(skyVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, skyVBO_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyVerts), skyVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

glm::mat3 Skybox::rotation() const {
    return glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(rotationDeg), glm::vec3(0, 1, 0)));
}

glm::vec3 Skybox::lightDirection() const {
    if (sunOverride) return glm::normalize(overrideLightDir);
    if (!cubemap_ || !isHDR_) return glm::normalize(glm::vec3(-0.2f, -1.0f, -0.3f));
    // rotation() maps world -> panorama, so its transpose maps panorama -> world
    glm::vec3 sunWorld = glm::transpose(rotation()) * analysis_.sunDir;
    // Keep the light slightly above the horizon so terrain never goes fully black
    sunWorld.y = std::max(sunWorld.y, 0.05f);
    return -glm::normalize(sunWorld);
}

glm::vec3 Skybox::lightColor() const {
    if (sunOverride) return overrideLightColor;
    if (!cubemap_ || !isHDR_) return glm::vec3(1.0f, 0.98f, 0.9f);
    return analysis_.sunColor * analysis_.sunIntensity;
}

void Skybox::bindForLighting(GLuint prog, int unit) const {
    auto U = [&](const char* n) { return glGetUniformLocation(prog, n); };
    glUniform1i(U("skyTex"), unit);
    glUniform1i(U("hasSky"), cubemap_ ? 1 : 0);
    glm::mat3 R = rotation();
    glUniformMatrix3fv(U("skyRot"), 1, GL_FALSE, glm::value_ptr(R));
    glUniform1f(U("skyExposure"), exposure);
    glUniform1i(U("skyHDR"), isHDR_ ? 1 : 0);
    glUniform1f(U("skyMaxLod"), (float)(mipLevels_ - 1));
    if (cubemap_) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap_);
        glActiveTexture(GL_TEXTURE0);
    }
}

void Skybox::draw(const glm::mat4& invView, const glm::mat4& invProj, bool hasSkybox, float time, bool cloudEnabled, float cloudSpeed, float cloudScale, float cloudOpacity) {
    auto U = [&](const char* n) { return glGetUniformLocation(skyShader_, n); };
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glUseProgram(skyShader_);
    glUniformMatrix4fv(U("invProj"), 1, GL_FALSE, glm::value_ptr(invProj));
    glUniformMatrix4fv(U("invView"), 1, GL_FALSE, glm::value_ptr(invView));
    glUniform1i(U("skybox"), 1);
    glUniform1i(U("hasSkybox"), (hasSkybox && cubemap_) ? 1 : 0);
    glUniform1f(U("time"), time);
    glUniform1i(U("cloudEnabled"), cloudEnabled ? 1 : 0);
    glUniform1f(U("cloudSpeed"), cloudSpeed);
    glUniform1f(U("cloudScale"), cloudScale);
    glUniform1f(U("cloudOpacity"), cloudOpacity);
    glm::mat3 R = rotation();
    glUniformMatrix3fv(U("skyRot"), 1, GL_FALSE, glm::value_ptr(R));
    glUniform1f(U("exposure"), exposure);
    glUniform1i(U("skyHDR"), isHDR_ ? 1 : 0);
    glUniform1f(U("blur"), blur);
    glUniform1i(U("horizonFill"), horizonFill ? 1 : 0);
    glUniform3fv(U("fogColor"), 1, glm::value_ptr(fogColor));
    glUniform1f(U("maxLod"), (float)(mipLevels_ - 1));
    glm::vec3 sun = -lightDirection();
    glUniform3fv(U("sunDir"), 1, glm::value_ptr(sun));
    glm::vec3 sunCol = lightColor();
    glUniform3fv(U("sunColor"), 1, glm::value_ptr(sunCol));
    if (cubemap_) { glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_CUBE_MAP, cubemap_); glActiveTexture(GL_TEXTURE0); }
    glBindVertexArray(skyVAO_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}

// ---------------------------------------------------------------------------
// CPU helpers
// ---------------------------------------------------------------------------

// Direction for texel (u,v) in [0,1] of an OpenGL cube face (matches the GL cubemap convention)
static glm::vec3 cubeFaceDir(int face, float u, float v) {
    float a = 2.0f * u - 1.0f;
    float b = 2.0f * v - 1.0f;
    switch (face) {
        case 0: return glm::normalize(glm::vec3(1, -b, -a));   // +X
        case 1: return glm::normalize(glm::vec3(-1, -b, a));   // -X
        case 2: return glm::normalize(glm::vec3(a, 1, b));     // +Y
        case 3: return glm::normalize(glm::vec3(a, -1, -b));   // -Y
        case 4: return glm::normalize(glm::vec3(a, -b, 1));    // +Z
        default:return glm::normalize(glm::vec3(-a, -b, -1));  // -Z
    }
}

// Equirectangular mapping. Image row 0 is the zenith, so v grows *downwards*.
static glm::vec2 dirToEquirect(const glm::vec3& d) {
    float lon = std::atan2(d.z, d.x);
    float lat = std::asin(glm::clamp(d.y, -1.0f, 1.0f));
    return { (lon + glm::pi<float>()) / glm::two_pi<float>(),
             0.5f - lat / glm::pi<float>() };
}

static glm::vec3 equirectToDir(float u, float v) {
    float lon = u * glm::two_pi<float>() - glm::pi<float>();
    float lat = (0.5f - v) * glm::pi<float>();
    return { std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon) };
}

void Skybox::equirectToFaces(const float* img, int w, int h, int faceSize, std::vector<float> faces[6]) {
    // Bilinear sample with horizontal wrap-around (no seam at the 0/360 degree line)
    auto sample = [&](float u, float v) {
        float x = u * w - 0.5f, y = glm::clamp(v * h - 0.5f, 0.0f, (float)(h - 1));
        int x0 = (int)std::floor(x), y0 = (int)y;
        float fx = x - x0, fy = y - y0;
        int x1 = x0 + 1, y1 = std::min(y0 + 1, h - 1);
        x0 = ((x0 % w) + w) % w; x1 = ((x1 % w) + w) % w;
        const float* p00 = img + (y0 * w + x0) * 3; const float* p10 = img + (y0 * w + x1) * 3;
        const float* p01 = img + (y1 * w + x0) * 3; const float* p11 = img + (y1 * w + x1) * 3;
        glm::vec3 c;
        for (int k = 0; k < 3; ++k)
            c[k] = (p00[k] * (1 - fx) + p10[k] * fx) * (1 - fy) + (p01[k] * (1 - fx) + p11[k] * fx) * fy;
        return c;
    };

    std::vector<std::thread> workers;
    for (int face = 0; face < 6; ++face) {
        workers.emplace_back([&, face]() {
            std::vector<float>& out = faces[face];
            out.resize((size_t)faceSize * faceSize * 3);
            for (int y = 0; y < faceSize; ++y) for (int x = 0; x < faceSize; ++x) {
                glm::vec2 uv = dirToEquirect(cubeFaceDir(face, (x + 0.5f) / faceSize, (y + 0.5f) / faceSize));
                glm::vec3 c = sample(uv.x, uv.y);
                float* o = &out[((size_t)y * faceSize + x) * 3];
                o[0] = c.r; o[1] = c.g; o[2] = c.b;
            }
        });
    }
    for (auto& t : workers) t.join();
}

SkyAnalysis Skybox::analyzeEquirect(const float* img, int w, int h, bool hdr) {
    SkyAnalysis a;
    auto lum = [&](int x, int y) {
        const float* p = img + ((size_t)y * w + x) * 3;
        return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
    };

    // Log-average luminance of the sky (upper hemisphere), clamped so the sun doesn't dominate.
    // Rows are weighted by cos(latitude) because equirect oversamples the poles.
    double logSum = 0.0, wSum = 0.0;
    float peak = 0.0f; int px = 0, py = 0;
    const int step = std::max(1, w / 1024);
    for (int y = 0; y < h / 2; y += step) {
        float lat = (0.5f - (y + 0.5f) / h) * glm::pi<float>();
        float wt = std::cos(lat);
        for (int x = 0; x < w; x += step) {
            float L = lum(x, y);
            logSum += wt * std::log(1e-4f + std::min(L, 20.0f)); wSum += wt;
            if (L > peak) { peak = L; px = x; py = y; }
        }
    }
    float logAvg = (float)std::exp(logSum / std::max(wSum, 1e-6));

    if (!hdr) {
        // LDR images are already tone-mapped; the brightest spot is usually a cloud, not the sun
        a.autoExposure = 1.0f;
        return a;
    }
    // Target a mid-grey sky after tone mapping; clamp so night skies stay dark
    a.autoExposure = glm::clamp(0.28f / std::max(logAvg, 1e-4f), 0.05f, 4.0f);

    // Without a clear sun (overcast, mist) light comes softly from above, tinted like the sky
    a.sunDir = glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f));
    a.sunColor = glm::vec3(0.9f, 0.93f, 1.0f);
    a.sunIntensity = 0.3f;

    // How much brighter the peak is than the average sky: ~1e5 for a clear midday sun,
    // ~1e3 at sunrise, ~1e2 for moonlight, <10 for overcast. HDRIs are rarely calibrated in
    // absolute units, so this ratio is a better measure of sun strength than the peak itself.
    float contrast = peak / std::max(logAvg, 1e-3f);

    // The sun: centroid of all pixels near the peak brightness (full resolution around the peak)
    if (contrast > 12.0f) {
        glm::vec3 dirSum(0.0f), colSum(0.0f); float total = 0.0f;
        int r = std::max(4, w / 64);
        for (int y = std::max(0, py - r); y < std::min(h, py + r); ++y)
            for (int dx = -r; dx < r; ++dx) {
                int x = ((px + dx) % w + w) % w;
                float L = lum(x, y);
                if (L < peak * 0.25f) continue;
                const float* p = img + ((size_t)y * w + x) * 3;
                dirSum += L * equirectToDir((x + 0.5f) / w, (y + 0.5f) / h);
                colSum += glm::vec3(p[0], p[1], p[2]);
                total += L;
            }
        if (total > 0.0f) {
            a.hasSun = true;
            a.sunDir = glm::normalize(dirSum);
            float m = std::max({colSum.r, colSum.g, colSum.b, 1e-6f});
            glm::vec3 tint = colSum / m;
            // Real sun discs are clipped/saturated in HDRIs; keep some tint but lean towards white
            a.sunColor = glm::mix(glm::vec3(1.0f), tint, 0.6f);
            a.sunIntensity = glm::clamp((std::log10(contrast) - 1.0f) / 3.0f, 0.4f, 1.0f);
            // Low suns get weaker and warmer through more atmosphere
            float elev = glm::clamp(a.sunDir.y, 0.0f, 1.0f);
            a.sunIntensity *= glm::mix(0.7f, 1.0f, glm::smoothstep(0.0f, 0.35f, elev));
        }
    }
    return a;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

static bool endsWithCI(const std::string& s, const std::string& suf) {
    if (s.size() < suf.size()) return false;
    for (size_t i = 0; i < suf.size(); ++i)
        if (std::tolower((unsigned char)s[s.size() - suf.size() + i]) != std::tolower((unsigned char)suf[i])) return false;
    return true;
}

bool Skybox::loadFromPath(const std::string& path) {
    if (path.empty()) {
        if (cubemap_) { glDeleteTextures(1, &cubemap_); cubemap_ = 0; }
        analysis_ = SkyAnalysis();
        return true;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    stbi_set_flip_vertically_on_load(false);

    std::vector<float> faces[6];
    int faceSize = 0;
    bool hdr = false;
    SkyAnalysis analysis;

    if (fs::is_directory(path, ec)) {
        static const char* faceNames[6] = {"right", "left", "top", "bottom", "front", "back"};
        static const char* exts[] = {".hdr", ".png", ".jpg", ".jpeg", ".bmp", ".PNG", ".JPG", ".BMP"};
        for (int i = 0; i < 6; ++i) {
            std::string found;
            for (const char* ext : exts) {
                std::string cand = (fs::path(path) / (std::string(faceNames[i]) + ext)).string();
                if (fs::exists(cand, ec)) { found = cand; break; }
            }
            if (found.empty()) { std::cerr << "Skybox: missing face '" << faceNames[i] << "' in " << path << "\n"; return false; }
            int w = 0, h = 0, c = 0;
            // stbi_loadf converts LDR images to linear light (gamma 2.2) so everything is lit the same way
            float* data = stbi_loadf(found.c_str(), &w, &h, &c, 3);
            if (!data || w != h || (faceSize && w != faceSize)) {
                std::cerr << "Skybox: bad face image " << found << "\n";
                if (data) stbi_image_free(data);
                return false;
            }
            faceSize = w;
            hdr = hdr || endsWithCI(found, ".hdr");
            faces[i].assign(data, data + (size_t)w * h * 3);
            stbi_image_free(data);
        }
    } else {
        int w = 0, h = 0, c = 0;
        float* img = stbi_loadf(path.c_str(), &w, &h, &c, 3);
        if (!img) { std::cerr << "Skybox: failed to load " << path << ": " << stbi_failure_reason() << "\n"; return false; }
        hdr = stbi_is_hdr(path.c_str());
        // A quarter of the panorama width gives ~1 texel per source pixel at the horizon
        faceSize = glm::clamp(w / 4, 256, 2048);
        equirectToFaces(img, w, h, faceSize, faces);
        analysis = analyzeEquirect(img, w, h, hdr);
        stbi_image_free(img);
    }

    if (cubemap_) { glDeleteTextures(1, &cubemap_); cubemap_ = 0; }
    GLuint texID = 0;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    // R11G11B10F keeps HDR range at 4 bytes per texel
    for (int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_R11F_G11F_B10F, faceSize, faceSize, 0, GL_RGB, GL_FLOAT, faces[i].data());
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    // Filter across cube face edges (removes visible seams, especially on blurred mips)
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    cubemap_ = texID;
    isHDR_ = hdr;
    analysis_ = analysis;
    exposure = analysis.autoExposure;
    mipLevels_ = 1 + (int)std::floor(std::log2((float)faceSize));
    std::cout << "Skybox: loaded " << path << " (" << faceSize << "px faces, " << (hdr ? "HDR" : "LDR")
              << ", exposure " << exposure << (analysis.hasSun ? ", sun detected" : "") << ")\n";
    return true;
}
