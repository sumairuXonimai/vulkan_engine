#pragma once

#include <vk_types.h>
#include <vk_initializers.h>

#include <string>
#include <unordered_set>
#include <stdexcept>

// Forward declarations
class VulkanEngine;

// Render target class
class RenderTarget {
private:
    AllocatedImage image;
    VulkanEngine* engine;

    uint32_t width{ 512 };
    uint32_t height{ 512 };

public:
    RenderTarget(VulkanEngine* engine_p, uint32_t w, uint32_t h, bool isDepth = false) : width(w), height(h), engine(engine_p) {
        if (!isDepth)
            createColorResources();
        else
            createDepthResources();
    }

    ~RenderTarget();

    AllocatedImage* getResource() { return &image; }

    VkImage getImage() const { return image.image; }
    VkImageView getImageView() const { return image.imageView; }
    VkFormat getFormat() const { return image.imageFormat; }

    uint32_t getWidth() const { return image.imageExtent.width; }
    uint32_t getHeight() const { return image.imageExtent.height; }

private:
    void createColorResources();

    void createDepthResources();
};

// Base render pass class
class RenderPass {
private:
    std::string name{};
    std::vector<std::string> dependencies{};
    bool enabled{ true };

protected:
    std::vector<RenderTarget*> targets{}; // for multiple render targets

public:
    explicit RenderPass(const std::string& passName) : name(passName) {}
    virtual ~RenderPass() {
        dependencies.clear();

        for (RenderTarget* target : targets)
            delete target;
    }

    const std::string& GetName() const { return name; }

    void addDependency(const std::string& dependency) {
        dependencies.push_back(dependency);
    }

    const std::vector<std::string>& getDependencies() const {
        return dependencies;
    }

    void setEnabled(bool isEnabled) {
        enabled = isEnabled;
    }

    bool isEnabled() const {
        return enabled;
    }

    virtual void execute(VkCommandBuffer& commandBuffer, const std::vector<const RenderObject*>& visibleObjects = {}) {
        if (!enabled) return;

        beginPass(commandBuffer);
        render(commandBuffer, visibleObjects);
        endPass(commandBuffer);
    }

protected:
    // With dynamic rendering, BeginPass typically calls vkCmdBeginRendering
    // instead of vkCmdBeginRenderPass
    virtual void beginPass(VkCommandBuffer& commandBuffer) = 0;
    virtual void render(VkCommandBuffer& commandBuffer, const std::vector<const RenderObject*>& visibleObjects) = 0;
    // With dynamic rendering, EndPass typically calls vkCmdEndRendering
    // instead of vkCmdEndRenderPass
    virtual void endPass(VkCommandBuffer& commandBuffer) = 0;
};

// Render pass manager
class RenderPassManager {
private:
    std::unordered_map<std::string, std::unique_ptr<RenderPass>> renderPasses;
    std::vector<RenderPass*> sortedPasses;
    bool dirty = true;

public:
    template<typename T, typename... Args>
    T* AddRenderPass(const std::string& name, Args&&... args) {
        static_assert(std::is_base_of<RenderPass, T>::value, "T must derive from RenderPass");

        auto it = renderPasses.find(name);
        if (it != renderPasses.end()) {
            return dynamic_cast<T*>(it->second.get());
        }

        auto pass = std::make_unique<T>(name, std::forward<Args>(args)...);
        T* passPtr = pass.get();
        renderPasses[name] = std::move(pass);
        dirty = true;

        return passPtr;
    }

    RenderPass* GetRenderPass(const std::string& name) {
        auto it = renderPasses.find(name);
        if (it != renderPasses.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    void RegisterPass(std::unique_ptr<RenderPass> pass) {
        if (!pass) {
            return;
        }

        const std::string& name = pass->GetName();

        if (renderPasses.find(name) == renderPasses.end()) {
            renderPasses[name] = std::move(pass);
        }
    }

    void RemoveRenderPass(const std::string& name) {
        auto it = renderPasses.find(name);
        if (it != renderPasses.end()) {
            renderPasses.erase(it);
            dirty = true;
        }
    }

    void Execute(VkCommandBuffer& commandBuffer) {
        if (dirty) {
            SortPasses();
            dirty = false;
        }

        for (auto pass : sortedPasses) {
            pass->execute(commandBuffer);
        }
    }

    void Clear() {
        renderPasses.clear();
    }

private:
    void SortPasses() {
        // Topologically sort render passes based on dependencies
        sortedPasses.clear();

        // Create a copy of render passes for sorting
        std::unordered_map<std::string, RenderPass*> passMap;
        for (const auto& [name, pass] : renderPasses) {
            passMap[name] = pass.get();
        }

        // Perform topological sort
        std::unordered_set<std::string> visited;
        std::unordered_set<std::string> visiting;

        for (const auto& [name, pass] : passMap) {
            if (visited.find(name) == visited.end()) {
                TopologicalSort(name, passMap, visited, visiting);
            }
        }
    }

    void TopologicalSort(const std::string& name,
        const std::unordered_map<std::string, RenderPass*>& passMap,
        std::unordered_set<std::string>& visited,
        std::unordered_set<std::string>& visiting) {
        visiting.insert(name);

        auto pass = passMap.at(name);
        for (const auto& dep : pass->getDependencies()) {
            if (visited.find(dep) == visited.end()) {
                if (visiting.find(dep) != visiting.end()) {
                    // Circular dependency detected
                    throw std::runtime_error("Circular dependency detected in render passes");
                }
                TopologicalSort(dep, passMap, visited, visiting);
            }
        }

        visiting.erase(name);
        visited.insert(name);
        sortedPasses.push_back(pass);
    }
};