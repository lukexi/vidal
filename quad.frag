#version 410 core

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uTexY;
uniform sampler2D uTexU;
uniform sampler2D uTexV;

const vec3 R_cf = vec3(1.164383,  0.000000,  1.596027);
const vec3 G_cf = vec3(1.164383, -0.391762, -0.812968);
const vec3 B_cf = vec3(1.164383,  2.017232,  0.000000);
const vec3 offset = vec3(-0.0625, -0.5, -0.5);

void main() {
    float y = texture(uTexY, vUV).r;
    float u = texture(uTexU, vUV).r;
    float v = texture(uTexV, vUV).r;
    vec3 yuv = vec3(y,u,v);
    yuv += offset;
    fragColor = vec4(0.0, 0.0, 0.0, 1.0);
    fragColor.r = dot(yuv, R_cf);
    fragColor.g = dot(yuv, G_cf);
    fragColor.b = dot(yuv, B_cf);
}
