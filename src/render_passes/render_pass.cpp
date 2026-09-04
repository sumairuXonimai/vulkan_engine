#include <render_pass.h>

#include <vk_engine.h>

RenderTarget::~RenderTarget()
{
    if (image.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(engine->_device, image.imageView, nullptr);
    }
    if (image.image != VK_NULL_HANDLE) {
        vmaDestroyImage(engine->_allocator, image.image, image.allocation);
    }
}

void RenderTarget::createColorResources() {
    VkExtent3D drawImageExtent{
    width,
    height,
    1
    };

    image.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    image.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsage{};
    drawImageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    drawImageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    drawImageUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
    drawImageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkImageCreateInfo rimgInfo{ vkinit::image_create_info(image.imageFormat, drawImageUsage, image.imageExtent) };

    VmaAllocationCreateInfo rimg_allocinfo{};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vmaCreateImage(engine->_allocator, &rimgInfo, &rimg_allocinfo, &image.image, &image.allocation, nullptr);

    VkImageViewCreateInfo rview_info{ vkinit::imageview_create_info(image.imageFormat, image.image, VK_IMAGE_ASPECT_COLOR_BIT) };
    vkCreateImageView(engine->_device, &rview_info, nullptr, &image.imageView);
}

void RenderTarget::createDepthResources() {
    VkExtent3D drawImageExtent{
    width,
    height,
    1
    };

    image.imageFormat = VK_FORMAT_D32_SFLOAT;
    image.imageExtent = drawImageExtent;

    VkImageUsageFlags depthImageUsage{};
    depthImageUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateInfo dimginfo{ vkinit::image_create_info(image.imageFormat, depthImageUsage, image.imageExtent) };

    VmaAllocationCreateInfo rimg_allocinfo{};
    rimg_allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rimg_allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vmaCreateImage(engine->_allocator, &dimginfo, &rimg_allocinfo, &image.image, &image.allocation, nullptr);

    VkImageViewCreateInfo dview_info{ vkinit::imageview_create_info(image.imageFormat, image.image, VK_IMAGE_ASPECT_DEPTH_BIT) };
    vkCreateImageView(engine->_device, &dview_info, nullptr, &image.imageView);
}