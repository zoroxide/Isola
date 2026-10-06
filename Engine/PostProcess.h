#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include "Graphics.h"

// GPU timings of named passes (ring-buffered queries, so reading them never stalls the GPU)
class GpuTimers {
public:
    void begin(const std::string& name);
    void end();
    void newFrame();          // call once per frame, before the first begin()
    float ms(const std::string& name) const;
    float totalMs() const;       // smoothed (for display)
    float lastTotalMs() const;   // most recent complete frame (for the resolution controller)
    const std::vector<std::string>& names() const { return names_; }
private:
    static const int kRing = 4;
    struct Section { std::string name; GLuint q[kRing] = {}; bool used[kRing] = {}; float avg = 0.0f, last = 0.0f; int lastFrame = -1; };
    int frameCount_ = 0;
    std::vector<Section> sections_;
    std::vector<std::string> names_;
    int frame_ = 0, active_ = -1;
};

// Scene render target at an adaptive resolution + bloom, sun rays, FXAA/sharpen, colour grading
class PostProcess {
public:
    ~PostProcess();
    struct Programs { GLuint bright = 0, blur = 0, rays = 0, composite = 0; };
    bool init(const Programs& p);
    void setPrograms(const Programs& p) { prog_ = p; }

    // Bind the scene target for this frame; returns the scene viewport size
    glm::ivec2 beginScene(int windowW, int windowH, float scale);
    // Run the post chain and write the final image to `outputFbo` (0 = the window)
    void endScene(const GraphicsSettings& g, GLuint outputFbo, const glm::mat4& viewProj, const glm::vec3& cameraPos,
                  const glm::vec3& sunDir, const glm::vec3& sunColor, bool underwater, float time);

    // Dynamic resolution: adjust the scale from the measured GPU frame time
    float updateScale(const GraphicsSettings& g, float gpuFrameMs);
    int framesSinceStart_ = 0;
    float scale() const { return scale_; }
    void setScale(float s) { scale_ = s; }
    // Extra exposure on top of the graphics setting (eye adaptation indoors); rolloff compresses
    // highlights so sunlit surfaces don't clip while the exposure is raised
    void setAdaptation(float exposureScale, bool rolloff) { adaptExposure_ = exposureScale; rolloff_ = rolloff; }

private:
    void allocate(int w, int h);
    void release();
    GLuint makeTex(int w, int h, GLenum internal, GLenum format, GLenum type);
    Programs prog_;
    int W_ = 0, H_ = 0;             // allocated (window) size
    int sw_ = 0, sh_ = 0;           // current scene viewport
    float scale_ = 1.0f;
    float adaptExposure_ = 1.0f;
    bool rolloff_ = false;
    GLuint sceneFbo_ = 0, sceneColor_ = 0, sceneDepth_ = 0;
    // Quarter / eighth resolution buffers for bloom + rays
    GLuint qFbo_[3] = {}, qTex_[3] = {};   // 0: bright (a = sun ray source), 1: blur temp, 2: bloom result
    GLuint eFbo_[2] = {}, eTex_[2] = {};   // eighth res: wide bloom
    GLuint raysFbo_ = 0, raysTex_ = 0;
    GLuint vao_ = 0;
};
