#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
in vec2 textureCoordinate;
out vec4 fragColor;
uniform samplerExternalOES inputImageTexture;

void main() {
    fragColor = texture(inputImageTexture, textureCoordinate);
}