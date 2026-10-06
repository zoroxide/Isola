#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec2 AtlasUV;
in float Tint;
flat in float Fade;
flat in float Yaw;

#include "common.glsl"

uniform sampler2D impAlbedo;
uniform sampler2D impNormal;

float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
    if (ign(gl_FragCoord.xy) >= Fade) discard;
    vec4 a = texture(impAlbedo, AtlasUV);
    if (a.a < 0.45) discard;
    vec4 nt = texture(impNormal, AtlasUV);
    // Atlas colours are premultiplied by coverage (black background): undo it
    vec3 albedo = a.rgb / a.a * Tint;
    vec3 ln = normalize(nt.rgb / a.a * 2.0 - 1.0);
    float c = cos(Yaw), s = sin(Yaw);
    vec3 n = normalize(vec3(c * ln.x - s * ln.z, ln.y, s * ln.x + c * ln.z));
    bool leaf = nt.a / a.a > 0.75;

    vec3 light = normalize(-lightDir);
    vec3 v = normalize(viewPos - FragPos);
    float shadow = sunShadow(FragPos);
    vec3 amb = skyAmbient(n) * mix(vec3(0.5, 0.48, 0.38), vec3(1.0), n.y * 0.5 + 0.5) * 0.6;
    float ndl = dot(n, light);
    float diff = leaf ? clamp(ndl * 0.6 + 0.4, 0.0, 1.0) : max(ndl, 0.0);
    vec3 col = (amb + lightColor * diff * 0.85 * shadow + pointLighting(FragPos, n, numOutdoorLights)) * albedo;
    if (leaf) col += lightColor * albedo * vec3(0.9, 1.0, 0.5) * pow(max(dot(-v, light), 0.0), 4.0) * 0.6 * shadow;
    FragColor = vec4(applyFog(col, FragPos), 1.0);
}
