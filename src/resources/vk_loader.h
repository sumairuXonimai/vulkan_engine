#pragma once
#include <vk_types.h>
#include "vk_descriptors.h"
#include <unordered_map>
#include <filesystem>
#include <memory>

struct GeoSurface
{
    uint32_t startIndex;
    uint32_t count;
    Bounds bounds;
    Material* material{ nullptr };
};

struct MeshAsset
{
    std::string name;
    std::vector<GeoSurface> surfaces;
    GPUMeshBuffers meshBuffers;
};

class VulkanEngine;

struct LoadedGLTF : public IRenderable
{
    std::unordered_map<std::string, AllocatedImage> images;
    std::unordered_map<std::string, Material*> materials;
    std::unordered_map<std::string, std::shared_ptr<MeshAsset>> meshes;
    std::unordered_map<std::string, std::shared_ptr<Node>> nodes;

    std::vector<std::shared_ptr<Node>> topNodes;
    std::vector<VkSampler> samplers;

    VulkanEngine* creator;

    ~LoadedGLTF() { clearAll(); }

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;

private:
    void clearAll();
};

std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(VulkanEngine* engine, std::string_view filePath);