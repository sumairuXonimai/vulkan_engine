#include "culling_system.h"
#include "vk_engine.h"
#include "vk_pipelines.h"

void CullingSystem::build_pipelines()
{
    VkShaderModule cullingShader;
    if (!vkutil::load_shader_module(engine->shader_path("cull.comp.spv").c_str(), engine->_device, &cullingShader))
    {
        fmt::print("Error when building the culling shader module inside CullingSystem\n");
        return;
    }

    VkPipelineShaderStageCreateInfo shaderStageInfo{ vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_COMPUTE_BIT, cullingShader) };

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(CullingPushConstant);
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    DescriptorLayoutBuilder builder;
    builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    VkDescriptorSetLayout cullingDescriptorSetLayout = builder.build(engine->_device, VK_SHADER_STAGE_COMPUTE_BIT);

    engine->_mainDeletionQueue.push_function([=]() {
        vkDestroyDescriptorSetLayout(engine->_device, cullingDescriptorSetLayout, nullptr);
        });

    VkPipelineLayoutCreateInfo cullingPipelineLayoutInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .pNext = nullptr };
    cullingPipelineLayoutInfo.setLayoutCount = 1;
    cullingPipelineLayoutInfo.pSetLayouts = &cullingDescriptorSetLayout;
    cullingPipelineLayoutInfo.pushConstantRangeCount = 1;
    cullingPipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    vkCreatePipelineLayout(engine->_device, &cullingPipelineLayoutInfo, nullptr, &cullingPipelineLayout);

    VkComputePipelineCreateInfo computePipelineInfo{ .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .pNext = nullptr };
    computePipelineInfo.stage = shaderStageInfo;
    computePipelineInfo.layout = cullingPipelineLayout;

    vkCreateComputePipelines(engine->_device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &cullingPipeline);

    vkDestroyShaderModule(engine->_device, cullingShader, nullptr);

    engine->_mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(engine->_device, cullingPipelineLayout, nullptr);
        vkDestroyPipeline(engine->_device, cullingPipeline, nullptr);
        });
}

std::vector<const RenderObject*> CullingSystem::cullScene(const std::vector<RenderObject>& allObjects, const glm::mat4& viewProj) 
{
    CullingPushConstant push;
    push.viewproj = viewProj;
    push.maxObjects = allObjects.size();

    VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    size_t cullingBufferSize{ sizeof(CullingObject) * allObjects.size() };
    AllocatedBuffer cullingBuffer{ engine->create_buffer(cullingBufferSize, usageFlags, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT) };
    VkBufferDeviceAddressInfo cullingInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = cullingBuffer.buffer
    };
    push.cullingBufferAddress = vkGetBufferDeviceAddress(engine->_device, &cullingInfo);

    CullingObject* mappedInput = static_cast<CullingObject*>(cullingBuffer.info.pMappedData);
    for (size_t i = 0; i < allObjects.size(); i++)
    {
        mappedInput[i].transform = allObjects[i].transform;
        mappedInput[i].bounds = allObjects[i].bounds;
    }

    size_t visibleIndicesBufferSize{ sizeof(uint32_t) * allObjects.size() };
    AllocatedBuffer visibleIndicesBuffer{ engine->create_buffer(visibleIndicesBufferSize, usageFlags, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT) };
    VkBufferDeviceAddressInfo visibleIndicesInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = visibleIndicesBuffer.buffer
    };
    push.visibleIndicesAddress = vkGetBufferDeviceAddress(engine->_device, &visibleIndicesInfo);
    indices = static_cast<uint32_t*>(visibleIndicesBuffer.info.pMappedData);

    size_t counterBufferSize{ sizeof(uint32_t) };
    AllocatedBuffer counterBuffer{ engine->create_buffer(counterBufferSize, usageFlags, VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT) };
    VkBufferDeviceAddressInfo counterInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = counterBuffer.buffer
    };
    push.counterBufferAddress = vkGetBufferDeviceAddress(engine->_device, &counterInfo);
    uint32_t* counterMapped = static_cast<uint32_t*>(counterBuffer.info.pMappedData);
    *counterMapped = 0;

    VK_CHECK(vkResetFences(engine->_device, 1, &engine->_computeFence));
    VK_CHECK(vkResetCommandBuffer(engine->_computeCommandBuffer, 0));

    VkCommandBuffer cmd{ engine->_computeCommandBuffer };
    VkCommandBufferBeginInfo cmdBeginInfo{ vkinit::command_buffer_begin_info() };
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullingPipeline);

    vkCmdPushConstants(cmd, cullingPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullingPushConstant), &push);

    uint32_t local_size_x = 64;
    uint32_t groupCountX = (allObjects.size() + local_size_x - 1) / local_size_x;

    vkCmdDispatch(cmd, groupCountX, 1, 1);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdSubmitInfo{ vkinit::command_buffer_submit_info(cmd) };
    VkSubmitInfo2 submit{ vkinit::submit_info(&cmdSubmitInfo, nullptr, nullptr) };

    VK_CHECK(vkQueueSubmit2(engine->_computeQueue, 1, &submit, engine->_computeFence));
    VK_CHECK(vkWaitForFences(engine->_device, 1, &engine->_computeFence, VK_TRUE, 9999999999));

    // filter only the visible objects
    uint32_t actualCount = *counterMapped;
    uint32_t* indicesMapped = static_cast<uint32_t*>(visibleIndicesBuffer.info.pMappedData);

    visibleObjects.clear();
    for (int i = 0; i < actualCount; i++)
        visibleObjects.push_back(&allObjects[indicesMapped[i]]);

    engine->get_current_frame()._deletionQueue.push_function([=]() { engine->destroy_buffer(cullingBuffer); });
    engine->get_current_frame()._deletionQueue.push_function([=]() { engine->destroy_buffer(visibleIndicesBuffer); });
    engine->get_current_frame()._deletionQueue.push_function([=]() { engine->destroy_buffer(counterBuffer); });

    return visibleObjects;
}

//std::vector<const RenderObject*> CullingSystem::cullScene(const std::vector<RenderObject>& allObjects, const glm::mat4& viewProj) 
//{
//    visibleObjects.clear();
//
//    for (const RenderObject& obj : allObjects) {
//        if (is_visible(obj, viewProj))
//            visibleObjects.push_back(&obj);
//    }
//
//    return visibleObjects;
//}