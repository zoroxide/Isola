#version 330 core
// Plain shading for models and the flat test terrain
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

#include "common.glsl"

uniform sampler2D texture1;
uniform int useSolidColor;
uniform vec3 solidColor;

void main() {
    vec3 light = normalize(-lightDir);
    vec3 v = normalize(viewPos - FragPos);
    vec3 n = normalize(Normal);
    float diff = max(dot(n, light), 0.0);
    float spec = pow(max(dot(v, reflect(-light, n)), 0.0), 32.0);
    vec3 base = (useSolidColor == 1) ? solidColor : texture(texture1, TexCoords).rgb;
    float shadow = sunShadow(FragPos + n * 0.3);
    vec3 color = (skyAmbient(n) * 0.5 + diff * lightColor * shadow + pointLighting(FragPos, n, numPointLights)) * base + 0.25 * spec * lightColor * shadow;
    FragColor = vec4(applyFog(color, FragPos), 1.0);
}
