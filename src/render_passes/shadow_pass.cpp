#include <shadow_pass.h>
#include <geometry_pass.h>

#include <vk_engine.h>
#include <vk_pipelines.h>
#include <vk_initializers.h>
#include <vk_images.h>
#include <fmt/format.h>

ShadowPass::ShadowPass(const std::string& name, VulkanEngine* engineP)
    : RenderPass(name), engine(engineP) {
    // depth target only
    targets.reserve(1);
    targets.push_back(new RenderTarget(engine, 512, 512, true));

    initPipeline();
}

ShadowPass::~ShadowPass() {
    if (shadowPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(engine->_device, shadowPipelineLayout, nullptr);
    }
    if (shadowPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(engine->_device, shadowPipeline, nullptr);
    }
}

const RenderTarget& ShadowPass::getDepthTarget() const {
    return *targets[0];
}

void ShadowPass::initPipeline() {
    VkShaderModule shadowVertexShader;
    if (!vkutil::load_shader_module(engine->shader_path("shadow.vert.spv").c_str(), engine->_device, &shadowVertexShader))
    {
        fmt::print("Error when building the shadow vertex shader module inside ShadowPass\n");
        return;
    }

    // Pipeline Layout with 0 descriptor sets and Push Constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ShadowPushConstants);
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo layoutInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pSetLayouts = nullptr;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    vkCreatePipelineLayout(engine->_device, &layoutInfo, nullptr, &shadowPipelineLayout);

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = shadowPipelineLayout;
    pipelineBuilder.set_shaders(shadowVertexShader, VK_NULL_HANDLE); // Depth only
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depthtest(VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);

    pipelineBuilder.set_color_attachment_format(VK_FORMAT_UNDEFINED); // No color attachment
    pipelineBuilder.set_depth_format(getDepthTarget().getFormat());

    shadowPipeline = pipelineBuilder.build_pipeline(engine->_device);

    vkDestroyShaderModule(engine->_device, shadowVertexShader, nullptr);
}

void ShadowPass::beginPass(VkCommandBuffer& cmd) {
    const auto& shadow{ getDepthTarget() };

    // Transition local shadow map resource to depth optimal layout
    vkutil::transition_image(cmd, VK_IMAGE_ASPECT_DEPTH_BIT, shadow.getImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingAttachmentInfo depthAttachment{ vkinit::depth_attachment_info(shadow.getImageView(), VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) };
    depthAttachment.clearValue.depthStencil = { 1.f, 0 }; // clear depth to far plane

    VkExtent2D shadowExtent{ shadow.getWidth(), shadow.getHeight() };
    VkRenderingInfo renderInfo{ vkinit::rendering_info(shadowExtent, VK_NULL_HANDLE, &depthAttachment) };

    // Begin dynamic rendering
    vkCmdBeginRendering(cmd, &renderInfo);

    // Viewport and scissor configuration
    VkViewport viewport{ 0.f, 0.f, (float)shadowExtent.width, (float)shadowExtent.height, 0.f, 1.f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{ {0, 0}, shadowExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void ShadowPass::render(VkCommandBuffer& cmd, const std::vector<const RenderObject*>& visibleObjects) {
    VkBuffer lastIndexBuffer{ VK_NULL_HANDLE };

    if (shadowPipeline != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipeline);

        for (const RenderObject* draw : visibleObjects)
        {
            if (draw->indexBuffer != lastIndexBuffer)
            {
                vkCmdBindIndexBuffer(cmd, draw->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                lastIndexBuffer = draw->indexBuffer;
            }

            ShadowPushConstants pushConstants;
            pushConstants.vertexBufferAddress = draw->vertexBufferAddress;
            pushConstants.lightMVP = engine->sceneData.lightSpaceMatrix * draw->transform;

            vkCmdPushConstants(cmd, shadowPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(ShadowPushConstants), &pushConstants);

            vkCmdDrawIndexed(cmd, draw->indexCount, 1, draw->firstIndex, 0, 0);
        }
    }
}

void ShadowPass::endPass(VkCommandBuffer& cmd) {
    // End dynamic rendering
    vkCmdEndRendering(cmd);

    const auto& shadow{ getDepthTarget() };
    // Transition shadow map layout to shader-read optimal so it can be sampled later
    vkutil::transition_image(cmd, VK_IMAGE_ASPECT_DEPTH_BIT, shadow.getImage(), VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}