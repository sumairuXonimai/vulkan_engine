#pragma once

#include <vk_types.h>

class CullingSystem {
public:
    const std::vector<const RenderObject*>& cullScene(const std::vector<RenderObject>& allObjects, const glm::mat4& viewProj) {
        viewproj = viewProj;

        visibleObjects.clear();

        for (const RenderObject& obj : allObjects) {
            if (is_visible(obj))
                visibleObjects.push_back(&obj);
        }

        return visibleObjects;
    }

private:
    glm::mat4 viewproj;
    std::vector<const RenderObject*> visibleObjects;

    bool is_visible(const RenderObject& obj)
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
};