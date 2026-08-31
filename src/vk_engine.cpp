//> includes
#include "vk_engine.h"
#include "vk_images.h"
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include "vk_pipelines.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include <glm/gtx/transform.hpp>

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_initializers.h>
#include <vk_types.h>
#include <vk_shaders.h>

#include "VkBootstrap.h"

#include <chrono>
#include <thread>

bool is_visible(const RenderObject& obj, const glm::mat4& viewproj)
{
    constexpr std::array<glm::vec3, 8> corners{
        glm::vec3{ 1, 1, 1 },
        glm::vec3{ 1, 1, -1},
        glm::vec3{ 1, -1, 1},
        glm::vec3{ 1, -1, -1},
        glm::vec3{ -1, 1, 1},
        glm::vec3{ -1, 1, -1},
        glm::vec3{ -1, -1, 1},
        glm::vec3{ -1, -1, -1}
    };

    glm::mat4 matrix{ viewproj * obj.transform };

    glm::vec3 min{ 1.5, 1.5, 1.5 };
    glm::vec3 max{ -1.5, -1.5, -1.5 };

    for (int i = 0; i < 8; i++)
    {
        glm::vec4 v{ matrix * glm::vec4(obj.bounds.origin + (corners[i] * obj.bounds.extents), 1.f) };

        v.x /= v.w;
        v.y /= v.w;
        v.z /= v.w;

        min = glm::min(min, glm::vec3{ v.x, v.y, v.z });
        max = glm::max(max, glm::vec3{ v.x, v.y, v.z });
    }

    if (min.z > 1.f || max.z < 0.f || min.x > 1.f || max.x < -1.f || min.y > 1.f || max.y < -1.f)
        return false;
    else
        return true;
}

VulkanEngine* loadedEngine = nullptr;
constexpr bool bUseValidationLayers = true;

VulkanEngine& VulkanEngine::Get() { return *loadedEngine; }
void VulkanEngine::init()
{
    // only one engine initialization is allowed with the application.
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags{ (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE) };

    _window = SDL_CreateWindow(
        "Vulkan Engine",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        _windowExtent.width,
        _windowExtent.height,
        window_flags);

    init_vulkan();
    init_swapchain();
    init_commands();
    init_sync_structures();
    init_descriptors();
    init_pipelines();
    init_imgui();
    init_default_data();

    mainCamera.velocity = glm::vec3(0.f);
    mainCamera.position = glm::vec3(0.f);

    mainCamera.pitch = 0;
    mainCamera.yaw = 0;

    std::string vulkanscenemodelPath{ "../../assets/samplescene.gltf" };
    std::string vulkanscenemodelshadowsPath{ "../../assets/vulkanscene_shadow.gltf" };
    std::string shadowtestPath{ "../../assets/shadow_test.glb" };
    auto vulkanscenemodelFile{ loadGltf(this, vulkanscenemodelPath) };
    auto vulkanscenemodelshadowsFile{ loadGltf(this, vulkanscenemodelshadowsPath) };
    auto shadowtestFile{ loadGltf(this, shadowtestPath) };

    assert(vulkanscenemodelFile.has_value());
    assert(vulkanscenemodelshadowsFile.has_value());
    assert(shadowtestFile.has_value());

    loadedScenes["samplescene"] = vulkanscenemodelFile.value();
    loadedScenes["vulkansceneshadow"] = vulkanscenemodelshadowsFile.value();
    loadedScenes["shadowtest"] = shadowtestFile.value();

    // shadows
    VkSamplerCreateInfo depthSampl{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    depthSampl.minFilter = VK_FILTER_LINEAR;
    depthSampl.magFilter = VK_FILTER_LINEAR;
    depthSampl.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    depthSampl.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    depthSampl.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    depthSampl.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; // Returns 1.0 (unshadowed) outside light frustum
    vkCreateSampler(_device, &depthSampl, nullptr, &_shadowSampler);
    _shadowmapTexIndex = _bindlessManager.register_texture(_shadowmap.imageView, _shadowSampler);

    _mainDeletionQueue.push_function([&] {
        vkDestroySampler(_device, _shadowSampler, nullptr);
        });

    // everything went fine
    _isInitialized = true;
}

void VulkanEngine::init_vulkan()
{
    vkb::InstanceBuilder builder;

    // make the vulkan instace, with basic debug features
    auto inst_ret{ 
        builder
        .set_app_name("Example Vulkan Application")
        .request_validation_layers(bUseValidationLayers)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build() 
    };

    vkb::Instance vkb_inst{ inst_ret.value() };

    // grab the instace
    _instance = vkb_inst.instance;
    _debug_messenger = vkb_inst.debug_messenger;

    SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

    // vulkan 1.0 features
    VkPhysicalDeviceFeatures features10{};
    features10.shaderInt64 = VK_TRUE;

    // vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    features.dynamicRendering = VK_TRUE;
    features.synchronization2 = VK_TRUE;

    // vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    features12.bufferDeviceAddress = VK_TRUE; // enable Buffer Device Address (BDA)

    // explicitly request bindless texturing capabilities
    features12.descriptorIndexing = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE; // nonuniformEXT()
    features12.runtimeDescriptorArray = VK_TRUE;                     // unsized arrays (e.g. textures[])
    features12.descriptorBindingPartiallyBound = VK_TRUE;           // leaving slots empty
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE; // updating descriptors mid-frame

    // use vkbootstrap to select a gpu.
    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    vkb::PhysicalDevice physicalDevice{
        selector
        .set_minimum_version(1, 3)
        .set_required_features(features10)
        .set_required_features_13(features)
        .set_required_features_12(features12)
        .set_surface(_surface)
        .select()
        .value()
    };

    // create the final vulkan device
    vkb::DeviceBuilder deviceBuilder{ physicalDevice };
    vkb::Device vkbDevice{ deviceBuilder.build().value() };

    // Get the VkDevice handle used in the rest of a vulkan application
    _device = vkbDevice.device;
    _chosenGPU = physicalDevice.physical_device;

    // use vkbootstrap to get a Graphics queue
    _graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // initialize the memory allocator
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = _chosenGPU;
    allocatorInfo.device = _device;
    allocatorInfo.instance = _instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocatorInfo, &_allocator);

    // init shader loader
    _shaderCache.init(_device);

    // init bindless manager
    _bindlessManager.init(_device);
    bindlessDescriptorSet = _bindlessManager.get_descriptor_set();

    // Init the new MaterialSystem
    _materialSystem.init(this, &_bindlessManager);

    _mainDeletionQueue.push_function([&]() {
        _materialSystem.cleanup();
        _bindlessManager.cleanup();
        _shaderCache.cleanup();
        vmaDestroyAllocator(_allocator);
    });
}

AllocatedImage VulkanEngine::create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    AllocatedImage newImage;
    newImage.imageFormat = format;
    newImage.imageExtent = size;

    VkImageCreateInfo imgInfo{ vkinit::image_create_info(newImage.imageFormat, usage, newImage.imageExtent) };
    if (mipmapped)
        imgInfo.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;

    //for the draw image, we want to allocate it from gpu memory
    VmaAllocationCreateInfo img_allocinfo{};
    img_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    img_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    //allocate and create the image
    vmaCreateImage(_allocator, &imgInfo, &img_allocinfo, &newImage.image, &newImage.allocation, nullptr);

    //choose the correct aspect flag
    VkImageAspectFlags aspectFlag{ VK_IMAGE_ASPECT_COLOR_BIT };
    if (format == VK_FORMAT_D32_SFLOAT)
        aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;

    //build the image view
    VkImageViewCreateInfo rview_info{ vkinit::imageview_create_info(newImage.imageFormat, newImage.image, aspectFlag) };
    rview_info.subresourceRange.levelCount = imgInfo.mipLevels;

    VK_CHECK(vkCreateImageView(_device, &rview_info, nullptr, &newImage.imageView));

    return newImage;
}

AllocatedImage VulkanEngine::create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped)
{
    size_t dataSize{ size.width * size.height * size.depth * 4 };
    AllocatedBuffer uploadBuffer{ create_buffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU) };

    memcpy(uploadBuffer.info.pMappedData, data, dataSize);

    AllocatedImage newImage{ create_image(size, format, usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, mipmapped) };

    immediate_submit([&](VkCommandBuffer cmd) {
        vkutil::transition_image(cmd, VK_IMAGE_ASPECT_COLOR_BIT, newImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;

        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;

        copyRegion.imageExtent = size;

        //copy the buffer into the image
        vkCmdCopyBufferToImage(cmd, uploadBuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        if (mipmapped)
            vkutil::generate_mipmap(cmd, newImage.image, VkExtent2D{ newImage.imageExtent.width, newImage.imageExtent.height });
        else
            vkutil::transition_image(cmd, VK_IMAGE_ASPECT_COLOR_BIT, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

    destroy_buffer(uploadBuffer);

    return newImage;
}

void VulkanEngine::destroy_image(const AllocatedImage& img)
{
    vkDestroyImageView(_device, img.imageView, nullptr);
    vmaDestroyImage(_allocator, img.image, img.allocation);
}

void VulkanEngine::init_swapchain()
{
    create_swapchain(_windowExtent.width, _windowExtent.height);

    //draw image size will match the window
    VkExtent3D drawImageExtent{
        _windowExtent.width,
        _windowExtent.height,
        1
    };

    //hardcoding the draw format to 64-bit float
    _drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    _drawImage.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsage{};
    drawImageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo rimgInfo{ vkinit::image_create_info(_drawImage.imageFormat, drawImageUsage, _drawImage.imageExtent) };

    //for the draw image, we want to allocate it from gpu memory
    VmaAllocationCreateInfo rimg_allocinfo{};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    //allocate and create the image
    vmaCreateImage(_allocator, &rimgInfo, &rimg_allocinfo, &_drawImage.image, &_drawImage.allocation, nullptr);

    //build the image view
    VkImageViewCreateInfo rview_info{ vkinit::imageview_create_info(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT) };
    vkCreateImageView(_device, &rview_info, nullptr, &_drawImage.imageView);

    // do the same for depth image
    _depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    _depthImage.imageExtent = drawImageExtent;

    VkImageUsageFlags depthImageUsage{};
    depthImageUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VkImageCreateInfo dimginfo{ vkinit::image_create_info(_depthImage.imageFormat, depthImageUsage, _depthImage.imageExtent) };

    vmaCreateImage(_allocator, &dimginfo, &rimg_allocinfo, &_depthImage.image, &_depthImage.allocation, nullptr);

    VkImageViewCreateInfo dview_info{ vkinit::imageview_create_info(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT) };
    vkCreateImageView(_device, &dview_info, nullptr, &_depthImage.imageView);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyImageView(_device, _drawImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);
        vkDestroyImageView(_device, _depthImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
    });

    // shadowmap image
    init_shadowmap();
}

void VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU, _device, _surface };

    _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain{
        swapchainBuilder
        //.use_default_format_selection()
        .set_desired_format(VkSurfaceFormatKHR{.format = _swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
        //use vsync present mode
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build()
        .value()
    };

    _swapchainExtent = vkbSwapchain.extent;
    //store swapchain and its related images
    _swapchain = vkbSwapchain.swapchain;
    _swapchainImages = vkbSwapchain.get_images().value();
    _swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void VulkanEngine::resize_swapchain()
{
    vkDeviceWaitIdle(_device);

    destroy_swapchain();

    int w, h;
    SDL_GetWindowSize(_window, &w, &h);
    _windowExtent.width = w;
    _windowExtent.height = h;

    create_swapchain(_windowExtent.width, _windowExtent.height);

    resize_requested = false;
}

void VulkanEngine::destroy_swapchain()
{
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);

    // destroy swapchain resources
    for (int i = 0; i < _swapchainImageViews.size(); i++)
    {
        vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
    }
}

void VulkanEngine::init_commands()
{
    VkCommandPoolCreateInfo commandPoolInfo{};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.pNext = nullptr;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolInfo.queueFamilyIndex = _graphicsQueueFamily;

    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

        // allocate the default command buffer that we will use for rendering
        VkCommandBufferAllocateInfo cmdAlocInfo{};
        cmdAlocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAlocInfo.pNext = nullptr;
        cmdAlocInfo.commandPool = _frames[i]._commandPool;
        cmdAlocInfo.commandBufferCount = 1;
        cmdAlocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAlocInfo, &_frames[i]._mainCommandBuffer));
    }

    // immediate commands
    VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));

    // allocate the command buffer for immediate submits
    VkCommandBufferAllocateInfo cmdAllocInfo{ vkinit::command_buffer_allocate_info(_immCommandPool, 1) };

    VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));
    _mainDeletionQueue.push_function([&]() {
        vkDestroyCommandPool(_device, _immCommandPool, nullptr);
    });
}

void VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function)
{
    VK_CHECK(vkResetFences(_device, 1, &_immFence));
    VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

    VkCommandBuffer cmd{ _immCommandBuffer };

    VkCommandBufferBeginInfo cmdBeginInfo{ vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) };

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdSubmitInfo{ vkinit::command_buffer_submit_info(cmd) };
    VkSubmitInfo2 submit{ vkinit::submit_info(&cmdSubmitInfo, nullptr, nullptr) };

    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));
    VK_CHECK(vkWaitForFences(_device, 1, &_immFence, VK_TRUE, 9999999999));
}

void VulkanEngine::init_sync_structures()
{
    //create synchronization structures
    //one fence to control when the gpu has finished rendering in the frame
    //and 2 semaphores to synchronize rendering with swapchain
    //we want the fence to start signalled so we can wait on it on the first frame
    VkFenceCreateInfo fenceCreateInfo{ vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT) };
    VkSemaphoreCreateInfo semaphoreCreateInfo{ vkinit::semaphore_create_info() };

    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));
        
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapchainSemaphore));
    }

    _renderSemaphores.resize(_swapchainImages.size());
    for (int i = 0; i < _renderSemaphores.size(); i++)
    {
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_renderSemaphores[i]));
    }

    // for immediate commands
    VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
    _mainDeletionQueue.push_function([&]() {
        vkDestroyFence(_device, _immFence, nullptr);
    });
}

void VulkanEngine::init_descriptors()
{
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes{
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}
    };

    globalDescriptorAllocator.init(_device, 10, sizes);

    DescriptorLayoutBuilder builder;
    builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    _drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);

    // allocate a descriptor set for our draw image
    _drawImageDescriptors = globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);

    DescriptorWriter writer;
    writer.write_image(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

    writer.update_set(_device, _drawImageDescriptors);

    _mainDeletionQueue.push_function([&]() {
            globalDescriptorAllocator.destroy_pools(_device);
            
            vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
        });

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        _gpuSceneDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        _mainDeletionQueue.push_function([&]() {
            vkDestroyDescriptorSetLayout(_device, _gpuSceneDataDescriptorLayout, nullptr);
            });
    }

    {
        DescriptorLayoutBuilder builder;
        builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        _singleImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
        _mainDeletionQueue.push_function([&]() {
            vkDestroyDescriptorSetLayout(_device, _singleImageDescriptorLayout, nullptr);
            });
    }

    for (int i = 0; i < FRAME_OVERLAP; i++)
    {
        std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes{
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
        };

        _frames[i]._frameDescriptors = DescriptorAllocatorGrowable{};
        _frames[i]._frameDescriptors.init(_device, 1000, frame_sizes);

        _frames[i].sceneDataBuffer = create_buffer(
            sizeof(GPUSceneData),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        _frames[i].mappedSceneData = _frames[i].sceneDataBuffer.allocation->GetMappedData();

        _frames[i].globalDescriptorSet = globalDescriptorAllocator.allocate(_device, _gpuSceneDataDescriptorLayout);

        DescriptorWriter writer;
        writer.write_buffer(0, _frames[i].sceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        writer.update_set(_device, _frames[i].globalDescriptorSet);

        _mainDeletionQueue.push_function([&, i]() {
            _frames[i]._frameDescriptors.destroy_pools(_device);
        });
    }
}

void VulkanEngine::init_pipelines()
{   
    init_shadow_pipeline();
    // material system's default pipelines (PBRMetallicRoughness)
    _materialSystem.build_default_templates();
}

void VulkanEngine::init_scene_pipelines()
{
    //_materialSystem.create_pipeline()
}

void VulkanEngine::init_imgui()
{
    VkDescriptorPoolSize poolSizes[]{
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = (uint32_t)std::size(poolSizes);
    poolInfo.pPoolSizes = poolSizes;

    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &imguiPool));

    ImGui::CreateContext();

    ImGui_ImplSDL2_InitForVulkan(_window);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = _instance;
    initInfo.PhysicalDevice = _chosenGPU;
    initInfo.Device = _device;
    initInfo.Queue = _graphicsQueue;
    initInfo.DescriptorPool = imguiPool;
    initInfo.MinImageCount = 3;
    initInfo.ImageCount = 3;
    initInfo.UseDynamicRendering = true;

    initInfo.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;

    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);

    ImGui_ImplVulkan_CreateFontsTexture();

    _mainDeletionQueue.push_function([=]() {
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(_device, imguiPool, nullptr);
        });
}

void VulkanEngine::init_default_data() 
{
    //testMeshes = loadGltfMeshes(this, "../../assets/basicmesh.glb").value();

    //3 default textures, white, grey, black
    uint32_t white{ glm::packUnorm4x8(glm::vec4(1, 1, 1, 1)) };
    uint32_t grey{ glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1)) };
    uint32_t black{ glm::packUnorm4x8(glm::vec4(0, 0, 0, 1)) };
    uint32_t magenta{ glm::packUnorm4x8(glm::vec4(1, 0, 1, 1)) };

    _whiteImage = create_image((void*)&white, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    _greyImage = create_image((void*)&grey, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    _blackImage = create_image((void*)&black, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    std::array<uint32_t, 16 * 16> pixels;
    for (int x = 0; x < 16; x++)
        for (int y = 0; y < 16; y++)
            pixels[x * 16 + y] = ((x & 1) ^ (y & 1)) ? magenta : black;

    _errorCheckerboardImage = create_image(pixels.data(), VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    VkSamplerCreateInfo sampl{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr };
    sampl.minFilter = VK_FILTER_LINEAR;
    sampl.magFilter = VK_FILTER_LINEAR;
    vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerLinear);

    sampl.minFilter = VK_FILTER_NEAREST;
    sampl.magFilter = VK_FILTER_NEAREST;
    vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerNearest);

    _mainDeletionQueue.push_function([&]() {
        vkDestroySampler(_device, _defaultSamplerLinear, nullptr);
        vkDestroySampler(_device, _defaultSamplerNearest, nullptr);

        destroy_image(_whiteImage);
        destroy_image(_greyImage);
        destroy_image(_blackImage);
        destroy_image(_errorCheckerboardImage);
    });

    // default/fallback material
    _materialSystem.create_default_materials(this);
    MaterialInstance* defaultMaterial{ _materialSystem.get_material_instance("DefaultMaterial") };

    _materialSystem.register_pbr_material("PBRShadow", "shadowmapping.vert.spv", "shadowmapping.frag.spv", "Shadow");
    MaterialInstance* shadowedMaterial = _materialSystem.create_pbr_instance(
        "PBRShadow",                         // unique name of material instance
        "Shadowmap",                         // shader template to use
        glm::vec4(0.0f, 1.0f, 0.0f, 0.5f),   // color factor
        1.0f,                                // metallic factor
        0.1f,                                // roughness factor
        _whiteImage.imageView,               // albedo texture
        _defaultSamplerLinear                // sampler
    );
}

void VulkanEngine::update_scene()
{
    auto start{ std::chrono::system_clock::now() };

    mainCamera.update();

    glm::mat4 view{ mainCamera.getViewMatrix() };
    glm::mat4 projection{ glm::perspective(glm::radians(70.f), (float)_windowExtent.width / (float)_windowExtent.height, 10000.f, 0.1f) };

    sceneData.view = view;
    sceneData.proj = projection;

    sceneData.proj[1][1] *= -1;
    sceneData.viewproj = sceneData.proj * sceneData.view;

    sceneData.cameraPos = glm::vec4(mainCamera.position, 1.f);

    {
        float timer = _frameNumber / 4.f;
        glm::vec3 lightPos{};
        lightPos.x = cos(glm::radians(timer)) * 10.f;
        lightPos.y = 5.f;
        lightPos.z = sin(glm::radians(timer)) * 10.f;

        //fmt::print("X: {}\n", lightPos.x);
        //fmt::print("Y: {}\n", lightPos.y);
        //fmt::print("Z: {}\n", lightPos.z);

        sceneData.lightPos = glm::vec4(lightPos, 1.0);

        glm::vec3 sceneCenter = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, glm::vec3(0.0f, 1.0f, 0.0f));

        // perspective projection matrix for spotlight
        float fovDegrees = 45.0f;
        float fovRadians = glm::radians(fovDegrees);
        float aspect = 1.0f;
        float zNear = 1.0f;
        float zFar = 100.f;

        glm::mat4 lightProjection = glm::perspective(fovRadians, aspect, zNear, zFar);
        lightProjection[1][1] *= -1.0f;

        sceneData.lightSpaceMatrix = lightProjection * lightView;

        sceneData.ambientColor = glm::vec4(0.15f, 0.15f, 0.15f, 1.0f);
    }

    mainDrawContext.OpaqueSurfaces.clear();
    mainDrawContext.TransparentSurfaces.clear();
    loadedScenes["vulkansceneshadow"]->Draw(glm::mat4{ 1.f }, mainDrawContext);
    loadedScenes["samplescene"]->Draw(glm::mat4{ 1.f }, mainDrawContext);

    //loadedScenes["shadowtest"]->Draw(glm::mat4{ 1.f }, mainDrawContext);
    //loadedScenes["shadowtest"]->Draw(glm::translate(glm::vec3(1.f) * 5.f) * glm::mat4{ 1.f }, mainDrawContext);
    //loadedScenes["shadowtest"]->Draw(glm::translate(glm::vec3(1.f) * 10.f) * glm::mat4{ 1.f }, mainDrawContext);
    //loadedScenes["shadowtest"]->Draw(glm::translate(glm::vec3(1.f) * 15.f) * glm::mat4{ 1.f }, mainDrawContext);

    auto end{ std::chrono::system_clock::now() };
    auto elapsed{ std::chrono::duration_cast<std::chrono::milliseconds>(end - start) };

    stats.scene_update_time = elapsed.count() ;
}

void VulkanEngine::cleanup()
{
    if (_isInitialized) {
        //make sure the gpu has stopped doing its things
        vkDeviceWaitIdle(_device);
        loadedScenes.clear();

        for (int i = 0; i < FRAME_OVERLAP; i++)
        {
            destroy_buffer(_frames[i].sceneDataBuffer);

            vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);

            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_device, _frames[i]._swapchainSemaphore, nullptr);

            _frames[i]._deletionQueue.flush();
        }

        //cleanup test meshes
        for (auto& mesh : testMeshes)
        {
            destroy_buffer(mesh->meshBuffers.indexBuffer);
            destroy_buffer(mesh->meshBuffers.vertexBuffer);
        }

        //flush the global deletion queue
        _mainDeletionQueue.flush();

        for (int i = 0; i < _renderSemaphores.size(); i++)
        {
            vkDestroySemaphore(_device, _renderSemaphores[i], nullptr);
        }

        destroy_swapchain();

        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkDestroyDevice(_device, nullptr);

        vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
        vkDestroyInstance(_instance, nullptr);

        SDL_DestroyWindow(_window);
    }

    // clear engine pointer
    loadedEngine = nullptr;
}

void VulkanEngine::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView)
{
    VkRenderingAttachmentInfo colorAttachment{ vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) };
    VkRenderingInfo renderInfo{ vkinit::rendering_info(_swapchainExtent, &colorAttachment, nullptr) };

    vkCmdBeginRendering(cmd, &renderInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRendering(cmd);
}

void VulkanEngine::draw()
{
    update_scene();

    VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame()._renderFence, true, 1000000000));

    get_current_frame()._deletionQueue.flush();
    get_current_frame()._frameDescriptors.clear_pools(_device);

    VK_CHECK(vkResetFences(_device, 1, &get_current_frame()._renderFence));

    // request image from the swapchain
    uint32_t swapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, 1000000000, get_current_frame()._swapchainSemaphore, nullptr, &swapchainImageIndex));

    // this is a pointer so we can just pass by value
    VkCommandBuffer cmd{ get_current_frame()._mainCommandBuffer };

    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdBeginInfo{ vkinit::command_buffer_begin_info() };
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // start
    _drawExtent.width = std::min(_swapchainExtent.width, _drawImage.imageExtent.width) * renderScale;
    _drawExtent.height = std::min(_swapchainExtent.height, _drawImage.imageExtent.height) * renderScale;

    // background
    draw_background(cmd);

    // shadowmap
    draw_shadowmap(cmd);

    // actual scene
    vkutil::transition_image(cmd, VK_IMAGE_ASPECT_COLOR_BIT, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, VK_IMAGE_ASPECT_DEPTH_BIT, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    draw_geometry(cmd);

    // copy draw image to swapchain
    vkutil::transition_image(cmd, VK_IMAGE_ASPECT_COLOR_BIT, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transition_image(cmd, VK_IMAGE_ASPECT_COLOR_BIT, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vkutil::copy_image_to_image(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

    // draw imgui to swapchain
    vkutil::transition_image(cmd, VK_IMAGE_ASPECT_COLOR_BIT, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
    draw_imgui(cmd, _swapchainImageViews[swapchainImageIndex]);

    vkutil::transition_image(cmd, VK_IMAGE_ASPECT_COLOR_BIT, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    //end the command buffer, commands cannot be added anymore but can be executed
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdinfo{ vkinit::command_buffer_submit_info(cmd) };

    //"wait" for the swapchain to be ready
    //"signal" when finished rendering
    VkSemaphoreSubmitInfo waitInfo{ vkinit::semaphore_submit_info(0, get_current_frame()._swapchainSemaphore)};
    VkSemaphoreSubmitInfo signalInfo{ vkinit::semaphore_submit_info(0, _renderSemaphores[swapchainImageIndex])};

    VkSubmitInfo2 submit{ vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo) };

    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame()._renderFence));

    //prepare for present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.pSwapchains = &_swapchain;
    presentInfo.swapchainCount = 1;

    presentInfo.pWaitSemaphores = &_renderSemaphores[swapchainImageIndex];
    presentInfo.waitSemaphoreCount = 1;

    presentInfo.pImageIndices = &swapchainImageIndex;

    VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

    _frameNumber++;
}

void VulkanEngine::draw_background(VkCommandBuffer cmd)
{
    vkutil::transition_image(cmd, VK_IMAGE_ASPECT_COLOR_BIT, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    VkClearColorValue clearValue;
    float flash = std::abs(std::sin(_frameNumber / 120.f));
    clearValue = { { 0.0f, 0.0f, flash, 1.0f } };

    VkImageSubresourceRange clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

    //clear image
    vkCmdClearColorImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);
}

void VulkanEngine::init_shadowmap()
{
    VkExtent3D drawImageExtent{
        _shadowmapExtent.width,
        _shadowmapExtent.height,
        1
    };

    _shadowmap.imageFormat = VK_FORMAT_D32_SFLOAT;
    _shadowmap.imageExtent = drawImageExtent;

    VkImageUsageFlags depthImageUsage{};
    depthImageUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateInfo dimginfo{ vkinit::image_create_info(_shadowmap.imageFormat, depthImageUsage, _shadowmap.imageExtent) };

    VmaAllocationCreateInfo rimg_allocinfo{};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vmaCreateImage(_allocator, &dimginfo, &rimg_allocinfo, &_shadowmap.image, &_shadowmap.allocation, nullptr);

    VkImageViewCreateInfo dview_info{ vkinit::imageview_create_info(_shadowmap.imageFormat, _shadowmap.image, VK_IMAGE_ASPECT_DEPTH_BIT) };
    vkCreateImageView(_device, &dview_info, nullptr, &_shadowmap.imageView);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyImageView(_device, _shadowmap.imageView, nullptr);
        vmaDestroyImage(_allocator, _shadowmap.image, _shadowmap.allocation);
        });
}

void VulkanEngine::init_shadow_pipeline()
{
    VkShaderModule shadowVertexShader;
    if (!vkutil::load_shader_module(shader_path("shadow.vert.spv").c_str(), _device, &shadowVertexShader))
    {
        fmt::print("Error when building the shadow vertex shader module\n");
        return;
    }

    // Pipeline Layout with 0 descriptor sets, only our slim Push Constants!
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ShadowPushConstants);
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo layoutInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.setLayoutCount = 0; // ZERO set layouts bound
    layoutInfo.pSetLayouts = nullptr;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK(vkCreatePipelineLayout(_device, &layoutInfo, nullptr, &_shadowPipelineLayout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _shadowPipelineLayout;
    pipelineBuilder.set_shaders(shadowVertexShader, VK_NULL_HANDLE); // Depth only
    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();
    pipelineBuilder.disable_blending();
    pipelineBuilder.enable_depthtest(VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);

    pipelineBuilder.set_color_attachment_format(VK_FORMAT_UNDEFINED); // No color attachment
    pipelineBuilder.set_depth_format(_shadowmap.imageFormat);

    _shadowPipeline = pipelineBuilder.build_pipeline(_device);

    vkDestroyShaderModule(_device, shadowVertexShader, nullptr);

    _mainDeletionQueue.push_function([&]() {
        vkDestroyPipelineLayout(_device, _shadowPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _shadowPipeline, nullptr);
        });
}

void VulkanEngine::draw_shadowmap(VkCommandBuffer cmd)
{
    auto start{ std::chrono::system_clock::now() };

    vkutil::transition_image(cmd, VK_IMAGE_ASPECT_DEPTH_BIT, _shadowmap.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    // begin depth-only rendering pass
    VkRenderingAttachmentInfo depthAttachment{ vkinit::depth_attachment_info(_shadowmap.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) };
    depthAttachment.clearValue.depthStencil = { 1.f, 0 }; // clear depth to far plane

    VkExtent2D shadowExtent{ _shadowmapExtent.width, _shadowmapExtent.height };
    VkRenderingInfo renderInfo{ vkinit::rendering_info(shadowExtent, VK_NULL_HANDLE, &depthAttachment) };
    vkCmdBeginRendering(cmd, &renderInfo);

    // viewport and scissor setup
    VkViewport viewport{ 0.f, 0.f, (float)_shadowmapExtent.width, (float)_shadowmapExtent.height, 0.f, 1.f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ {0, 0}, shadowExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // draw opaque geometry using Programmable Vertex Pulling
    VkBuffer lastIndexBuffer{ VK_NULL_HANDLE };

    if (_shadowPipeline != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowPipeline);

        for (const RenderObject& draw : mainDrawContext.OpaqueSurfaces)
        {
            if (draw.indexBuffer != lastIndexBuffer)
            {
                vkCmdBindIndexBuffer(cmd, draw.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                lastIndexBuffer = draw.indexBuffer;
            }

            ShadowPushConstants pushConstants;
            pushConstants.vertexBufferAddress = draw.vertexBufferAddress;
            pushConstants.lightMVP = sceneData.lightSpaceMatrix * draw.transform;

            vkCmdPushConstants(cmd, _shadowPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0, sizeof(ShadowPushConstants), &pushConstants);

            vkCmdDrawIndexed(cmd, draw.indexCount, 1, draw.firstIndex, 0, 0);
        }
    }

    vkCmdEndRendering(cmd);

    vkutil::transition_image(cmd, VK_IMAGE_ASPECT_DEPTH_BIT, _shadowmap.image, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    auto end{ std::chrono::system_clock::now() };
    auto elapsed{ std::chrono::duration_cast<std::chrono::milliseconds>(end - start) };
}

void VulkanEngine::draw_geometry(VkCommandBuffer cmd)
{
    stats.drawcall_count = stats.triangle_count = 0;
    auto start{ std::chrono::system_clock::now() };

    FrameData& currentFrame = get_current_frame();
    memcpy(currentFrame.mappedSceneData, &sceneData, sizeof(GPUSceneData));

    VkDescriptorSet& globalDescriptorSet{ currentFrame.globalDescriptorSet };

    VkRenderingAttachmentInfo colorAttachment{ vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) };
    VkRenderingAttachmentInfo depthAttachment{ vkinit::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) };

    VkRenderingInfo renderInfo{ vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment) };
    vkCmdBeginRendering(cmd, &renderInfo);

    // viewport and scissor setup
    VkViewport viewport{ 0.f, 0.f, (float)_drawExtent.width, (float)_drawExtent.height, 0.f, 1.f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{ {0, 0}, _drawExtent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // collect visibility lists
    std::vector<uint32_t> opaqueIndices;
    for (int i = 0; i < mainDrawContext.OpaqueSurfaces.size(); i++) {
        if (is_visible(mainDrawContext.OpaqueSurfaces[i], sceneData.viewproj))
            opaqueIndices.push_back(i);
    }
    std::vector<uint32_t> transparentIndices;
    for (int i = 0; i < mainDrawContext.TransparentSurfaces.size(); i++) {
        if (is_visible(mainDrawContext.TransparentSurfaces[i], sceneData.viewproj))
            transparentIndices.push_back(i);
    }

    // sort by material pipeline then buffer to maximize batching efficiency
    auto sortLamb = [&](const auto& i, const auto& j, const auto& surfaceList) {
        const RenderObject& objA = surfaceList[i];
        const RenderObject& objB = surfaceList[j];
        if (objA.material->pipeline != objB.material->pipeline)
            return objA.material->pipeline < objB.material->pipeline;
        return objA.indexBuffer < objB.indexBuffer;
        };
    std::sort(opaqueIndices.begin(), opaqueIndices.end(), [&](int i, int j) { return sortLamb(i, j, mainDrawContext.OpaqueSurfaces); });
    std::sort(transparentIndices.begin(), transparentIndices.end(), [&](int i, int j) { return sortLamb(i, j, mainDrawContext.TransparentSurfaces); });

    MaterialPipeline* lastPipeline{ nullptr };
    MaterialInstance* lastMaterial{ nullptr };
    VkBuffer lastIndexBuffer{ VK_NULL_HANDLE };

    auto draw = [&](const RenderObject& draw) {
        if (draw.material != lastMaterial)
        {
            if (draw.material->pipeline != lastPipeline)
            {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.material->pipeline->pipeline);

                // set 0: static global bindless texture array set
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.material->pipeline->layout,
                    0, 1, &bindlessDescriptorSet, 0, nullptr);

                // set 1: per-frame scene data descriptor set
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, draw.material->pipeline->layout,
                    1, 1, &globalDescriptorSet, 0, nullptr);

                lastPipeline = draw.material->pipeline;
            }
            lastMaterial = draw.material;
        }

        if (draw.indexBuffer != lastIndexBuffer)
        {
            vkCmdBindIndexBuffer(cmd, draw.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            lastIndexBuffer = draw.indexBuffer;
        }

        // generic push constant population
        GPUDrawPushConstants pushConstants;
        pushConstants.vertexBufferAddress = draw.vertexBufferAddress;
        pushConstants.worldMatrix = draw.transform;
        pushConstants.materialBufferAddress = draw.material->materialBufferAddress;

        // pass generic mapping slots to shaders
        pushConstants.albedoTexIndex = draw.material->get_texture_index(0);
        pushConstants.metalRoughTexIndex = draw.material->get_texture_index(1);
        pushConstants.shadowmapTexIndex = _shadowmapTexIndex;

        vkCmdPushConstants(cmd, draw.material->pipeline->layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(GPUDrawPushConstants), &pushConstants);

        vkCmdDrawIndexed(cmd, draw.indexCount, 1, draw.firstIndex, 0, 0);

        stats.drawcall_count++;
        stats.triangle_count += draw.indexCount / 3;
        };

    for (auto idx : opaqueIndices) draw(mainDrawContext.OpaqueSurfaces[idx]);
    for (auto idx : transparentIndices) draw(mainDrawContext.TransparentSurfaces[idx]);

    vkCmdEndRendering(cmd);

    auto end{ std::chrono::system_clock::now() };
    auto elapsed{ std::chrono::duration_cast<std::chrono::milliseconds>(end - start) };
    stats.mesh_draw_time = elapsed.count();
}

void VulkanEngine::run()
{
    SDL_Event e;
    bool bQuit = false;

    // main loop
    while (!bQuit) {
        // begin clock
        auto start{ std::chrono::system_clock::now() };

        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
            // close the window when user alt-f4s or clicks the X button

            if (e.type == SDL_QUIT)
                bQuit = true;

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    stop_rendering = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    stop_rendering = false;
                }

                if (e.window.event == SDL_WINDOWEVENT_RESIZED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    resize_requested = true;
                }
            }

            mainCamera.processSDLEvent(e);
            ImGui_ImplSDL2_ProcessEvent(&e);
        }

        // do not draw if we are minimized
        if (stop_rendering) {
            // throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (resize_requested) {
            resize_swapchain();
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();

        ImGui::NewFrame();

        if (ImGui::Begin("Params"))
        {
            ImGui::SliderFloat("Render Scale", &renderScale, 0.3f, 1.f);
            ImGui::End();
        }

        if (ImGui::Begin("Stats"))
        {
            ImGui::Text("frametime %f ms", stats.frametime);
            ImGui::Text("draw time %f ms", stats.mesh_draw_time);
            ImGui::Text("scene update time %f ms", stats.scene_update_time);
            ImGui::Text("triangles %i", stats.triangle_count);
            ImGui::Text("draws %i", stats.drawcall_count);

            ImGui::End();
        }

        ImGui::Render();

        draw();

        // end clock
        auto end{ std::chrono::system_clock::now() };

        auto elapsed{ std::chrono::duration_cast<std::chrono::milliseconds>(end - start) };
        stats.frametime = elapsed.count();
    }
}

AllocatedBuffer VulkanEngine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
    VkBufferCreateInfo bufferInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = nullptr };
    bufferInfo.usage = usage;
    bufferInfo.size = allocSize;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    AllocatedBuffer newBuffer;
    VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &allocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));

    return newBuffer;
}

void VulkanEngine::destroy_buffer(const AllocatedBuffer& buffer)
{
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

std::string VulkanEngine::shader_path(std::string_view path)
{
    return "../../shaders/" + std::string(path);
}

GPUMeshBuffers VulkanEngine::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    const size_t vertexBufferSize{ vertices.size() * sizeof(Vertex) };
    const size_t indexBufferSize{ indices.size() * sizeof(uint32_t) };

    GPUMeshBuffers meshBuffer;
    meshBuffer.vertexBuffer = create_buffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                            VMA_MEMORY_USAGE_GPU_ONLY);
    VkBufferDeviceAddressInfo deviceAddressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = meshBuffer.vertexBuffer.buffer };
    meshBuffer.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAddressInfo);

    meshBuffer.indexBuffer = create_buffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                            VMA_MEMORY_USAGE_GPU_ONLY);

    // change it to CPU only so it's writable
    AllocatedBuffer staging{ create_buffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY) };

    void* data{ staging.allocation->GetMappedData() };

    // copy vertex buffer
    memcpy(data, vertices.data(), vertexBufferSize);
    // copy index buffer
    memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

    immediate_submit([&](VkCommandBuffer cmd) {
        VkBufferCopy vertexCopy{ 0 };
        vertexCopy.srcOffset = 0;
        vertexCopy.dstOffset = 0;
        vertexCopy.size = vertexBufferSize;
        vkCmdCopyBuffer(cmd, staging.buffer, meshBuffer.vertexBuffer.buffer, 1, &vertexCopy);

        VkBufferCopy indexCopy{ 0 };
        indexCopy.srcOffset = vertexBufferSize;
        indexCopy.dstOffset = 0;
        indexCopy.size = indexBufferSize;
        vkCmdCopyBuffer(cmd, staging.buffer, meshBuffer.indexBuffer.buffer, 1, &indexCopy);
    });

    destroy_buffer(staging);

    return meshBuffer;
}

void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
    glm::mat4 nodeMatrix{ topMatrix * worldTransform };

    for (auto& s : mesh->surfaces)
    {
        RenderObject def;
        def.material = s.material;

        def.indexCount = s.count;
        def.firstIndex = s.startIndex;
        def.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;

        def.transform = nodeMatrix;
        def.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;

        def.bounds = s.bounds;

        if (s.material->passType == MaterialPass::Transparent) {
            ctx.TransparentSurfaces.push_back(def);
        }
        else {
            ctx.OpaqueSurfaces.push_back(def);
        }
    }

    Node::Draw(topMatrix, ctx);
}