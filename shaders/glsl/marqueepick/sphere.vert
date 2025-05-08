#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inColor;

layout (binding = 0) uniform UBO 
{
    mat4 projection;
    mat4 view;
    vec4 lightPos;
} ubo;

layout (push_constant) uniform PushConstants {
    mat4 model;
} pushConstants;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec3 outViewVec;
layout (location = 3) out vec3 outLightVec;

void main() 
{
    // Calculate vertex position in world space
    vec4 worldPos = pushConstants.model * vec4(inPos, 1.0);
    
    // Calculate the final vertex position in screen space
    gl_Position = ubo.projection * ubo.view * worldPos;
    
    // Calculate normal in world space
    outNormal = mat3(pushConstants.model) * inNormal;
    
    // Pass color to fragment shader
    outColor = inColor;
    
    // Vector from vertex to camera
    outViewVec = -worldPos.xyz;
    
    // Vector from vertex to light
    outLightVec = ubo.lightPos.xyz - worldPos.xyz;
}