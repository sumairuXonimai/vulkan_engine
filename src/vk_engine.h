// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vk_descriptors.h>
#include "vk_loader.h"
#include <camera.h>
#include <vk_shaders.h>

#include "bindless_manager.h"
#include "material_system.h"
#include "culling_system.h"
#include "render_pass.h"

class GeometryPass;
class ShadowPass;

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function)
	{
		deletors.push_back(function);
	}

	void flush()
	{
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
		{
			(*it)();
		}

		deletors.clear();
	}
};

struct FrameData
{
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	VkSemaphore _swapchainSemaphore;
	VkFence _renderFence;

	DeletionQueue _deletionQueue;
	DescriptorAllocatorGrowable _frameDescriptors;

	// PERSISTENT RESOURCES (Created once, reused forever)
	AllocatedBuffer sceneDataBuffer;
	VkDescriptorSet globalDescriptorSet;
	void* mappedSceneData{ nullptr }; // Direct pointer to mapped GPU memory
};

constexpr unsigned int FRAME_OVERLAP{ 2 };

struct GPUSceneData
{
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewproj;
	glm::vec4 cameraPos;
	glm::vec4 ambientColor;
	glm::vec4 sunlightColor;
	glm::vec4 sunlightDirection;
	glm::vec4 lightPos;
	glm::mat4 lightSpaceMatrix;
};

struct MeshNode : public Node
{
	std::shared_ptr<MeshAsset> mesh;

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;
};

struct DrawContext
{
	std::vector<RenderObject> OpaqueSurfaces;
	std::vector<RenderObject> TransparentSurfaces;
};

struct EngineStats
{
	float frametime;
	int triangle_count;
	int drawcall_count;
	float scene_update_time;
	float mesh_draw_time;
};

class VulkanEngine {
public:

	bool _isInitialized{ false };
	int _frameNumber {0};
	bool stop_rendering{ false };
	VkExtent2D _windowExtent{ 1280 , 720 };

	struct SDL_Window* _window{ nullptr };
	bool resize_requested{ false };

	static VulkanEngine& Get();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();

	// initialization
	VkInstance _instance; // Vulkan library handle
	VkDebugUtilsMessengerEXT _debug_messenger; // Vulkan debug output handle
	VkPhysicalDevice _chosenGPU; // GPU chosen as the default device
	VkDevice _device; // Vulkan device for commands
	VkSurfaceKHR _surface; // Vulkan window surface

	// swapchain
	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;

	std::vector<VkImage> _swapchainImages;
	std::vector<VkSemaphore> _renderSemaphores;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent;

	// commands
	FrameData _frames[FRAME_OVERLAP];

	FrameData& get_current_frame() { return _frames[_frameNumber % FRAME_OVERLAP]; }

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	// deletion queue
	DeletionQueue _mainDeletionQueue;

	// vma
	VmaAllocator _allocator;

	// draw resources
	AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);
	void destroy_image(const AllocatedImage& img);

	AllocatedImage _drawImage;
	AllocatedImage _depthImage;
	VkExtent2D _drawExtent;
	float renderScale = 1.f;

	// descriptor
	DescriptorAllocatorGrowable globalDescriptorAllocator;

	GPUSceneData sceneData;
	VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;

	// immediate submit structures
	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool _immCommandPool;

	void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

	// background
	void draw_background(VkCommandBuffer cmd);

	// actual scene
	void init_scene_pipelines();
	void draw_geometry(VkCommandBuffer cmd);

	// mesh buffer pipeline
	VkPipelineLayout _meshPipelineLayout;
	VkPipeline _meshPipeline;
	GPUMeshBuffers rectangle;

	// mesh buffers
	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void destroy_buffer(const AllocatedBuffer& buffer);
	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

	// default data
	std::vector<std::shared_ptr<MeshAsset>> testMeshes;

	AllocatedImage _whiteImage;
	AllocatedImage _blackImage;
	AllocatedImage _greyImage;
	AllocatedImage _errorCheckerboardImage;

	VkSampler _defaultSamplerLinear;
	VkSampler _defaultSamplerNearest;

	Material defaultData;

	// node architecture
	DrawContext mainDrawContext;
	std::unordered_map<std::string, std::shared_ptr<Node>> loadedNodes;
	std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> loadedScenes;

	void update_scene();

	Camera mainCamera;

	CullingSystem cullingSystem;

	EngineStats stats;

	// shader loader
	std::string shader_path(std::string_view path);
	ShaderCache _shaderCache;

	// bindless manager
	BindlessManager _bindlessManager;
	VkDescriptorSet bindlessDescriptorSet;

	// material system
	MaterialSystem _materialSystem;

	// render pass system
	GeometryPass* _geometryPass;
	ShadowPass* _shadowPass;

	RenderPassManager _renderPassManager;

private:
	void init_vulkan();

	// imgui
	void init_imgui();
	void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);

	void init_swapchain();
	void create_swapchain(uint32_t width, uint32_t height);
	void resize_swapchain();
	void destroy_swapchain();

	void init_commands();
	
	void init_sync_structures();

	void init_descriptors();

	void init_pipelines();

	void init_default_data();

	void init_render_passes();
};
