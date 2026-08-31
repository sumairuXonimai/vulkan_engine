#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_types.h>
#include <vk_initializers.h>
#include <vk_descriptors.h>

#include "vk_textures.h"
#include "vk_shaders.h"

//#include "Tracy.hpp"
//#include "TracyVulkan.hpp"
#include "vk_profiler.h"
#include "cvars.h"

AutoCVar_Int CVAR_FreezeCull("culling.freeze", "Locks culling", 0, CVarFlags::EditCheckbox);

AutoCVar_Int CVAR_Shadowcast("gpu.shadowcast", "Use shadowcasting", 1, CVarFlags::EditCheckbox);

AutoCVar_Float CVAR_ShadowBias("gpu.shadowBias", "Distance cull", 5.25f);
AutoCVar_Float CVAR_SlopeBias("gpu.shadowBiasSlope", "Distance cull", 4.75f);


glm::vec4 normalizePlane(glm::vec4 p)
{
	return p / glm::length(glm::vec3(p));
}