#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>


#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err) {                                                      \
            fmt::println("Detected Vulkan error: {}", string_VkResult(err)); \
            abort();                                                    \
        }                                                               \
    } while (0)

struct AllocatedImage
{
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
};

struct AllocatedBuffer
{
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
};

struct Vertex
{
    glm::vec3 position;
    float uv_x;
    glm::vec3 normal;
    float uv_y;
    glm::vec4 color;
};

struct GPUMeshBuffers
{
    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress;
};

struct GPUDrawPushConstants
{
    glm::mat4 worldMatrix;
    VkDeviceAddress vertexBufferAddress;
    VkDeviceAddress materialBufferAddress;
    uint32_t colorTexIndex;
    uint32_t metalRoughTexIndex;
    uint32_t shadowmapTexIndex;
};

enum class MaterialPass : uint8_t
{
    MainColor,
    Transparent,
    Other
};

struct MaterialPipeline
{
    VkPipeline pipeline;
    VkPipelineLayout layout;
};

// Unified Material struct
struct Material {
    std::string name;
    MaterialPipeline* pipeline{ nullptr };
    MaterialPass passType{ MaterialPass::MainColor };

    // Kept for direct shader block alignment inside GPUDrawPushConstants
    VkDeviceAddress materialBufferAddress{ 0 };

    // The material slot position inside the global system buffer [4]
    uint32_t globalMaterialIndex{ 0xFFFFFFFF };

    // [0] = Albedo/Diffuse, [1] = MetalRough, [2] = Normal, etc.
    std::vector<uint32_t> textureIndices;

    uint32_t get_texture_index(size_t slot) const {
        return slot < textureIndices.size() ? textureIndices[slot] : 0;
    }
};

struct Bounds
{
    glm::vec3 origin;
    float sphereRadius;
    glm::vec3 extents;
};

struct RenderObject
{
    Material* material{ nullptr };
    VkBuffer indexBuffer{ VK_NULL_HANDLE };
    uint32_t indexCount{ 0 };
    uint32_t firstIndex{ 0 };
    VkDeviceAddress vertexBufferAddress{ 0 };
    glm::mat4 transform{ 1.f };
    Bounds bounds;
};

struct DrawContext;

class IRenderable
{
    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
};

struct Node : public IRenderable
{
    std::weak_ptr<Node> parent;
    std::vector<std::shared_ptr<Node>> children;

    glm::mat4 localTransform;
    glm::mat4 worldTransform;

    void refreshTransform(const glm::mat4& parentMatrix)
    {
        worldTransform = parentMatrix * localTransform;
        for (auto c : children)
            c->refreshTransform(parentMatrix);
    }

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx)
    {
        for (auto& c : children)
            c->Draw(topMatrix, ctx);
    }
};