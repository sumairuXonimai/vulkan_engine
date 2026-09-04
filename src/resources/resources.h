#pragma once

#include <string>
#include <unordered_map>
#include <typeindex>

class Resource
{
public:
    explicit Resource(const std::string& id) : resourceId(id) {}
    virtual ~Resource() = default;

    const std::string& GetId() const { return resourceId; }
    bool IsLoaded() const { return loaded; }

    bool Load() {
        loaded = doLoad();
        return loaded;
    }

    void Unload() {
        doUnload();
        loaded = false;
    }

private:
	std::string resourceId{};
	bool loaded{ false };

protected:
    virtual bool doLoad() = 0;
    virtual bool doUnload() = 0;
};

class ResourceManager
{
public:
    template<typename T>
    ResourceHandle<T> Load(const std::string& resourceId)
    {
        static_assert(std::is_base_of<Resource, T>::value, "T must derive from Resource");

        auto& typeResources[std::type_index(typeid(T))];
        auto resourceIt{ typeResources.find(resourceId) };

        if (resourceIt != resources.end())
        {
            resourceIt->second.refCount++;
            return ResourceHandle<T>(resourceId, this);
        }

        auto resource{ std::make_shared<T>(resourceId) };
        if (!resource->Load())
        {
            return ResourceHandle<T>();
        }

        typeResources[resourceId].resource = resource;
        typeResources[resourceId] = 1;
        return ResourceHandle<T>(resourceId, this);
    }

    template<typename T>
    T* GetResource(const std::string& resourceId) {
        auto& typeResources = resources[std::type_index(typeid(T))];
        auto it = typeResources.find(resourceId);

        if (it != typeResources.end()) {
            return static_cast<T*>(it->second.get());
        }

        return nullptr;
    }

    template<typename T>
    bool HasResource(const std::string& resourceId) {
        auto resourceIt = resources.find(std::type_index(typeid(T)));
        return resourceIt != resources.end();
    }

    void Unload(const std::string& resourceId)
    {
        auto it{ refCounts.find(resourceId) };
        if (it != refCounts.end())
        {
            it->second--;

            if (it->second <= 0)
            {
                for (auto& [type, typeResources] : resources)
                {
                    auto resourceIt{ typeResources.find(resourceId) };
                    if (resourceIt != typeResources.end())
                    {
                        resourceIt->second->Unload();
                        typeResources.erase(resourceIt);
                        break;
                    }
                }

                refCounts.erase(it);
            }
        }
    }

    void UnloadAll()
    {
        for (auto& [type, typeResources] : resources)
        {
            for (auto& [id, resource] : typeResources)
                resource->Unload();
            typeResources.clear();
        }
        resources.clear();
    }

private:

    std::unordered_map<std::type_index, std::unordered_map<std::string, std::shared_ptr<Resource>>> resources;

    std::unordered_map<std::string, int> refCounts;
};

template<typename T>
class ResourceHandle
{
public:
    ResourceHandle(const std::string& id, ResourceManager* manager) : resourceId(id), resourceManager(manager) {}

    T* get() const
    {
        if (!resourceManager) return nullptr;
        return resourceManager->LoadResource(resourceId);
    }

    bool isValid() const
    {
        return resourceManager && resourceManager->HasResource<T>(resourceId);
    }

    const std::string& GetId() const {
        return resourceId;
    }

    // Convenience operators
    T* operator->() const {
        return Get();
    }

    T& operator*() const {
        return *Get();
    }

    operator bool() const {
        return IsValid();
    }
    
private:
    std::string resourceId;
    ResourceManager* resourceManager;
};