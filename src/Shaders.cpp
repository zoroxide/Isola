#include "Shaders.h"
#include <fstream>
#include <sstream>
#include <iostream>

static std::string loadFile(const char* path) {
    std::ifstream in(path);
    if(!in) { std::cerr << "Failed to open " << path << std::endl; return {}; }
    std::stringstream ss; ss << in.rdbuf(); return ss.str();
}

// Expands `#include "file"` lines (paths relative to the including file) so shaders can
// share common code such as fog, sky and shadow functions.
static std::string preprocess(const std::string& path, int depth = 0) {
    std::string src = loadFile(path.c_str());
    if (src.empty() || depth > 8) return src;
    std::string dir = path.substr(0, path.find_last_of('/') + 1);
    std::stringstream in(src), out;
    std::string line;
    while (std::getline(in, line)) {
        size_t p = line.find("#include");
        size_t q0 = line.find('"'), q1 = line.rfind('"');
        if (p != std::string::npos && line.find_first_not_of(" \t") == p && q0 != std::string::npos && q1 > q0) {
            out << preprocess(dir + line.substr(q0 + 1, q1 - q0 - 1), depth + 1) << "\n";
        } else {
            out << line << "\n";
        }
    }
    return out.str();
}

ShaderManager::~ShaderManager(){
    for (auto& kv : programs_) {
        if (kv.second.id) glDeleteProgram(kv.second.id);
    }
}

GLuint ShaderManager::compileShaderFromFile(const char* path, GLenum type) {
    std::string src = preprocess(path);
    if(src.empty()) return 0;
    // Quality defines go right after the #version line
    if (!defines_.empty()) {
        size_t eol = src.find('\n');
        if (src.compare(0, 8, "#version") == 0 && eol != std::string::npos) src.insert(eol + 1, defines_);
        else src = defines_ + src;
    }
    const char* csrc = src.c_str();
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &csrc, nullptr);
    glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    char buf[4096] = "";
    glGetShaderInfoLog(sh, sizeof(buf), nullptr, buf);
    if (buf[0]) log_ += std::string(ok ? "warning " : "ERROR ") + path + ":\n" + buf + "\n";
    if(!ok) {
        std::cerr << "Shader compile error (" << path << ")\n" << buf << std::endl;
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint ShaderManager::createProgram(const char* vsPath, const char* fsPath) {
    GLuint vs = compileShaderFromFile(vsPath, GL_VERTEX_SHADER);
    GLuint fs = compileShaderFromFile(fsPath, GL_FRAGMENT_SHADER);
    if(!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return 0; }
    GLuint prog = glCreateProgram(); glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    GLint ok; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs); glDeleteShader(fs);
    if(!ok) {
        char buf[4096]; glGetProgramInfoLog(prog, 4096, nullptr, buf);
        std::cerr << "Program link error (" << vsPath << " + " << fsPath << "):\n" << buf << std::endl;
        log_ += std::string("LINK ERROR ") + vsPath + " + " + fsPath + ":\n" + buf + "\n";
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

GLuint ShaderManager::loadProgram(const std::string& name, const char* vsPath, const char* fsPath) {
    auto it = programs_.find(name);
    if (it != programs_.end() && it->second.id) return it->second.id;
    GLuint p = createProgram(vsPath, fsPath);
    if (p) programs_[name] = Entry{ p, vsPath, fsPath };
    return p;
}

void ShaderManager::reloadAll() {
    for (auto& kv : programs_) {
        GLuint p = createProgram(kv.second.vs.c_str(), kv.second.fs.c_str());
        if (!p) continue;   // keep the working version
        glDeleteProgram(kv.second.id);
        kv.second.id = p;
    }
}

GLuint ShaderManager::get(const std::string& name) const {
    auto it = programs_.find(name);
    return (it != programs_.end()) ? it->second.id : 0;
}
