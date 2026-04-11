#version 300 es
 layout(location=0) in vec4 p; layout(location=1) in vec2 tc; out vec2 vtc; void main(){gl_Position=p; vtc=tc;}