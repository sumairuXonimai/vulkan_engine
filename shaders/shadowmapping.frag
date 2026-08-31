#version 450

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "input_structures.glsl"

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec3 inViewVec;
layout (location = 3) in vec3 inLightVec;
layout (location = 4) in vec4 inShadowCoord;

layout (constant_id = 0) const int enablePCF = 0;

layout (location = 0) out vec4 outFragColor;

#define ambient 0.1

float textureProj(vec4 shadowCoord, vec2 off, float bias)
{
	if (shadowCoord.x < 0.0 || shadowCoord.x > 1.0 ||
		shadowCoord.y < 0.0 || shadowCoord.y > 1.0 ||
		shadowCoord.z < 0.0 || shadowCoord.z > 1.0)
			return ambient;

	float shadow = 1.0;
	if ( shadowCoord.z > -1.0 && shadowCoord.z < 1.0 ) 
	{
		float dist = texture(globalTextures[nonuniformEXT(push.shadowmapTexIndex)], shadowCoord.st + off ).r;
		if ( shadowCoord.w > 0.0 && dist < (shadowCoord.z - bias)) 
		{
			shadow = ambient;
		}
	}
	return shadow;
}

float filterPCF(vec4 sc, float bias)
{
	ivec2 texDim = textureSize(globalTextures[nonuniformEXT(push.shadowmapTexIndex)], 0);
	float scale = 1.5;
	float dx = scale * 1.0 / float(texDim.x);
	float dy = scale * 1.0 / float(texDim.y);

	float shadowFactor = 0.0;
	int count = 0;
	int range = 1;
	
	for (int x = -range; x <= range; x++)
	{
		for (int y = -range; y <= range; y++)
		{
			shadowFactor += textureProj(sc, vec2(dx*x, dy*y), bias);
			count++;
		}
	
	}
	return shadowFactor / count;
}

void main() 
{	
	vec3 N = normalize(inNormal);
	vec3 L = normalize(inLightVec);
	vec3 V = normalize(inViewVec);
	vec3 R = normalize(-reflect(L, N));
	vec3 diffuse = max(dot(N, L), ambient) * inColor;

	// Calculate a dynamic slope-scaled bias based on the light angle
	float bias = max(0.005 * (1.0 - dot(N, L)), 0.001);

	float shadow = (enablePCF == 1) ? filterPCF(inShadowCoord / inShadowCoord.w, bias) : textureProj(inShadowCoord / inShadowCoord.w, vec2(0.0), bias);

	outFragColor = vec4(diffuse * shadow, 1.0);
}