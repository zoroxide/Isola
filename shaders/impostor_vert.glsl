#version 330 core
// Distant trees: a camera-facing billboard showing the pre-rendered view closest to the camera angle
layout (location = 0) in vec2 aCorner;   // x -0.5..0.5, y 0..1
layout (location = 1) in vec4 iPosYaw;
layout (location = 2) in vec4 iParams;   // scale, tint, fade, atlas row (mesh index)

uniform mat4 viewProj;
uniform vec3 viewPos;
uniform float tileSize[9];
uniform int views;
uniform int rows;
uniform float time;
uniform vec2 windDir;
uniform float windStrength;

out vec3 FragPos;
out vec2 AtlasUV;
out float Tint;
flat out float Fade;
flat out float Yaw;

void main() {
    int row = int(iParams.w + 0.5);
    float S = tileSize[row] * iParams.x;
    vec3 base = iPosYaw.xyz;
    vec2 toCam = viewPos.xz - base.xz;
    toCam = length(toCam) > 1e-3 ? normalize(toCam) : vec2(0.0, 1.0);
    vec3 right = vec3(toCam.y, 0.0, -toCam.x);    // cross(up, toCamera)
    // Tree-space direction to the camera -> nearest baked view (see Foliage::bakeImpostors)
    float c = cos(-iPosYaw.w), s = sin(-iPosYaw.w);
    vec2 local = vec2(c * toCam.x - s * toCam.y, s * toCam.x + c * toCam.y);
    float theta = atan(-local.x, local.y);
    // wrap into 0..views-1 without % (undefined for negative numbers; AMD and Intel differ)
    float fv = float(views);
    float kf = floor(theta / (6.2831853 / fv) + 0.5);
    int k = int(kf - fv * floor(kf / fv));

    vec3 p = base + right * aCorner.x * S + vec3(0.0, aCorner.y * S - 0.5 * iParams.x, 0.0);
    // Gentle sway of the top
    p.xz += windDir * windStrength * 0.25 * aCorner.y * aCorner.y * iParams.x * sin(time * 1.1 + iPosYaw.w * 7.0);
    FragPos = p;
    AtlasUV = (vec2(float(k), float(row)) + vec2(aCorner.x + 0.5, aCorner.y)) / vec2(float(views), float(rows));
    Tint = iParams.y;
    Fade = iParams.z;
    Yaw = iPosYaw.w;
    gl_Position = viewProj * vec4(p, 1.0);
}
