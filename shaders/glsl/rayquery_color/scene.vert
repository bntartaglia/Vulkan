#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inColor;
layout (location = 3) in vec3 inNormal;

layout (binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 view;
	mat4 model;
	vec3 lightPos;
} ubo;

// Push constant for model matrix
layout (push_constant) uniform PushConstants {
	mat4 matrix;
	vec4 color;
} pushConstants;

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec3 outViewVec;
layout (location = 3) out vec3 outLightVec;
layout (location = 4) out vec3 outWorldPos;

void main() 
{
	// Pass through vertex color (though we'll use push constants in the fragment shader)
	outColor = inColor;
	outNormal = inNormal;
	
	// Use the pushed matrix instead of model
	gl_Position = ubo.projection * ubo.view * pushConstants.matrix * vec4(inPos.xyz, 1.0);
    vec4 pos = pushConstants.matrix * vec4(inPos, 1.0);
	outWorldPos = vec3(pushConstants.matrix * vec4(inPos, 1.0));
    outNormal = mat3(pushConstants.matrix) * inNormal;
    outLightVec = normalize(ubo.lightPos - inPos);
    outViewVec = -pos.xyz;
}