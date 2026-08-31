#version 450

#extension GL_GOOGLE_include_directive : require

#include "input_structures.glsl"

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec3 outViewVec;
layout (location = 3) out vec3 outLightVec;
layout (location = 4) out vec4 outShadowCoord;

struct Vertex {
	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
}; 

layout(buffer_reference, std430) readonly buffer VertexBuffer{ 
	Vertex vertices[];
};

const mat4 biasMat = mat4( 
	0.5, 0.0, 0.0, 0.0,
	0.0, 0.5, 0.0, 0.0,
	0.0, 0.0, 1.0, 0.0,
	0.5, 0.5, 0.0, 1.0 );

void main() 
{
	Vertex v = VertexBuffer(push.vertexBuffer).vertices[gl_VertexIndex];

	vec4 position = vec4(v.position, 1.0f);
	vec4 worldPos = push.worldMatrix * position;
	gl_Position = sceneData.viewproj * worldPos;

	outNormal = mat3(push.worldMatrix) * v.normal;

	GLTFMaterialData material = GLTFMaterialData(push.materialBufferAddress);
	outColor = v.color.xyz * material.colorFactors.xyz;

	outShadowCoord = biasMat * sceneData.lightSpaceMatrix * worldPos;

	vec3 cameraWorldPos = vec3(inverse(sceneData.view)[3]);
	// outViewVec = sceneData.cameraPos.xyz - worldPos.xyz;
	outViewVec = cameraWorldPos - worldPos.xyz;

	outLightVec = sceneData.lightPos.xyz - worldPos.xyz;
}