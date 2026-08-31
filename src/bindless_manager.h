#pragma once
#include <vulkan/vulkan.h>
#include <queue>
#include <mutex>
#include <stdexcept>

class BindlessManager {
public:
    // 10,000 slots is a safe baseline supported on almost all desktop hardware
    static constexpr uint32_t MAX_BINDLESS_RESOURCES = 10000;

    void init(VkDevice device);
    void cleanup();

    // Registers a texture and returns its index in the global array
    uint32_t register_texture(VkImageView imageView, VkSampler sampler);
    void unregister_texture(uint32_t index);

    VkDescriptorSetLayout get_layout() const { return descriptorSetLayout; }
    VkDescriptorSet get_descriptor_set() const { return descriptorSet; }

private:
    VkDevice device{ VK_NULL_HANDLE };
    VkDescriptorPool descriptorPool{ VK_NULL_HANDLE };
    VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
    VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };

    std::queue<uint32_t> freeIndices;
    std::mutex allocatorMutex;
};