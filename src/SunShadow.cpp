#include "SunShadow.h"
#include "Terrain.h"
#include <cmath>

SunShadow::~SunShadow() {
    if (tex_) glDeleteTextures(1, &tex_);
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
}

bool SunShadow::init(GLuint program, int resolution) {
    prog_ = program;
    res_ = resolution;
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, res_, res_, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGenVertexArrays(1, &vao_);
    return ok && prog_;
}

bool SunShadow::needsRebuild(const glm::vec3& sunDir) const {
    return !built_ || glm::dot(glm::normalize(sunDir), lastSun_) < 0.99995f;
}

void SunShadow::build(const Terrain& terrain, GLuint canopyHeightTex, const glm::vec3& sunDir) {
    if (!prog_ || !terrain.heightTexture()) return;
    GLint prevFbo = 0, vp[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_VIEWPORT, vp);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, res_, res_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(prog_);
    auto U = [&](const char* n) { return glGetUniformLocation(prog_, n); };
    glm::vec3 s = glm::normalize(sunDir);
    glUniform3fv(U("sunDir"), 1, &s.x);
    glUniform1f(U("halfExtent"), terrain.getHalfExtent());
    glUniform1f(U("texelWorld"), 2.0f * terrain.getHalfExtent() / res_);
    glUniform1i(U("heightTex"), 0);
    glUniform1i(U("canopyTex"), 1);
    glUniform1i(U("hasCanopy"), canopyHeightTex ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, terrain.heightTexture());
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, canopyHeightTex);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    built_ = true;
    lastSun_ = s;
}

void SunShadow::bind(GLuint prog, int unit, float halfExtent, float strength, bool enabled) const {
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "sunShadowTex"), unit);
    glUniform1i(glGetUniformLocation(prog, "hasSunShadow"), (enabled && built_) ? 1 : 0);
    glUniform1f(glGetUniformLocation(prog, "shadowHalf"), halfExtent);
    glUniform1f(glGetUniformLocation(prog, "shadowStrength"), strength);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glActiveTexture(GL_TEXTURE0);
}
