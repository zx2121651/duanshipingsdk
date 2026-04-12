#version 300 es
layout(location = 0) in vec4 position;
layout(location = 1) in vec4 inputTextureCoordinate;
uniform mat4 textureMatrix;
uniform bool flipHorizontal;
uniform bool flipVertical;
out vec2 textureCoordinate;
void main() {
    gl_Position = position;
    vec2 flippedCoord = inputTextureCoordinate.xy;
    if (flipHorizontal) {
        flippedCoord.x = 1.0 - flippedCoord.x;
    }
    if (flipVertical) {
        flippedCoord.y = 1.0 - flippedCoord.y;
    }
    textureCoordinate = (textureMatrix * vec4(flippedCoord, 0.0, 1.0)).xy;
}
