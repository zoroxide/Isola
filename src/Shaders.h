#pragma once

#include <string>
#include <unordered_map>
#include <GL/glew.h>

class ShaderManager {
public:
    ShaderManager() = default;
    ~ShaderManager();

    GLuint loadProgram(const std::string& name, const char* vsPath, const char* fsPath);
    GLuint get(const std::string& name) const;

    // Lines such as "#define QUALITY 1\n" inserted after #version in every shader.
    // Call reloadAll() afterwards: programs get new ids (fetch them again with get()).
    void setDefines(const std::string& defines) { defines_ = defines; }
    const std::string& defines() const { return defines_; }
    // Recompile every program; a program that fails keeps its previous version
    void reloadAll();
    // Every compile / link message so far (written to the GPU report)
    const std::string& log() const { return log_; }

private:
    GLuint compileShaderFromFile(const char* path, GLenum type);
    GLuint createProgram(const char* vsPath, const char* fsPath);

    struct Entry { GLuint id = 0; std::string vs, fs; };
    std::unordered_map<std::string, Entry> programs_;
    std::string defines_, log_;
};
