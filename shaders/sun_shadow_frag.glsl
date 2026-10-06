#version 330 core
// For each texel: march towards the sun and find the height a point here must be above to see it
in vec2 uv;
out float need;

uniform sampler2D heightTex;   // terrain heights (vertex-centred, texel i <-> world i*scale - half)
uniform sampler2D canopyTex;   // tree crown top heights, same layout
uniform int hasCanopy;
uniform vec3 sunDir;
uniform float halfExtent;
uniform float texelWorld;

float heightAt(vec2 u) {
    float h = textureLod(heightTex, u, 0.0).r;
    if (hasCanopy == 1) h = max(h, textureLod(canopyTex, u, 0.0).r);
    return h;
}

void main() {
    float horiz = length(sunDir.xz);
    need = -1e4;
    if (horiz < 1e-3 || sunDir.y <= 0.0) { need = sunDir.y <= 0.0 ? 1e4 : -1e4; return; }
    vec2 dir = sunDir.xz / horiz;
    float rise = sunDir.y / horiz;              // metres gained per metre travelled
    vec2 duv = dir / (2.0 * halfExtent);
    float step = texelWorld;
    float t = step;
    for (int i = 0; i < 220; ++i) {
        vec2 u = uv + duv * t;
        if (u.x < 0.0 || u.y < 0.0 || u.x > 1.0 || u.y > 1.0) break;
        need = max(need, heightAt(u) - t * rise);
        t += step;
        step *= 1.015;                          // coarser steps far away (shadows from distant peaks)
    }
}
