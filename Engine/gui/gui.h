#pragma once

#include <string>

struct GLFWwindow;

class Engine;

// Minimal ImGui wrapper for the project. The implementation assumes
// ImGui + backends (imgui_impl_glfw.h/imgui_impl_opengl3.h) are available
// in the build. The GUI class is responsible for initializing ImGui with
// the GLFW window and drawing a simple control panel.
class GUI {
public:
    GUI(Engine* engine);
    ~GUI();

    // Initialize ImGui using the provided GLFWwindow. Call after the
    // OpenGL context and window are created.
    bool init(GLFWwindow* window);

    // Render the GUI for one frame. Should be called every frame before
    // swap buffers.
    void render();
    // A centred message drawn on top of the current frame (used while benchmarking the GPU / loading a map)
    void renderOverlayMessage(const std::string& text,
                              const std::string& detail = "This runs once per GPU / driver (saved in graphics.cfg).");

private:
    void drawTerrainPanel();
    void drawSkyPanel();
    void drawMinimap();
    void drawFoliagePanel();
    void drawGraphicsPanel();
    void drawVillagePanel();
    void drawMapPanel();
    Engine* engine_;
    GLFWwindow* window_;
    bool initialized_;
};
