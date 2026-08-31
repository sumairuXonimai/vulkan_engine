#include <vk_pipelines.h>
#include <fstream>
#include <vk_initializers.h>

#include <vk_shaders.h>

void PipelineBuilder::clear()
{
	_inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	_rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	_colorBlendAttachment = {};
	_multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	_pipelineLayout = {};
	_depthStencil = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	_renderInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };

	_shaderStages.clear();
}

VkPipeline PipelineBuilder::build_pipeline(VkDevice device)
{
	// completely clear VertexInputStateCreateInfo, as we have no need for it
	VkPipelineVertexInputStateCreateInfo _vertexInputInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .pNext = nullptr };

	VkPipelineViewportStateCreateInfo viewportState{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .pNext = nullptr };
	viewportState.scissorCount = 1;
	viewportState.viewportCount = 1;

	// setup dummy color blending. We arent using transparent objects yet
	// the blending is just "no blend", but we do write to the color attachment
	VkPipelineColorBlendStateCreateInfo colorBlending{ .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .pNext = nullptr };
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &_colorBlendAttachment;

	// build the actual pipeline
	VkGraphicsPipelineCreateInfo pipelineInfo{ .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineInfo.pNext = &_renderInfo;

	pipelineInfo.stageCount = (uint32_t)_shaderStages.size();
	pipelineInfo.pStages = _shaderStages.data();
	pipelineInfo.pVertexInputState = &_vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &_inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &_rasterizer;
	pipelineInfo.pMultisampleState = &_multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDepthStencilState = &_depthStencil;
	pipelineInfo.layout = _pipelineLayout;

	// setting up dynamic state
	VkDynamicState state[]{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .pNext = nullptr };
	dynamicInfo.pDynamicStates = state;
	dynamicInfo.dynamicStateCount = 2;

	pipelineInfo.pDynamicState = &dynamicInfo;

	VkPipeline newPipeline;
	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS)
	{
		fmt::println("failed to create pipeline\n");
		return VK_NULL_HANDLE;
	}
	return newPipeline;
}

void PipelineBuilder::set_shaders(VkShaderModule vertexShader, VkShaderModule fragmentShader)
{
	_shaderStages.clear();

	if (vertexShader != VK_NULL_HANDLE) _shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT, vertexShader));
	if (fragmentShader != VK_NULL_HANDLE) _shaderStages.push_back(vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader));
}

void PipelineBuilder::set_shaders(ShaderEffect* effect)
{
	_shaderStages.clear();
	effect->fill_stages(_shaderStages);

	_pipelineLayout = effect->builtLayout;
}

void PipelineBuilder::set_input_topology(VkPrimitiveTopology topology)
{
	_inputAssembly.topology = topology;

	_inputAssembly.primitiveRestartEnable = VK_FALSE;
}

void PipelineBuilder::set_polygon_mode(VkPolygonMode mode)
{
	_rasterizer.polygonMode = mode;
	_rasterizer.lineWidth = 1.f;
}

void PipelineBuilder::set_cull_mode(VkCullModeFlags cullMode, VkFrontFace frontFace)
{
	_rasterizer.cullMode = cullMode;
	_rasterizer.frontFace = frontFace;
}

void PipelineBuilder::set_multisampling_none()
{
	_multisampling.sampleShadingEnable = VK_FALSE;
	// multisampling defaulted to no multisampling (1 sample per pixel)
	_multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	_multisampling.minSampleShading = 1.0f;
	_multisampling.pSampleMask = nullptr;
	// no alpha to coverage either
	_multisampling.alphaToCoverageEnable = VK_FALSE;
	_multisampling.alphaToOneEnable = VK_FALSE;
}

void PipelineBuilder::disable_blending()
{
	_colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	_colorBlendAttachment.blendEnable = VK_FALSE;
}

void PipelineBuilder::set_color_attachment_format(VkFormat format)
{
	if (format == VK_FORMAT_UNDEFINED) return;

	_colorAttachmentFormat = format;

	_renderInfo.colorAttachmentCount = 1;
	_renderInfo.pColorAttachmentFormats = &_colorAttachmentFormat;
}

void PipelineBuilder::set_depth_format(VkFormat format)
{
	_renderInfo.depthAttachmentFormat = format;
}

void PipelineBuilder::disable_depthtest()
{
	_depthStencil.depthTestEnable = VK_FALSE;
	_depthStencil.depthWriteEnable = VK_FALSE;
	_depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER;
	_depthStencil.depthBoundsTestEnable = VK_FALSE;
	_depthStencil.stencilTestEnable = VK_FALSE;
	_depthStencil.front = {};
	_depthStencil.back = {};
	_depthStencil.minDepthBounds = 0.f;
	_depthStencil.maxDepthBounds = 1.f;
}

void PipelineBuilder::enable_depthtest(bool depthWriteEnable, VkCompareOp op)
{
	_depthStencil.depthTestEnable = VK_TRUE;
	_depthStencil.depthWriteEnable = depthWriteEnable;
	_depthStencil.depthCompareOp = op;
	_depthStencil.depthBoundsTestEnable = VK_FALSE;
	_depthStencil.stencilTestEnable = VK_FALSE;
	_depthStencil.front = {};
	_depthStencil.back = {};
	_depthStencil.minDepthBounds = 0.f;
	_depthStencil.maxDepthBounds = 1.f;
}

void PipelineBuilder::enable_blending_additive()
{
	_colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	_colorBlendAttachment.blendEnable = VK_TRUE;
	_colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	_colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	_colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	_colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	_colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	_colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
}

void PipelineBuilder::enable_blending_alphablend()
{
	_colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	_colorBlendAttachment.blendEnable = VK_TRUE;
	_colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	_colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	_colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	_colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	_colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	_colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
}

bool vkutil::load_shader_module(const char* filePath, VkDevice device, VkShaderModule* outShaderModule)
{
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open())
		return false;

	size_t fileSize{ (size_t)file.tellg() };

	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	file.seekg(0);

	file.read((char*)(buffer.data()), fileSize);

	file.close();

	VkShaderModuleCreateInfo shader_info{ .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .pNext = nullptr };
	shader_info.codeSize = buffer.size() * sizeof(uint32_t);
	shader_info.pCode = buffer.data();

	if (vkCreateShaderModule(device, &shader_info, nullptr, outShaderModule) != VK_SUCCESS)
		return false;

	return true;
}