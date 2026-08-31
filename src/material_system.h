#pragma once

#include <vk_types.h>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

// Forward declare the fastgltf namespace and struct
namespace fastgltf {
    struct Asset;
    struct Material;
}

class VulkanEngine;
class BindlessManager;

class MaterialType {
public:
    virtual ~MaterialType() = default;
    virtual void init(VulkanEngine* engine) = 0;
    virtual size_t get_constant_buffer_size() const = 0;
    virtual MaterialInstance* write_material(
        VulkanEngine* engine,
        const fastgltf::Asset& asset,
        const fastgltf::Material& gltfMat,
        VkDeviceAddress gpuBufferAddress,
        void* cpuBufferPointer,
        const std::vector<AllocatedImage>& images,
        const std::vector<VkSampler>& samplers
    ) = 0;
};

class MaterialSystem {
public:
    // Only declare functions in the header
    void init(VulkanEngine* engine, BindlessManager* bindlessManager);
    void cleanup();
    void build_default_templates();

    void create_default_materials(VulkanEngine* engine);

    void register_material_type(const std::string& name, std::unique_ptr<MaterialType> type);
    MaterialType* get_material_type(const std::string& name);

    MaterialPipeline* create_pipeline(
        const std::string& name,
        const std::string& vertexShader,
        const std::string& fragmentShader,
        MaterialPass passType,
        bool depthWrite = true,
        VkCompareOp depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL
    );
    MaterialPipeline* get_pipeline(const std::string& name);

    void register_pbr_material(
        const std::string& name,
        const std::string& vertexShader,
        const std::string& fragmentShader,
        const std::string& prefixName = "Standard"
    );

    MaterialInstance* create_pbr_instance(
        const std::string& instanceName,
        const std::string& templateTypeName,
        const glm::vec4& color,
        float metallic,
        float roughness,
        VkImageView textureView,
        VkSampler sampler
    );

    MaterialInstance* create_material_instance(
        const std::string& name,
        MaterialPipeline* pipeline,
        MaterialPass passType,
        VkDeviceAddress bufferAddress,
        const std::vector<std::pair<VkImageView, VkSampler>>& textures
    );
    MaterialInstance* get_material_instance(const std::string& name);

private:
    VulkanEngine* _engine{ nullptr };
    BindlessManager* _bindlessManager{ nullptr };

    std::vector<AllocatedBuffer> _allocatedBuffers;

    std::unordered_map<std::string, std::unique_ptr<MaterialType>> _types;
    std::unordered_map<std::string, std::unique_ptr<MaterialPipeline>> _pipelines;
    std::unordered_map<std::string, std::unique_ptr<MaterialInstance>> _materials;
};