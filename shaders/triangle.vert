#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;


layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    fragColor = inColor;
    fragTexCoord = vec2(inTexCoord.x, inTexCoord.y);
    gl_Position = vec4(inPosition.x, inPosition.y, 0.0, 1.0);
}