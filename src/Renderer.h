#pragma once
#include <glm/glm.hpp>
#include <GL/glew.h>

namespace isola::detail {

class Terrain;
class Skybox;

class Renderer {
public:
    Renderer() = default;
    // terrainProg: chunked LOD terrain shader; plainProg: flat terrain; skyProg: sky
    void setPrograms(GLuint terrainProg, GLuint plainProg, GLuint skyProg) { terrainProg_ = terrainProg; plainProg_ = plainProg; skyProg_ = skyProg; }
    void setScene(Terrain* terrain, Skybox* sky) { terrain_ = terrain; sky_ = sky; }

    void drawFrame(const glm::mat4& view, const glm::mat4& proj, const glm::mat4& model,
                   const glm::mat4& invView, const glm::mat4& invProj,
                   const glm::vec3& cameraPos,
                   bool hasSkybox, float time,
                   bool cloudEnabled, float cloudSpeed, float cloudScale, float cloudOpacity);

private:
    GLuint terrainProg_ = 0;
    GLuint skyProg_ = 0;
    GLuint plainProg_ = 0;
    Terrain* terrain_ = nullptr;
    Skybox* sky_ = nullptr;
};

} // namespace isola::detail
