#version 100
precision mediump float;

varying vec3 fragNormal;
varying vec2 fragTexCoord;

uniform vec3 uLightDir;
uniform vec3 uBaseColor;
uniform float uKd;
uniform float uKa;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(-uLightDir);

    // Equacao (2) do artigo: Id = kd * max(0.0, dot(N, L))
    float Id = uKd * max(0.0, dot(N, L));
    vec3 finalColor = uBaseColor * (uKa + Id);

    gl_FragColor = vec4(finalColor, 1.0);
}