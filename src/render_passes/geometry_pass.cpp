#include <geometry_pass.h>

#include <culling_system.h>
#include <vk_engine.h>
#include <vk_initializers.h>
#include <vk_images.h>

#include <cstring>

GeometryPass::GeometryPass(const std::string& name, VulkanEngine* engineP, ShadowPass* shadowPassP)
    : RenderPass(name), engine(engineP), shadowPass(shadowPassP) {
    // create render targets
    targets.reserve(2);
    // color, depth
    targets.push_back(new RenderTarget(engine, 1920, 1080));
    targets.push_back(new RenderTarget(engine, 1920, 1080, true));

    shadowTexIndex = engine->_bindlessManager.register_texture(shadowPass->getDepthTarget().getImageView(), engine->_defaultSamplerLinear);
}

VulkanEngine* GeometryPass::getEngine() {
    return engine;
}

const RenderTarget& GeometryPass::getColorTarget() const {
    return *targets[0];
}

const RenderTarget& GeometryPass::getDepthTarget() const {
    return *targets[1];
}

void GeometryPass::beginPass(VkCommandBuffer& cmd) {
    const auto& color{ getColorTarget() };
    const auto& depth{ getDepthTarget() };

    vkutil::transition_image(cmd, VK_IMAGE_ASPECT_COLOR_BIT, color.getImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, VK_IMAGE_ASPECT_DEPTH_BIT, depth.getImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    
    // Set up color attachment
    VkRenderingAttachmentInfo colorAttachment{ vkinit::attachment_info(color.getImageView(), nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) };

    // Set up depth attachment
    VkRenderingAttachmentInfo depthAttachment{ vkinit::depth_attachment_info(depth.getImageView(), VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) };

    // Configure rendering info
    VkRenderingInfo renderInfo{ vkinit::rendering_info(drawExtent, &colorAttachment, &depthAttachment) };

    // Begin dynamic rendering
    vkCmdBeginRendering(cmd, &renderInfo);

    VkViewport viewport{ 0.f, 0.f, (float)drawExtent.width, (float)drawExtent.height, 0.f, 1.f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{ {0, 0}, drawExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void GeometryPass::render(VkCommandBuffer& cmd, const std::vector<const RenderObject*>& visibleObjects) {
    VkClearAttachment clearAttachment = {};
    clearAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clearAttachment.colorAttachment = 0; // The index of the color attachment
    clearAttachment.clearValue.color = VkClearColorValue{ 0.f, 0.f, 1.f, 1.f };

    VkClearRect clearRect = {};
    clearRect.rect.offset = { 0, 0 };
    clearRect.rect.extent = drawExtent;
    clearRect.baseArrayLayer = 0;
    clearRect.layerCount = 1;

    vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &clearRect);

    // Get visible ROs
    FrameData& currentFrame = engine->get_current_frame();
    std::memcpy(currentFrame.mappedSceneData, &engine->sceneData, sizeof(GPUSceneData));

    VkDescriptorSet& globalDescriptorSet{ currentFrame.globalDescriptorSet };

    MaterialPipeline* lastPipeline{ nullptr };
    Material* lastMaterial{ nullptr };
    VkBuffer lastIndexBuffer{ VK_NULL_HANDLE };

    // Render each RO to render targets
    for (auto draw : visibleObjects) {
        if (draw->material != lastMaterial)
        {
            if (draw->material->pipeline != lastPipeline)
            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw->material->pipeline->pipeline);

                // set 0: static global bindless texture array set
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw->material->pipeline->layout,
                    0, 1, &engine->bindlessDescriptorSet, 0, nullptr);

                // set 1: per-frame scene data descriptor set
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw->material->pipeline->layout,
                    1, 1, &globalDescriptorSet, 0, nullptr);

                lastPipeline = draw->material->pipeline;
            }
            lastMaterial = draw->material;
        }

        if (draw->indexBuffer != lastIndexBuffer)
        {
            vkCmdBindIndexBuffer(cmd, draw->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            lastIndexBuffer = draw->indexBuffer;
        }

        // generic push constant population
        GPUDrawPushConstants pushConstants;
        pushConstants.vertexBufferAddress = draw->vertexBufferAddress;
        pushConstants.worldMatrix = draw->transform;
        pushConstants.materialBufferAddress = draw->material->materialBufferAddress;

        // pass generic mapping slots to shaders
        pushConstants.colorTexIndex = draw->material->get_texture_index(0);
        pushConstants.metalRoughTexIndex = draw->material->get_texture_index(1);
        pushConstants.shadowmapTexIndex = shadowTexIndex;

        vkCmdPushConstants(cmd, draw->material->pipeline->layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(GPUDrawPushConstants), &pushConstants);

        vkCmdDrawIndexed(cmd, draw->indexCount, 1, draw->firstIndex, 0, 0);

        engine->stats.drawcall_count++;
        engine->stats.triangle_count += draw->indexCount / 3;
    }
}

void GeometryPass::endPass(VkCommandBuffer& cmd) {
    vkCmdEndRendering(cmd);
}