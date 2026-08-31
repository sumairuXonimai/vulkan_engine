#pragma once
#include <vk_types.h>

#include "vk_descriptors.h"
#include <unordered_map>
#include <filesystem>

struct GLTFMaterial
{
	MaterialInstance data;
};

struct GeoSurface
{
	uint32_t startIndex;
	uint32_t count;
	Bounds bounds;
	MaterialInstance* material{ nullptr };
};

struct MeshAsset
{
	std::string name;

	std::vector<GeoSurface> surfaces;
	GPUMeshBuffers meshBuffers;
};

// forward decl
class VulkanEngine;

//std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(VulkanEngine* engine, std::filesystem::path filePath);

struct LoadedGLTF : public IRenderable
{
	std::unordered_map<std::string, AllocatedImage> images;
	std::unordered_map<std::string, MaterialInstance*> materials;
	std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
	std::unordered_map<std::string, std::shared_ptr<Node>> nodes;

	std::vector<std::shared_ptr<Node>> topNodes;

	std::vector<VkSampler> samplers;

	//DescriptorAllocatorGrowable descriptorPool;

	AllocatedBuffer materialDataBuffer;

	VulkanEngine* creator;

	~LoadedGLTF() { clearAll(); }

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx);

private:
	void clearAll();
};

std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(VulkanEngine* engine, std::string_view filePath);