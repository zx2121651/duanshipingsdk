#version 300 es
precision mediump float;
in vec2 vTextureCoord;
out vec4 fragColor;
uniform sampler2D sTexture;

void main() {
    vec4 color = texture(sTexture, vTextureCoord);
    float luminance = dot(color.rgb, vec3(0.299, 0.587, 0.114));
    fragColor = vec4(0.0, luminance, 0.0, color.a);
}
