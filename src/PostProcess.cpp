#include "PostProcess.h"
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// GPU timers
// ---------------------------------------------------------------------------
void GpuTimers::newFrame() { frame_ = (frame_ + 1) % kRing; ++frameCount_; }

void GpuTimers::begin(const std::string& name) {
    int idx = -1;
    for (int i = 0; i < (int)sections_.size(); ++i) if (sections_[i].name == name) idx = i;
    if (idx < 0) {
        sections_.push_back(Section());
        idx = (int)sections_.size() - 1;
        sections_[idx].name = name;
        glGenQueries(kRing, sections_[idx].q);
        names_.push_back(name);
    }
    Section& s = sections_[idx];
    // Read the result of the query we are about to reuse (issued kRing frames ago)
    if (s.used[frame_]) {
        GLuint ready = 0;
        glGetQueryObjectuiv(s.q[frame_], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready) {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(s.q[frame_], GL_QUERY_RESULT, &ns);
            s.last = (float)(ns / 1e6);
            s.lastFrame = frameCount_;
            s.avg = s.avg * 0.9f + s.last * 0.1f;
        }
    }
    glBeginQuery(GL_TIME_ELAPSED, s.q[frame_]);
    s.used[frame_] = true;
    active_ = idx;
}

void GpuTimers::end() {
    if (active_ < 0) return;
    glEndQuery(GL_TIME_ELAPSED);
    active_ = -1;
}

float GpuTimers::ms(const std::string& name) const {
    for (const auto& s : sections_) if (s.name == name) return s.avg;
    return 0.0f;
}

float GpuTimers::lastTotalMs() const {
    // Passes that did not run recently (e.g. the shadow rebuild) don't count
    float t = 0.0f;
    for (const auto& s : sections_) if (frameCount_ - s.lastFrame <= kRing + 1) t += s.last;
    return t;
}

float GpuTimers::totalMs() const {
    float t = 0.0f;
    for (const auto& s : sections_) t += s.avg;
    return t;
}

// ---------------------------------------------------------------------------
// Post-processing
// ---------------------------------------------------------------------------
PostProcess::~PostProcess() {
    release();
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

void PostProcess::release() {
    GLuint texs[] = { sceneColor_, sceneDepth_, qTex_[0], qTex_[1], qTex_[2], eTex_[0], eTex_[1], raysTex_ };
    for (GLuint t : texs) if (t) glDeleteTextures(1, &t);
    GLuint fbos[] = { sceneFbo_, qFbo_[0], qFbo_[1], qFbo_[2], eFbo_[0], eFbo_[1], raysFbo_ };
    for (GLuint f : fbos) if (f) glDeleteFramebuffers(1, &f);
    sceneColor_ = sceneDepth_ = raysTex_ = sceneFbo_ = raysFbo_ = 0;
    for (int i = 0; i < 3; ++i) qTex_[i] = qFbo_[i] = 0;
    for (int i = 0; i < 2; ++i) eTex_[i] = eFbo_[i] = 0;
}

bool PostProcess::init(const Programs& p) {
    prog_ = p;
    glGenVertexArrays(1, &vao_);
    return p.bright && p.blur && p.rays && p.composite;
}

GLuint PostProcess::makeTex(int w, int h, GLenum internal, GLenum format, GLenum type) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void PostProcess::allocate(int w, int h) {
    release();
    W_ = w; H_ = h;
    auto fboFor = [](GLuint tex) {
        GLuint f; glGenFramebuffers(1, &f);
        glBindFramebuffer(GL_FRAMEBUFFER, f);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        return f;
    };
    // Scene: HDR-capable 32-bit colour + sampled depth (sky mask for the sun rays)
    sceneColor_ = makeTex(w, h, GL_R11F_G11F_B10F, GL_RGB, GL_FLOAT);
    sceneDepth_ = makeTex(w, h, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    sceneFbo_ = fboFor(sceneColor_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sceneDepth_, 0);
    int qw = std::max(1, w / 4), qh = std::max(1, h / 4), ew = std::max(1, w / 8), eh = std::max(1, h / 8);
    for (int i = 0; i < 3; ++i) { qTex_[i] = makeTex(qw, qh, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT); qFbo_[i] = fboFor(qTex_[i]); }
    for (int i = 0; i < 2; ++i) { eTex_[i] = makeTex(ew, eh, GL_R11F_G11F_B10F, GL_RGB, GL_FLOAT); eFbo_[i] = fboFor(eTex_[i]); }
    raysTex_ = makeTex(qw, qh, GL_R11F_G11F_B10F, GL_RGB, GL_FLOAT); raysFbo_ = fboFor(raysTex_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

glm::ivec2 PostProcess::beginScene(int windowW, int windowH, float scale) {
    if (windowW != W_ || windowH != H_) allocate(windowW, windowH);
    scale_ = scale;
    sw_ = std::max(16, (int)(W_ * scale));
    sh_ = std::max(16, (int)(H_ * scale));
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glViewport(0, 0, sw_, sh_);
    return glm::ivec2(sw_, sh_);
}

float PostProcess::updateScale(const GraphicsSettings& g, float gpuMs) {
    if (!g.autoResolution) return scale_ = glm::clamp(g.renderScale, 0.3f, 1.0f);
    if (++framesSinceStart_ < 30 || gpuMs <= 0.0f) return scale_;   // shaders still warming up
    float budget = 1000.0f / std::max(g.targetFps, 10.0f);
    // Keep the GPU frame just under budget: with VSync, missing 16.6 ms by a little halves the FPS.
    // Small steps per frame so the image doesn't pump; only part of the cost scales with resolution.
    // The timed passes don't include the small gaps between them, so aim a little lower
    if (gpuMs > budget * 0.85f) scale_ -= glm::clamp((gpuMs / budget - 0.85f) * 0.2f, 0.005f, 0.03f);
    else if (gpuMs < budget * 0.72f) scale_ += 0.005f;
    scale_ = glm::clamp(scale_, g.minScale, g.maxScale);
    return scale_;
}

void PostProcess::endScene(const GraphicsSettings& g, GLuint outputFbo, const glm::mat4& viewProj, const glm::vec3& cameraPos,
                           const glm::vec3& sunDir, const glm::vec3& sunColor, bool underwater, float time) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glBindVertexArray(vao_);
    const glm::vec2 uvScale((float)sw_ / W_, (float)sh_ / H_);
    const int qw = std::max(1, sw_ / 4), qh = std::max(1, sh_ / 4), ew = std::max(1, sw_ / 8), eh = std::max(1, sh_ / 8);
    const glm::vec2 qTexel(1.0f / std::max(1, W_ / 4), 1.0f / std::max(1, H_ / 4));
    const glm::vec2 eTexel(1.0f / std::max(1, W_ / 8), 1.0f / std::max(1, H_ / 8));

    // Sun position on screen (for the rays)
    glm::vec4 sc = viewProj * glm::vec4(cameraPos + glm::normalize(sunDir) * 1000.0f, 1.0f);
    glm::vec2 sunUV(0.5f);
    float sunVisible = 0.0f;
    if (sc.w > 0.0f) {
        sunUV = glm::vec2(sc.x, sc.y) / sc.w * 0.5f + 0.5f;
        float edge = std::max(std::fabs(sunUV.x - 0.5f), std::fabs(sunUV.y - 0.5f));
        sunVisible = 1.0f - glm::smoothstep(0.5f, 1.1f, edge);
        sunVisible *= glm::smoothstep(0.0f, 0.15f, glm::normalize(sunDir).y + 0.05f);
    }
    bool doBloom = g.bloom, doRays = g.godRays && sunVisible > 0.01f && !underwater;

    auto bindTex = [](int unit, GLuint tex) { glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, tex); };
    auto U = [](GLuint p, const char* n) { return glGetUniformLocation(p, n); };

    if (doBloom || doRays) {
        // 1. Bright pass at quarter resolution (+ sky mask for the rays in alpha)
        glBindFramebuffer(GL_FRAMEBUFFER, qFbo_[0]);
        glViewport(0, 0, qw, qh);
        glUseProgram(prog_.bright);
        bindTex(0, sceneColor_); bindTex(1, sceneDepth_);
        glUniform1i(U(prog_.bright, "scene"), 0);
        glUniform1i(U(prog_.bright, "depth"), 1);
        glUniform2fv(U(prog_.bright, "uvScale"), 1, &uvScale.x);
        glUniform2f(U(prog_.bright, "srcTexel"), 1.0f / W_, 1.0f / H_);
        glUniform1f(U(prog_.bright, "threshold"), g.bloomThreshold);
        glUniform2fv(U(prog_.bright, "sunUV"), 1, &sunUV.x);
        glUniform1f(U(prog_.bright, "aspect"), (float)sw_ / sh_);
        glUniform1i(U(prog_.bright, "needSky"), doRays ? 1 : 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    if (doBloom) {
        // 2. Separable blur at quarter res, then a wider one at eighth res
        glUseProgram(prog_.blur);
        glUniform1i(U(prog_.blur, "src"), 0);
        glUniform2fv(U(prog_.blur, "uvScale"), 1, &uvScale.x);
        auto blur = [&](GLuint srcTex, GLuint dstFbo, int w, int h, glm::vec2 texel, glm::vec2 dir) {
            glBindFramebuffer(GL_FRAMEBUFFER, dstFbo);
            glViewport(0, 0, w, h);
            bindTex(0, srcTex);
            glm::vec2 step = texel * dir;
            glUniform2fv(U(prog_.blur, "step"), 1, &step.x);
            glUniform2fv(U(prog_.blur, "texel"), 1, &texel.x);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        };
        // Downsample to 1/8 while blurring horizontally, then blur vertically (cheap and wide)
        blur(qTex_[0], eFbo_[0], ew, eh, qTexel, glm::vec2(2.0f, 0));
        blur(eTex_[0], eFbo_[1], ew, eh, eTexel, glm::vec2(0, 1.2f));
    }
    if (doRays) {
        // 3. Radial blur of the bright sky towards the sun
        glBindFramebuffer(GL_FRAMEBUFFER, raysFbo_);
        glViewport(0, 0, qw, qh);
        glUseProgram(prog_.rays);
        bindTex(0, qTex_[0]);
        glUniform1i(U(prog_.rays, "src"), 0);
        glUniform2fv(U(prog_.rays, "uvScale"), 1, &uvScale.x);
        glUniform2fv(U(prog_.rays, "sunUV"), 1, &sunUV.x);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // 4. Composite to the output at full window resolution
    glBindFramebuffer(GL_FRAMEBUFFER, outputFbo);
    glViewport(0, 0, W_, H_);
    GLuint p = prog_.composite;
    glUseProgram(p);
    bindTex(0, sceneColor_); bindTex(1, eTex_[1]); bindTex(2, raysTex_);
    glUniform1i(U(p, "scene"), 0);
    glUniform1i(U(p, "bloom"), 1);
    glUniform1i(U(p, "rays"), 2);
    glUniform2fv(U(p, "uvScale"), 1, &uvScale.x);
    glUniform2f(U(p, "sceneTexel"), 1.0f / W_, 1.0f / H_);
    glUniform2f(U(p, "sceneSize"), (float)sw_, (float)sh_);
    glUniform1i(U(p, "fxaa"), g.fxaa ? 1 : 0);
    glUniform1f(U(p, "sharpen"), g.sharpen * glm::clamp((1.0f - scale_) * 2.5f + 0.4f, 0.0f, 1.0f));
    glUniform1i(U(p, "useRays"), doRays ? 1 : 0);
    glUniform1f(U(p, "bloomIntensity"), doBloom ? g.bloomIntensity : 0.0f);
    glm::vec3 rayCol = sunColor * (doRays ? g.godRayIntensity * sunVisible : 0.0f);
    glUniform3fv(U(p, "rayColor"), 1, &rayCol.x);
    glUniform1f(U(p, "vignette"), g.vignette ? g.vignetteStrength : 0.0f);
    glUniform1f(U(p, "exposure"), g.exposure * adaptExposure_);
    glUniform1i(U(p, "rolloff"), rolloff_ ? 1 : 0);
    glUniform1f(U(p, "contrast"), g.contrast);
    glUniform1f(U(p, "saturation"), g.saturation);
    glUniform1f(U(p, "warmth"), g.warmth);
    glUniform1f(U(p, "wobble"), underwater && g.underwaterWobble ? 1.0f : 0.0f);
    glUniform1f(U(p, "time"), time);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
