#include "material_system.h"
#include "vk_engine.h"
#include "vk_pipelines.h"
#include "vk_shaders.h"
#include "vk_initializers.h"

#include <fastgltf/types.hpp>
#include <iostream>

void MaterialSystem::init(VulkanEngine* engine, BindlessManager* bindlessManager, uint32_t maxMaterials) {
    _engine = engine;
    _bindlessManager = bindlessManager;
    _maxMaterials = maxMaterials;
    _materialCount = 0;

    // Allocate continuous global buffer visible to host and mapped directly
    size_t bufferSize = maxMaterials * sizeof(GPUMaterialData);
    _globalMaterialBuffer = _engine->create_buffer(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
    );
    _mappedMaterialData = static_cast<GPUMaterialData*>(_globalMaterialBuffer.info.pMappedData);
}

void MaterialSystem::cleanup() {
    VkDevice device = _engine->_device;

    _engine->destroy_buffer(_globalMaterialBuffer);
    _mappedMaterialData = nullptr;

    for (auto& [name, pipeline] : _pipelines) {
        vkDestroyPipelineLayout(device, pipeline->layout, nullptr);
        vkDestroyPipeline(device, pipeline->pipeline, nullptr);
    }
    _pipelines.clear();
    _materials.clear();
}

uint32_t MaterialSystem::allocate_material_slot(const GPUMaterialData& data) {
    if (_materialCount >= _maxMaterials) {
        std::cerr << "[MaterialSystem] Error: Global material slots exceeded capacity limits." << std::endl;
        return 0xFFFFFFFF;
    }
    uint32_t index = _materialCount++;
    update_material_slot(index, data);
    return index;
}

void MaterialSystem::update_material_slot(uint32_t slotIndex, const GPUMaterialData& data) {
    if (slotIndex < _maxMaterials && _mappedMaterialData) {
        _mappedMaterialData[slotIndex] = data;
    }
}

void MaterialSystem::create_default_materials(VulkanEngine* engine) {
    uint32_t fallbackWhiteTex = 0; // Standard fallback index on the global bindless list
    create_pbr_instance(
        "DefaultMaterial",
        glm::vec4{ 1.f },
        1.f,
        0.5f,
        fallbackWhiteTex,
        fallbackWhiteTex
    );
}

void MaterialSystem::build_default_templates() {
    // Standard opaque and transparent pipeline instances using default standard shaders
    create_pipeline("StandardOpaque", "mesh.vert.spv", "mesh.frag.spv", MaterialPass::MainColor);
    create_pipeline("StandardTransparent", "mesh.vert.spv", "mesh.frag.spv", MaterialPass::Transparent);
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

Material* MaterialSystem::write_material(
    const fastgltf::Asset& asset,
    const fastgltf::Material& gltfMat,
    const std::vector<uint32_t>& textureBindlessIndices
) {
    GPUMaterialData data{};

    // Extract basic GLTF PBR constants [3]
    data.colorFactors = glm::vec4(
        gltfMat.pbrData.baseColorFactor[0],
        gltfMat.pbrData.baseColorFactor[1],
        gltfMat.pbrData.baseColorFactor[2],
        gltfMat.pbrData.baseColorFactor[3]
    );
    data.metalRoughFactors = glm::vec4(
        gltfMat.pbrData.metallicFactor,
        gltfMat.pbrData.roughnessFactor,
        0.0f, 0.0f
    );

    // Map base textures directly into constants block [4]
    if (gltfMat.pbrData.baseColorTexture.has_value()) {
        uint32_t texIndex = gltfMat.pbrData.baseColorTexture.value().textureIndex;
        if (texIndex < textureBindlessIndices.size()) {
            data.baseColorTexIndex = textureBindlessIndices[texIndex];
        }
    }
    else
        data.baseColorTexIndex = 0;

    if (gltfMat.pbrData.metallicRoughnessTexture.has_value()) {
        uint32_t texIndex = gltfMat.pbrData.metallicRoughnessTexture.value().textureIndex;
        if (texIndex < textureBindlessIndices.size()) {
            data.metalRoughTexIndex = textureBindlessIndices[texIndex];
        }
    }
    else
        data.metalRoughTexIndex = 0;

    if (gltfMat.normalTexture.has_value()) {
        uint32_t texIndex = gltfMat.normalTexture.value().textureIndex;
        if (texIndex < textureBindlessIndices.size()) {
            data.normalTexIndex = textureBindlessIndices[texIndex];
        }
    }
    else
        data.normalTexIndex = 0;

    uint32_t globalIndex = allocate_material_slot(data);

    MaterialPass passType = (gltfMat.alphaMode == fastgltf::AlphaMode::Blend) ? MaterialPass::Transparent : MaterialPass::MainColor;

    MaterialPipeline* activePipeline = (passType == MaterialPass::Transparent)
        ? get_pipeline("StandardTransparent")
        : get_pipeline("StandardOpaque");

    return create_material_instance(
        gltfMat.name.c_str(),
        activePipeline,
        passType,
        globalIndex
    );
}

Material* MaterialSystem::create_pbr_instance(
    const std::string& instanceName,
    const glm::vec4& color,
    float metallic,
    float roughness,
    uint32_t baseColorTexBindless,
    uint32_t metalRoughTexBindless
) {
    GPUMaterialData data{};
    data.colorFactors = color;
    data.metalRoughFactors = glm::vec4(metallic, roughness, 0.0f, 0.0f);
    data.baseColorTexIndex = baseColorTexBindless;
    data.metalRoughTexIndex = metalRoughTexBindless;

    uint32_t globalIndex = allocate_material_slot(data);
    MaterialPipeline* activePipeline = get_pipeline("StandardOpaque");

    return create_material_instance(
        instanceName,
        activePipeline,
        MaterialPass::MainColor,
        globalIndex
    );
}

Material* MaterialSystem::create_material_instance(
    const std::string& name,
    MaterialPipeline* pipeline,
    MaterialPass passType,
    uint32_t globalIndex
) {
    auto material = std::make_unique<Material>();
    material->name = name;
    material->pipeline = pipeline;
    material->passType = passType;
    material->globalMaterialIndex = globalIndex;

    // Calculate absolute GPU address offset inside the global storage buffer
    VkBufferDeviceAddressInfo addressInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = _globalMaterialBuffer.buffer
    };
    VkDeviceAddress baseAddress = vkGetBufferDeviceAddress(_engine->_device, &addressInfo);
    material->materialBufferAddress = baseAddress + (globalIndex * sizeof(GPUMaterialData));

    // Populate the texture index array to support legacy shader rendering structures
    GPUMaterialData* data = get_material_data_ptr(globalIndex);
    if (data) {
        material->textureIndices = {
            data->baseColorTexIndex,
            data->metalRoughTexIndex,
            data->normalTexIndex
        };
    }

    auto* ptr = material.get();
    _materials[name] = std::move(material);
    return ptr;
}

Material* MaterialSystem::get_material_instance(const std::string& name) {
    auto it = _materials.find(name);
    return it != _materials.end() ? it->second.get() : nullptr;
}