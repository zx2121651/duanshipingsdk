#version 300 es
layout(location = 0) in vec4 position;
layout(location = 1) in vec4 inputTextureCoordinate;
uniform mat4 textureMatrix;
uniform bool flipHorizontal;
uniform bool flipVertical;
out vec2 textureCoordinate;
void main() {
    gl_Position = position;

    vec4 coord = inputTextureCoordinate;
    // Apply manual flips if necessary BEFORE the matrix transform
    if (flipHorizontal) coord.x = 1.0 - coord.x;
    if (flipVertical) coord.y = 1.0 - coord.y;

    // Apply SurfaceTexture transform matrix to fix orientation/cropping
    textureCoordinate = (textureMatrix * coord).xy;
}
