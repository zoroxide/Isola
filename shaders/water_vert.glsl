#version 330 core
layout (location = 0) in vec3 aPos;   // grid offset from the camera (metres), y = 0

uniform mat4 viewProj;
uniform vec2 gridOrigin;
uniform vec3 viewPos;
uniform float time;
uniform float waterY;
uniform float waveHeight;
uniform float waveLength;
uniform float choppiness;
uniform float windAngle;   // radians
uniform float waveSpeed;
uniform sampler2D heightTex;
uniform float terrainHalf;

out vec3 FragPos;
out vec3 WaveNormal;
out float Crest;           // -1 trough .. +1 crest
out float Depth;           // water depth at this point (metres)

// Keep in sync with kWave* in Terrain.cpp (the CPU uses the same sum for swimming)
const int NW = 5;
const float ANG[NW] = float[](0.0, 0.55, -0.45, 1.05, -1.2);
const float LEN[NW] = float[](1.0, 0.61, 0.41, 0.27, 0.17);
const float AMP[NW] = float[](1.0, 0.55, 0.36, 0.22, 0.14);

void main() {
    vec2 p = gridOrigin + aPos.xz;
    float dist = length(p - viewPos.xz);

    vec2 huv = p / (2.0 * terrainHalf) + 0.5;
    float depth = 100.0;
    if (all(greaterThan(huv, vec2(0.0))) && all(lessThan(huv, vec2(1.0))))
        depth = waterY - textureLod(heightTex, huv, 0.0).r;
    // Waves calm down in the shallows
    float atten = 0.12 + 0.88 * smoothstep(0.0, 10.0, depth);
    float a0 = waveHeight * 0.5 * atten;

    vec3 pos = vec3(p.x, waterY, p.y);
    vec3 n = vec3(0.0, 1.0, 0.0);
    float crest = 0.0;
    for (int i = 0; i < NW; ++i) {
        float ang = windAngle + ANG[i];
        vec2 D = vec2(cos(ang), sin(ang));
        float L = max(waveLength * LEN[i], 0.5);
        float k = 6.2831853 / L;
        float c = sqrt(9.81 / k);
        // Small waves fade out with distance where the grid gets too coarse to show them
        float A = a0 * AMP[i] * (1.0 - smoothstep(L * 4.0, L * 10.0, dist));
        float f = k * (dot(D, p) - c * time * waveSpeed);
        float S = sin(f), C = cos(f);
        // Horizontal (Gerstner) motion sharpens the crests; capped so waves never loop over
        float Qa = min(choppiness * A, 0.9 / (k * float(NW)));
        pos.xz += Qa * D * C;
        pos.y += A * S;
        n.xz -= D * (k * A * C);
        n.y -= Qa * k * S;
        crest += AMP[i] * S;
    }
    FragPos = pos;
    WaveNormal = normalize(n);
    Crest = crest / 2.27;
    Depth = depth;
    gl_Position = viewProj * vec4(pos, 1.0);
}
