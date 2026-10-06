#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in float T;
in float AO;
in float Shadow;
in vec3 Variation;
flat in vec3 FlowerColor;
in float IsFlower;
in vec3 BaseColor;
in vec3 Ambient;
in vec4 Fog;

uniform vec3 lightDir;
uniform vec3 lightColor;
uniform vec3 viewPos;

void main() {
    vec3 light = normalize(-lightDir);
    vec3 v = normalize(viewPos - FragPos);
    vec3 n = normalize(Normal);
    if (dot(n, v) < 0.0) n = -n;   // two-sided blades

    vec3 root = BaseColor * vec3(0.6, 0.68, 0.5);
    vec3 tip = BaseColor * vec3(1.35, 1.35, 0.85) + vec3(0.06, 0.07, 0.0);
    vec3 albedo = mix(root, tip, smoothstep(0.0, 1.0, T)) * Variation;
    if (IsFlower > 0.5) albedo = FlowerColor;

    float ndl = dot(n, light);
    vec3 col = (Ambient + lightColor * clamp(ndl * 0.6 + 0.4, 0.0, 1.0) * 0.85 * Shadow) * albedo * AO;
    // Thin blades let sunlight through when backlit
    col += lightColor * albedo * pow(max(dot(-v, light), 0.0), 3.0) * T * 0.8 * Shadow;
    // Sheen: blades catch the light at grazing angles, which gives meadows their silvery shimmer
    vec3 h = normalize(light + v);
    float fres = pow(1.0 - abs(dot(n, v)), 3.0);
    col += lightColor * (pow(max(dot(n, h), 0.0), 24.0) * 0.18 + fres * 0.06) * T * Shadow;
    FragColor = vec4(mix(col, Fog.rgb, Fog.a), 1.0);
}
