#version 450

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec3 inViewVec;
layout (location = 3) in vec3 inLightVec;

layout (push_constant) uniform PushConstants {
    mat4 model;        // Vertex shader uses this
    vec3 idColor;      // Fragment shader uses these
    float padding;
} pushConstants;

layout (location = 0) out vec4 outFragColor;

void main() 
{
    // Simply output the unique object ID as a color
    outFragColor = vec4(pushConstants.idColor, 1.0);
}