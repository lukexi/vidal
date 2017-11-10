#version 410 core

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uTex;

void main() {
    vec3 rgb = texture(uTex, vUV).rgb;

    fragColor = vec4(rgb, 1.0);
}
