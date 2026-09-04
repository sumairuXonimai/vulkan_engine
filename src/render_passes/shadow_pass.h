#pragma once

#include <render_pass.h>
#include <string>

class GeometryPass;
class VulkanEngine;

class ShadowPass : public RenderPass
{
private:
    GeometryPass* geometryPass{ nullptr };
    VulkanEngine* engine{ nullptr };

    // Pass pipeline ownership
    VkPipeline shadowPipeline{ VK_NULL_HANDLE };
    VkPipelineLayout shadowPipelineLayout{ VK_NULL_HANDLE };

public:
    struct ShadowPushConstants {
        glm::mat4 lightMVP;
        VkDeviceAddress vertexBufferAddress;
    };

    ShadowPass(const std::string& name, VulkanEngine* engineP);
    ~ShadowPass() override;

    const RenderTarget& getDepthTarget() const;

protected:
    void beginPass(VkCommandBuffer& cmd) override;
    void render(VkCommandBuffer& cmd, const std::vector<const RenderObject*>& visibleObjects) override;
    void endPass(VkCommandBuffer& cmd) override;

private:
    void initPipeline();
};