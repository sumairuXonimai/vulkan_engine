#include "material_system.h"
#include "vk_engine.h"
#include "vk_pipelines.h"
#include "vk_shaders.h"
#include "vk_initializers.h"

#include <fastgltf/types.hpp> 

// Concrete Subclass Definition
class PBRMetallicRoughness : public MaterialType {
private:
    std::string _vertexShader;
    std::string _fragmentShader;
    std::string _prefixName;
    
public:
    struct MaterialConstants {
        glm::vec4 colorFactors;
        glm::vec4 metalRoughFactors;
    };

    PBRMetallicRoughness(std::string vertex, std::string fragment, std::string prefix)
        : _vertexShader(std::move(vertex))
        , _fragmentShader(std::move(fragment))
        , _prefixName(std::move(prefix)) {
    }

    MaterialPipeline* opaquePipeline{ nullptr };
    MaterialPipeline* transparentPipeline{ nullptr };

    void init(VulkanEngine* engine) override {
        opaquePipeline = engine->_materialSystem.create_pipeline(
            _prefixName + "Opaque", _vertexShader, _fragmentShader, MaterialPass::MainColor);
        transparentPipeline = engine->_materialSystem.create_pipeline(
            _prefixName + "Transparent", _vertexShader, _fragmentShader, MaterialPass::Transparent);
    }

    size_t get_constant_buffer_size() const override {
        return sizeof(MaterialConstants);
    }

    MaterialInstance* write_material(
        VulkanEngine* engine,
        const fastgltf::Asset& asset,
        const fastgltf::Material& gltfMat,
        VkDeviceAddress gpuBufferAddress,
        void* cpuBufferPointer,
        const std::vector<AllocatedImage>& images,
        const std::vector<VkSampler>& samplers
    ) override {
        MaterialConstants* constants = static_cast<MaterialConstants*>(cpuBufferPointer);
        constants->colorFactors = glm::vec4(
            gltfMat.pbrData.baseColorFactor[0],
            gltfMat.pbrData.baseColorFactor[1],
            gltfMat.pbrData.baseColorFactor[2],
            gltfMat.pbrData.baseColorFactor[3]
        );
        constants->metalRoughFactors = glm::vec4(
            gltfMat.pbrData.metallicFactor,
            gltfMat.pbrData.roughnessFactor,
            0.0f, 0.0f
        );

        MaterialPass passType = (gltfMat.alphaMode == fastgltf::AlphaMode::Blend) ? MaterialPass::Transparent : MaterialPass::MainColor;
        MaterialPipeline* activePipeline = (passType == MaterialPass::Transparent) ? transparentPipeline : opaquePipeline;

        AllocatedImage colorImage = engine->_whiteImage;
        VkSampler colorSampler = engine->_defaultSamplerLinear;

        if (gltfMat.pbrData.baseColorTexture.has_value()) {
            auto& texInfo = gltfMat.pbrData.baseColorTexture.value();
            if (texInfo.textureIndex < asset.textures.size()) {
                auto& texture = asset.textures[texInfo.textureIndex];

                // Safe image resolution
                if (texture.imageIndex.has_value() && texture.imageIndex.value() < images.size()) {
                    colorImage = images[texture.imageIndex.value()];
                }

                // Safe sampler resolution (use fallback if sampler index is omitted or invalid)
                if (texture.samplerIndex.has_value() && texture.samplerIndex.value() < samplers.size()) {
                    colorSampler = samplers[texture.samplerIndex.value()];
                }
            }
        }

        // Resolve metallic-roughness texture safely
        AllocatedImage metalRoughImage = engine->_whiteImage;
        VkSampler metalRoughSampler = engine->_defaultSamplerLinear;

        if (gltfMat.pbrData.metallicRoughnessTexture.has_value()) {
            auto& texInfo = gltfMat.pbrData.metallicRoughnessTexture.value();
            if (texInfo.textureIndex < asset.textures.size()) {
                auto& texture = asset.textures[texInfo.textureIndex];

                // Safe image resolution
                if (texture.imageIndex.has_value() && texture.imageIndex.value() < images.size()) {
                    metalRoughImage = images[texture.imageIndex.value()];
                }

                // Safe sampler resolution
                if (texture.samplerIndex.has_value() && texture.samplerIndex.value() < samplers.size()) {
                    metalRoughSampler = samplers[texture.samplerIndex.value()];
                }
            }
        }


        std::vector<std::pair<VkImageView, VkSampler>> textureSlots;
        textureSlots.push_back({ colorImage.imageView, colorSampler });
        textureSlots.push_back({ metalRoughImage.imageView, metalRoughSampler });

        return engine->_materialSystem.create_material_instance(
            gltfMat.name.c_str(),
            activePipeline,
            passType,
            gpuBufferAddress,
            textureSlots
        );
    }
};

// Implementations of MaterialSystem
void MaterialSystem::init(VulkanEngine* engine, BindlessManager* bindlessManager) {
    _engine = engine;
    _bindlessManager = bindlessManager;
}

void MaterialSystem::cleanup() {
    VkDevice device = _engine->_device;

    // Destroy all manually created instance buffers
    for (auto& buffer : _allocatedBuffers) {
        _engine->destroy_buffer(buffer);
    }
    _allocatedBuffers.clear();

    for (auto& [name, pipeline] : _pipelines) {
        vkDestroyPipelineLayout(device, pipeline->layout, nullptr);
        vkDestroyPipeline(device, pipeline->pipeline, nullptr);
    }
    _pipelines.clear();
    _materials.clear();
}

void MaterialSystem::create_default_materials(VulkanEngine* engine) {
    create_pbr_instance(
        "DefaultMaterial",
        "PBRMetallicRoughness",
        glm::vec4{ 1.f },
        1.f,
        0.5f,
        engine->_whiteImage.imageView,
        engine->_defaultSamplerLinear
    );
}

void MaterialSystem::build_default_templates() {
    // Default PBR with standard shaders
    register_material_type("PBRMetallicRoughness",
        std::make_unique<PBRMetallicRoughness>("mesh.vert.spv", "mesh.frag.spv", "Standard"));
}

void MaterialSystem::register_material_type(const std::string& name, std::unique_ptr<MaterialType> type) {
    type->init(_engine);
    _types[name] = std::move(type);
}

MaterialType* MaterialSystem::get_material_type(const std::string& name) {
    auto it = _types.find(name);
    return it != _types.end() ? it->second.get() : nullptr;
}

MaterialPipeline* MaterialSystem::create_pipeline(
    const std::string& name,
    const std::string& vertexShader,
    const std::string& fragmentShader,
    MaterialPass passType,
    bool depthWrite,
    VkCompareOp depthCompareOp
) {
    auto it = _pipelines.find(name);
    if (it != _pipelines.end()) {
        return it->second.get();
    }

    VkDevice device = _engine->_device;

    ShaderEffect* effect = new ShaderEffect();
    effect->add_stage(_engine->_shaderCache.get_shader(_engine->shader_path(vertexShader)), VK_SHADER_STAGE_VERTEX_BIT);
    effect->add_stage(_engine->_shaderCache.get_shader(_engine->shader_path(fragmentShader)), VK_SHADER_STAGE_FRAGMENT_BIT);
    effect->reflect_layout(device, nullptr, 0);

    // Universal descriptor set layouts for bindless rendering
    VkDescriptorSetLayout layouts[] = {
        _bindlessManager->get_layout(),
        _engine->_gpuSceneDataDescriptorLayout
    };

    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(GPUDrawPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{ vkinit::pipeline_layout_create_info() };
    pipelineLayoutInfo.pPushConstantRanges = &bufferRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pSetLayouts = layouts;
    pipelineLayoutInfo.setLayoutCount = 2;

    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &layout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder.set_shaders(effect);
    pipelineBuilder._pipelineLayout = layout;

    pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.set_multisampling_none();

    if (passType == MaterialPass::Transparent) {
        pipelineBuilder.enable_blending_additive();
    }
    else {
        pipelineBuilder.disable_blending();
    }

    pipelineBuilder.enable_depthtest(depthWrite, depthCompareOp);
    pipelineBuilder.set_color_attachment_format(_engine->_drawImage.imageFormat);
    pipelineBuilder.set_depth_format(_engine->_depthImage.imageFormat);

    VkPipeline pipeline = pipelineBuilder.build_pipeline(device);
    delete effect;

    auto newPipeline = std::make_unique<MaterialPipeline>();
    newPipeline->pipeline = pipeline;
    newPipeline->layout = layout;

    auto* ptr = newPipeline.get();
    _pipelines[name] = std::move(newPipeline);
    return ptr;
}

MaterialPipeline* MaterialSystem::get_pipeline(const std::string& name) {
    auto it = _pipelines.find(name);
    return it != _pipelines.end() ? it->second.get() : nullptr;
}

void MaterialSystem::register_pbr_material(
    const std::string& name, 
    const std::string& vertexShader, 
    const std::string& fragmentShader, 
    const std::string& prefixName
) {
    register_material_type(name, std::make_unique<PBRMetallicRoughness>(vertexShader, fragmentShader, prefixName));
}

MaterialInstance* MaterialSystem::create_pbr_instance(
    const std::string& instanceName,
    const std::string& templateTypeName,
    const glm::vec4& color,
    float metallic,
    float roughness,
    VkImageView textureView,
    VkSampler sampler
) {
    MaterialType* handler = get_material_type(templateTypeName);
    if (!handler) return nullptr;

    // Allocate a dedicated constants buffer for this instance
    AllocatedBuffer buffer = _engine->create_buffer(
        handler->get_constant_buffer_size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
    );
    _allocatedBuffers.push_back(buffer); // Save to track and delete on cleanup()

    VkBufferDeviceAddressInfo addressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buffer.buffer };
    VkDeviceAddress gpuAddress = vkGetBufferDeviceAddress(_engine->_device, &addressInfo);
    void* cpuAddress = buffer.info.pMappedData;

    // Write your custom constants
    PBRMetallicRoughness::MaterialConstants* constants =
        static_cast<PBRMetallicRoughness::MaterialConstants*>(cpuAddress);
    constants->colorFactors = color;
    constants->metalRoughFactors = glm::vec4(metallic, roughness, 0.0f, 0.0f);

    // Get the pipeline pointer from our PBR handler
    PBRMetallicRoughness* pbr = static_cast<PBRMetallicRoughness*>(handler);

    // Create and return the instance
    return create_material_instance(
        instanceName,
        pbr->opaquePipeline,
        MaterialPass::MainColor,
        gpuAddress,
        {
            { textureView, sampler },
            { _engine->_whiteImage.imageView, _engine->_defaultSamplerLinear }
        }
    );
}

MaterialInstance* MaterialSystem::create_material_instance(
    const std::string& name,
    MaterialPipeline* pipeline,
    MaterialPass passType,
    VkDeviceAddress bufferAddress,
    const std::vector<std::pair<VkImageView, VkSampler>>& textures
) {
    auto material = std::make_unique<MaterialInstance>();
    material->name = name;
    material->pipeline = pipeline;
    material->passType = passType;
    material->materialBufferAddress = bufferAddress;

    for (const auto& [view, sampler] : textures) {
        uint32_t bindlessIndex = _bindlessManager->register_texture(view, sampler);
        material->textureIndices.push_back(bindlessIndex);
    }

    auto* ptr = material.get();
    _materials[name] = std::move(material);
    return ptr;
}

MaterialInstance* MaterialSystem::get_material_instance(const std::string& name) {
    auto it = _materials.find(name);
    return it != _materials.end() ? it->second.get() : nullptr;
}