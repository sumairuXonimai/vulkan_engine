#pragma once

#include <render_pass.h>
#include <shadow_pass.h>
#include <string>

class VulkanEngine;

class GeometryPass : public RenderPass {
private:
    VulkanEngine* engine{ nullptr };
    ShadowPass* shadowPass;
    int shadowTexIndex;

    VkExtent2D drawExtent;

public:
    GeometryPass(const std::string& name, VulkanEngine* engineP, ShadowPass* shadowPassP);

    void setDrawExtent(VkExtent2D extent) { drawExtent = extent; }

    VulkanEngine* getEngine();

    const RenderTarget& getColorTarget() const;
    const RenderTarget& getDepthTarget() const;

protected:
    void beginPass(VkCommandBuffer& cmd) override;
    void render(VkCommandBuffer& cmd, const std::vector<const RenderObject*>& visibleObjects) override;
    void endPass(VkCommandBuffer& cmd) override;
};