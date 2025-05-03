#version 450

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec3 inViewVec;
layout (location = 3) in vec3 inLightVec;

layout (binding = 0) uniform UBO 
{
    mat4 projection;
    mat4 view;
    vec4 lightPos;
} ubo;

layout (push_constant) uniform PushConstants {
    mat4 model;           // Vertex shader uses this
    vec3 objectColor;     // Fragment shader uses these
    float selected;
} pushConstants;

layout (location = 0) out vec4 outFragColor;

void main() 
{
    // Basic lighting calculation
    vec3 N = normalize(inNormal);
    vec3 L = normalize(inLightVec);
    vec3 V = normalize(inViewVec);
    vec3 R = reflect(-L, N);
    
    // Diffuse lighting
    vec3 diffuse = max(dot(N, L), 0.15) * pushConstants.objectColor;
    
    // Specular lighting
    vec3 specular = pow(max(dot(R, V), 0.0), 8.0) * vec3(0.5);
    
    // Combine lighting components
    vec3 color = diffuse + specular;
    
    // If object is selected, add a highlight effect
    if (pushConstants.selected > 0.5) {
        // Add pulsing highlight effect for selected objects
        float highlight = 0.3 * (sin(float(gl_PrimitiveID) * 0.01) * 0.5 + 0.5);
        color = mix(color, vec3(1.0, 1.0, 0.2), highlight);
    }
    
    outFragColor = vec4(color, 1.0);
}