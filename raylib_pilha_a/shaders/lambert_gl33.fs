#version 330

in vec3 fragNormal;
in vec2 fragTexCoord;

uniform vec3 uLightDir;
uniform vec3 uBaseColor;
uniform float uKd;
uniform float uKa;

out vec4 finalColorOut;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(-uLightDir);

    // Equacao (2) do artigo: Id = kd * max(0.0, dot(N, L))
    float Id = uKd * max(0.0, dot(N, L));
    vec3 finalColor = uBaseColor * (uKa + Id);

    finalColorOut = vec4(finalColor, 1.0);
}