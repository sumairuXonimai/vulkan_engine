#pragma once

#include <vk_types.h>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace fastgltf {
    struct Asset;
    struct Material;
}

class VulkanEngine;
class BindlessManager;

// GPU-aligned material uniform block (Matches 16-byte GLSL alignment standards)
struct alignas(16) GPUMaterialData {
    glm::vec4 colorFactors{ 1.0f };
    glm::vec4 metalRoughFactors{ 1.0f, 1.0f, 0.0f, 0.0f }; // x = metallic, y = roughness
    uint32_t baseColorTexIndex{ 0xFFFFFFFF };
    uint32_t metalRoughTexIndex{ 0xFFFFFFFF };
    uint32_t normalTexIndex{ 0xFFFFFFFF };
    uint32_t padding{ 0 };
};

class MaterialSystem {
public:
    void init(VulkanEngine* engine, BindlessManager* bindlessManager, uint32_t maxMaterials = 10000);
    void cleanup();

    // Compiles and registers standard pipelines
    void build_default_templates();
    void create_default_materials(VulkanEngine* engine);

    // Compiles and caches individual rendering pipelines (Shaders + Blend/Depth state)
    MaterialPipeline* create_pipeline(
        const std::string& name,
        const std::string& vertexShader,
        const std::string& fragmentShader,
        MaterialPass passType,
        bool depthWrite = true,
        VkCompareOp depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL
    );
    MaterialPipeline* get_pipeline(const std::string& name);

    // Directly processes GLTF material data and registers the instance with default pipelines
    Material* write_material(
        const fastgltf::Asset& asset,
        const fastgltf::Material& gltfMat,
        const std::vector<uint32_t>& textureBindlessIndices
    );

    // Creates a standalone material instance manually at runtime
    Material* create_pbr_instance(
        const std::string& instanceName,
        const glm::vec4& color,
        float metallic,
        float roughness,
        uint32_t baseColorTexBindless,
        uint32_t metalRoughTexBindless
    );

    Material* create_material_instance(
        const std::string& name,
        MaterialPipeline* pipeline,
        MaterialPass passType,
        uint32_t globalIndex
    );
    Material* get_material_instance(const std::string& name);

    // Allocates slot in the global storage buffer and returns its index [4]
    uint32_t allocate_material_slot(const GPUMaterialData& data);
    void update_material_slot(uint32_t slotIndex, const GPUMaterialData& data);

    GPUMaterialData* get_material_data_ptr(uint32_t slotIndex) {
        return _mappedMaterialData ? (_mappedMaterialData + slotIndex) : nullptr;
    }

    VkBuffer get_global_material_buffer() const { return _globalMaterialBuffer.buffer; }

private:
    VulkanEngine* _engine{ nullptr };
    BindlessManager* _bindlessManager{ nullptr };

    AllocatedBuffer _globalMaterialBuffer;
    GPUMaterialData* _mappedMaterialData{ nullptr };
    uint32_t _maxMaterials{ 0 };
    uint32_t _materialCount{ 0 };

    std::unordered_map<std::string, std::unique_ptr<MaterialPipeline>> _pipelines;
    std::unordered_map<std::string, std::unique_ptr<Material>> _materials;
};