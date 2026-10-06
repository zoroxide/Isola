#pragma once
#include <GL/glew.h>
#include <string>

// Procedurally generated helper textures (created once at startup)
namespace Textures {
// Tileable RGBA noise: r,g,b,a hold fractal noise at increasing frequencies. Mipmapped, repeating.
GLuint createNoise(int size = 256);
// Tileable water ripple detail: rg = normal xz (0.5 centred), b = foam noise, a = height. Mipmapped.
GLuint createWaterDetail(int size = 256);
// Load `count` same-sized images <dir>/<name><suffix>.jpg|.png into a mipmapped texture array.
// Colour maps are DXT1-compressed; normal maps keep only X/Y (RGTC2) - shaders rebuild Z.
// Returns 0 if any image is missing or the sizes differ.
// Change the anisotropic filtering of an existing texture (2D or array)
void setAnisotropy(GLuint tex, GLenum target, float amount);
GLuint loadMaterialArray(const std::string& dir, const char* const* names, int count, const char* suffix,
                         bool normals, float anisotropy);
}
