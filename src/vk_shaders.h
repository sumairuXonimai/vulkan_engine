// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vector>
#include <array>
#include <unordered_map>

#include <vk_descriptors.h>

struct ShaderModule {
    std::vector<uint32_t> code;
    VkShaderModule module;
};

namespace vkutil {
    //loads a shader module from a spir-v file. Returns false if it errors	
    bool load_shader_module(VkDevice device, const char* filePath, ShaderModule* outShaderModule);

    uint32_t hash_descriptor_layout_info(VkDescriptorSetLayoutCreateInfo* info);
}

class VulkanEngine;

//holds all information for a given shader set for pipeline
struct ShaderEffect {
public:
    struct ReflectionOverrides {
        const char* name;
        VkDescriptorType overridenType;
    };

    struct ReflectedBinding {
        uint32_t set;
        uint32_t binding;
        VkDescriptorType type;
    };

    ShaderEffect() = default;
    ~ShaderEffect(); // Declared here, implemented in .cpp

    // Make the class move-only to prevent double-destruction of Vulkan handles
    ShaderEffect(const ShaderEffect&) = delete;
    ShaderEffect& operator=(const ShaderEffect&) = delete;

    ShaderEffect(ShaderEffect&& other) noexcept;
    ShaderEffect& operator=(ShaderEffect&& other) noexcept;

    void add_stage(ShaderModule* shaderModule, VkShaderStageFlagBits stage);

    // Pass the device here and store it for destruction later
    void reflect_layout(VkDevice device, ReflectionOverrides* overrides, int overrideCount);

    void fill_stages(std::vector<VkPipelineShaderStageCreateInfo>& pipelineStages);

    VkPipelineLayout get_layout() const { return builtLayout; }
    const std::array<VkDescriptorSetLayout, 4>& get_set_layouts() const { return setLayouts; }

    // Declared public for bind checks, but actual data remains protected
    std::array<VkDescriptorSetLayout, 4> setLayouts{ VK_NULL_HANDLE };
    std::array<uint32_t, 4> setHashes{ 0 };
    VkPipelineLayout builtLayout{ VK_NULL_HANDLE };
    std::unordered_map<std::string, ReflectedBinding> bindings;

private:
    struct ShaderStage {
        ShaderModule* shaderModule;
        VkShaderStageFlagBits stage;
    };

    void cleanup();

    VkDevice device{ VK_NULL_HANDLE };
    std::vector<ShaderStage> stages;
};

class ShaderDescriptorBinder {
public:
    struct BufferWriteDescriptor {
        int dstSet;
        int dstBinding;
        VkDescriptorType descriptorType;
        VkDescriptorBufferInfo bufferInfo;
        uint32_t dynamic_offset;
    };

    ShaderDescriptorBinder() = default;
    ~ShaderDescriptorBinder() = default;

    ShaderDescriptorBinder(const ShaderDescriptorBinder&) = default;
    ShaderDescriptorBinder& operator=(const ShaderDescriptorBinder&) = default;
    ShaderDescriptorBinder(ShaderDescriptorBinder&&) noexcept = default;
    ShaderDescriptorBinder& operator=(ShaderDescriptorBinder&&) noexcept = default;

    void bind_buffer(const char* name, const VkDescriptorBufferInfo& bufferInfo);
    void bind_dynamic_buffer(const char* name, uint32_t offset, const VkDescriptorBufferInfo& bufferInfo);

    void apply_binds(VkCommandBuffer cmd);
    void build_sets(VkDevice device, DescriptorAllocatorGrowable& allocator);
    void set_shader(ShaderEffect* newShader);

    const std::array<VkDescriptorSet, 4>& get_descriptor_sets() const { return cachedDescriptorSets; }

private:
    struct DynOffsets {
        std::array<uint32_t, 16> offsets;
        uint32_t count{ 0 };
    };

    std::array<VkDescriptorSet, 4> cachedDescriptorSets{ VK_NULL_HANDLE };
    std::array<DynOffsets, 4> setOffsets;
    ShaderEffect* shaders{ nullptr };
    std::vector<BufferWriteDescriptor> bufferWrites;
};

class ShaderCache {
public:
    ShaderModule* get_shader(const std::string& path);
    void init(VkDevice device) { _device = device; };
    void cleanup();
private:
    VkDevice _device;
    std::unordered_map<std::string, ShaderModule> module_cache;
};