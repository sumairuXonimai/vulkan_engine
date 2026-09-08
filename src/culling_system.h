#pragma once

#include <vk_types.h>

class VulkanEngine;

class CullingSystem {
public:
    void init(VulkanEngine* engineP)
    {
        engine = engineP;

        build_pipelines();
    }

    // RenderObject but only transform and bounds
    struct CullingObject
    {
        glm::mat4 transform;
        Bounds bounds;
    };

    struct CullingPushConstant
    {
        glm::mat4 viewproj;
        VkDeviceAddress cullingBufferAddress;
        VkDeviceAddress visibleIndicesAddress;
        VkDeviceAddress counterBufferAddress;
        uint32_t maxObjects;
    };

    std::vector<const RenderObject*> cullScene(const std::vector<RenderObject>& allObjects, const glm::mat4& viewProj);

private:
    VulkanEngine* engine;

    VkPipeline cullingPipeline;
    VkPipelineLayout cullingPipelineLayout;
    uint32_t* indices;

    void build_pipelines();

    std::vector<const RenderObject*> visibleObjects;

    bool is_visible(const RenderObject& obj, const glm::mat4& viewproj)
    {
        // 1. Extract and normalize frustum planes
        glm::vec4 row0 = glm::vec4(viewproj[0][0], viewproj[1][0], viewproj[2][0], viewproj[3][0]);
        glm::vec4 row1 = glm::vec4(viewproj[0][1], viewproj[1][1], viewproj[2][1], viewproj[3][1]);
        glm::vec4 row2 = glm::vec4(viewproj[0][2], viewproj[1][2], viewproj[2][2], viewproj[3][2]);
        glm::vec4 row3 = glm::vec4(viewproj[0][3], viewproj[1][3], viewproj[2][3], viewproj[3][3]);

        glm::vec4 planes[6];
        planes[0] = row3 + row0; // Left
        planes[1] = row3 - row0; // Right
        planes[2] = row3 + row1; // Bottom
        planes[3] = row3 - row1; // Top
        planes[4] = row2;        // Near
        planes[5] = row3 - row2; // Far

        for (int i = 0; i < 6; i++)
        {
            float len = glm::length(glm::vec3(planes[i]));
            planes[i] /= len;
        }

        // 2. Transform the sphere center to world space
        glm::vec4 global_center = obj.transform * glm::vec4(glm::vec3(obj.bounds.origin_radius), 1.0);

        // Extract the uniform matrix scale to correctly scale the radius in world space
        float matrix_scale = glm::length(glm::vec3(obj.transform[0]));
        float global_radius = obj.bounds.origin_radius.w * matrix_scale;

        // 3. Test the sphere against all 6 planes
        for (int i = 0; i < 6; i++)
        {
            if (dot(planes[i], global_center) < -global_radius)
            {
                return false; // Culled (the sphere is entirely behind this plane)
            }
        }

        return true; // Visible
    }
};