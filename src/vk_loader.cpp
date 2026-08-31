#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <iostream>
#include <vk_loader.h>

#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_types.h"
#include <glm/gtx/quaternion.hpp>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>

std::optional<AllocatedImage> load_image(VulkanEngine* engine, fastgltf::Asset& asset, fastgltf::Image& image)
{
	AllocatedImage newImage{};
	
	int width, height, nrChannels;

	std::visit(fastgltf::visitor{
			[](auto& arg) {},
			[&](fastgltf::sources::URI& filePath) {
				assert(filePath.fileByteOffset == 0);
				assert(filePath.uri.isLocalPath());

				const std::string path(filePath.uri.path().begin(), filePath.uri.path().end());

				unsigned char* data{ stbi_load(path.c_str(), &width, &height, &nrChannels, 4) };
				if (data)
				{
					VkExtent3D imagesize;
					imagesize.width = width;
					imagesize.height = height;
					imagesize.depth = 1;

					newImage = engine->create_image(data, imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, true);

					stbi_image_free(data);
				}
			},
			[&](fastgltf::sources::Vector& vector) {
				unsigned char* data{ stbi_load_from_memory(vector.bytes.data(), static_cast<int>(vector.bytes.size()), 
					&width, &height, &nrChannels, 4) };

				if (data)
				{
					VkExtent3D imagesize;
					imagesize.width = width;
					imagesize.height = height;
					imagesize.depth = 1;

					newImage = engine->create_image(data, imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, true);

					stbi_image_free(data);
				}
			},
			[&](fastgltf::sources::BufferView& view) {
				auto& bufferView{ asset.bufferViews[view.bufferViewIndex]};
				auto& buffer{ asset.buffers[bufferView.bufferIndex] };

				std::visit(fastgltf::visitor{
						[](auto& arg) {},
						[&](fastgltf::sources::Vector& vector) {
							unsigned char* data{ stbi_load_from_memory(vector.bytes.data() + bufferView.byteOffset, static_cast<int>(bufferView.byteLength),
								&width, &height, &nrChannels, 4) };

							if (data)
							{
								VkExtent3D imagesize;
								imagesize.width = width;
								imagesize.height = height;
								imagesize.depth = 1;

								newImage = engine->create_image(data, imagesize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, true);

								stbi_image_free(data);
							}
						}
				},
				buffer.data);
			},
		},
		image.data);

	if (newImage.image == VK_NULL_HANDLE)
		return {};
	else
		return newImage;
}

VkFilter extract_filter(fastgltf::Filter filter)
{
	switch (filter)
	{
	case fastgltf::Filter::Nearest:
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::NearestMipMapLinear:
		return VK_FILTER_NEAREST;

	case fastgltf::Filter::Linear:
	case fastgltf::Filter::LinearMipMapNearest:
	case fastgltf::Filter::LinearMipMapLinear:
	default:
		return VK_FILTER_LINEAR;
	}
}

VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter)
{
	switch (filter)
	{
	case fastgltf::Filter::LinearMipMapNearest:
	case fastgltf::Filter::NearestMipMapNearest:
		return VK_SAMPLER_MIPMAP_MODE_NEAREST;

	case fastgltf::Filter::LinearMipMapLinear:
	case fastgltf::Filter::NearestMipMapLinear:
	default:
		return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}

std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(VulkanEngine* engine, std::string_view filePath)
{
	fmt::print("Loading GLTF: {}\n", filePath);

	std::shared_ptr<LoadedGLTF> scene{ std::make_shared<LoadedGLTF>() };
	scene->creator = engine;

	LoadedGLTF& file{ *scene.get() };

	fastgltf::Parser parser{};

	constexpr auto gltfOptions{ fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::AllowDouble | fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers };

	fastgltf::GltfDataBuffer data;
	data.loadFromFile(filePath);

	fastgltf::Asset gltf;

	std::filesystem::path path{ filePath };

	auto type{ fastgltf::determineGltfFileType(&data) };
	if (type == fastgltf::GltfType::glTF)
	{
		auto load{ parser.loadGLTF(&data, path.parent_path(), gltfOptions) };
		if (load)
			gltf = std::move(load.get());
		else
		{
			std::cerr << "Failed to load gLTF: " << fastgltf::to_underlying(load.error()) << std::endl;
			return {};
		}
	}
	else if (type == fastgltf::GltfType::GLB)
	{
		auto load{ parser.loadBinaryGLTF(&data, path.parent_path(), gltfOptions) };
		if (load)
			gltf = std::move(load.get());
		else
		{
			std::cerr << "Failed to load gLTF: " << fastgltf::to_underlying(load.error()) << std::endl;
			return {};
		}
	}
	else
	{
		std::cerr << "Failed to determine gLTF container" << std::endl;
		return {};
	}

	// load samplers
	for (fastgltf::Sampler& sampler : gltf.samplers)
	{
		VkSamplerCreateInfo sampl{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr };
		
		sampl.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));
		sampl.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));

		sampl.mipmapMode = extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

		VkSampler newSampler;
		vkCreateSampler(engine->_device, &sampl, nullptr, &newSampler);

		file.samplers.push_back(newSampler);
	}

	std::vector<AllocatedImage> images;
	std::vector<MaterialInstance*> materials;
	std::vector<std::shared_ptr<MeshAsset>> meshes;
	std::vector<std::shared_ptr<Node>> nodes;

	// load all textures
	for (fastgltf::Image& image : gltf.images)
	{
		std::optional<AllocatedImage> img{ load_image(engine, gltf, image) };

		if (img.has_value())
		{
			images.push_back(*img);
			file.images[image.name.c_str()] = *img;
		}
		else
		{
			images.push_back(engine->_errorCheckerboardImage);
			std::cout << "gltf failed to load texture " << image.name << std::endl;
		}
	}

	std::vector<MaterialType*> materialHandlers;
	size_t totalRequiredBufferSize = 0;

	for (fastgltf::Material& mat : gltf.materials) {
		// Detect material type. For now, default to PBR.
		// In the future, you can read GLTF extensions to select "Unlit", "Clearcoat", etc.
		//std::string typeName = "PBRMetallicRoughness";
		std::string typeName = "PBRShadow";

		MaterialType* handler = engine->_materialSystem.get_material_type(typeName);
		materialHandlers.push_back(handler);

		if (handler) {
			totalRequiredBufferSize += handler->get_constant_buffer_size();
		}
	}

	// Allocate a single contiguous material parameter buffer based on the exact size needed
	file.materialDataBuffer = engine->create_buffer(
		totalRequiredBufferSize,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU
	);

	VkBufferDeviceAddressInfo addressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = file.materialDataBuffer.buffer };
	VkDeviceAddress materialBufferBaseAddress = vkGetBufferDeviceAddress(engine->_device, &addressInfo);

	// Get the raw byte pointer of our mapped buffer allocation
	uint8_t* cpuBufferPointer = static_cast<uint8_t*>(file.materialDataBuffer.info.pMappedData);

	size_t currentBufferOffset = 0;

	// 3. Load and write materials dynamically
	for (size_t i = 0; i < gltf.materials.size(); i++) {
		fastgltf::Material& mat = gltf.materials[i];
		MaterialType* handler = materialHandlers[i];

		if (!handler) {
			std::cout << "Skipping material " << mat.name << " (no handler found)" << std::endl;
			continue;
		}

		// Determine the exact addresses for this instance's data slice
		VkDeviceAddress gpuAddress = materialBufferBaseAddress + currentBufferOffset;
		void* cpuAddress = cpuBufferPointer + currentBufferOffset;

		// Delegate constant mapping and instance creation entirely to the MaterialType handler
		MaterialInstance* newMat = handler->write_material(
			engine,
			gltf,
			mat,
			gpuAddress,
			cpuAddress,
			images,
			file.samplers
		);

		materials.push_back(newMat);
		file.materials[mat.name.c_str()] = newMat;

		// Increment offset by the size specified by the subclass
		currentBufferOffset += handler->get_constant_buffer_size();
	}

	// load all meshes
	std::vector<uint32_t> indices;
	std::vector<Vertex> vertices;

	for (fastgltf::Mesh& mesh : gltf.meshes)
	{
		std::shared_ptr<MeshAsset> newMesh{ std::make_shared<MeshAsset>() };
		meshes.push_back(newMesh);
		file.meshes[mesh.name.c_str()] = newMesh;
		newMesh->name = mesh.name;

		indices.clear();
		vertices.clear();

		for (auto&& p : mesh.primitives)
		{
			GeoSurface newSurface;
			newSurface.startIndex = (uint32_t)indices.size();
			newSurface.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;
			
			size_t initial_vtx{ vertices.size() };

			// load indices
			{
				fastgltf::Accessor& indexAccessor{ gltf.accessors[p.indicesAccessor.value()] };
				indices.reserve(indices.size() + indexAccessor.count);

				fastgltf::iterateAccessor<std::uint32_t>(gltf, indexAccessor,
					[&](std::uint32_t idx) {
						indices.push_back(initial_vtx + idx);
					});
			}

			// load vertex positions
			{
				fastgltf::Accessor& posAccessor{ gltf.accessors[p.findAttribute("POSITION")->second] };
				vertices.resize(initial_vtx + posAccessor.count);

				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
					[&](glm::vec3 v, size_t index) {
						Vertex newvtx{};
						newvtx.position = v;
						newvtx.normal = { 1, 0, 0 };
						newvtx.color = glm::vec4{ 1.f };
						newvtx.uv_x = 0;
						newvtx.uv_y = 0;
						vertices[initial_vtx + index] = newvtx;
					});
			}

			// load vertex normals
			auto normals{ p.findAttribute("NORMAL") };
			if (normals != p.attributes.end())
			{
				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[normals->second],
					[&](glm::vec3 v, size_t index) {
						vertices[initial_vtx + index].normal = v;
					});
			}

			// load UV
			auto uv{ p.findAttribute("TEXCOORD_0") };
			if (uv != p.attributes.end())
			{
				fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[uv->second],
					[&](glm::vec2 v, size_t index) {
						vertices[initial_vtx + index].uv_x = v.x;
						vertices[initial_vtx + index].uv_y = v.y;
					});
			}

			// load vertex colors
			auto colors{ p.findAttribute("COLOR_0") };
			if (colors != p.attributes.end())
			{
				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[colors->second],
					[&](glm::vec4 v, size_t index) {
						vertices[initial_vtx + index].color = v;
					});
			}

			// mesh materials
			if (p.materialIndex.has_value())
				newSurface.material = materials[p.materialIndex.value()];
			else
				newSurface.material = materials[0];

			// mesh bounds
			glm::vec3 minPos{ vertices[initial_vtx].position };
			glm::vec3 maxPos{ vertices[initial_vtx].position };
			for (int i = initial_vtx + 1; i < vertices.size(); i++)
			{
				minPos = glm::min(minPos, vertices[i].position);
				maxPos = glm::max(maxPos, vertices[i].position);
			}
			newSurface.bounds.origin = (minPos + maxPos) / 2.f;
			newSurface.bounds.extents = (maxPos - minPos) / 2.f;
			newSurface.bounds.sphereRadius = glm::length(newSurface.bounds.extents);

			newMesh->surfaces.push_back(newSurface);
		}

		newMesh->meshBuffers = engine->uploadMesh(indices, vertices);
	}

	// load all nodes and their meshes
	for (fastgltf::Node& node : gltf.nodes)
	{
		std::shared_ptr<Node> newNode;

		if (node.meshIndex.has_value())
		{
			newNode = std::make_shared<MeshNode>();
			static_cast<MeshNode*>(newNode.get())->mesh = meshes[*node.meshIndex];
		}
		else
			newNode = std::make_shared<Node>();

		nodes.push_back(newNode);
		file.nodes[node.name.c_str()];

		std::visit(fastgltf::visitor{
			[&](fastgltf::Node::TransformMatrix matrix) {
				memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
			},
			[&](fastgltf::Node::TRS transform) {
				glm::vec3 tl(transform.translation[0], transform.translation[1], transform.translation[2]);
				glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]);
				glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

				glm::mat4 tm{ glm::translate(glm::mat4{ 1.f }, tl) };
				glm::mat4 rm{ glm::toMat4(rot) };
				glm::mat4 sm{ glm::scale(glm::mat4{ 1.f }, sc) };

				newNode->localTransform = tm * rm * sm;
			}},
		node.transform);
	}

	// run loop again to setup transform hierarchy
	for (int i = 0; i < gltf.nodes.size(); i++)
	{
		fastgltf::Node& node{ gltf.nodes[i] };
		std::shared_ptr<Node>& sceneNode{ nodes[i] };

		for (auto& c : node.children)
		{
			sceneNode->children.push_back(nodes[c]);
			nodes[c]->parent = sceneNode;
		}
	}

	for (auto& node : nodes)
	{
		if (node->parent.lock() == nullptr)
		{
			file.topNodes.push_back(node);
			node->refreshTransform(glm::mat4{ 1.f });
		}
	}
	
	return scene;
}

void LoadedGLTF::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
	for (auto& node : topNodes)
	{
		node->Draw(topMatrix, ctx);
	}
}

void LoadedGLTF::clearAll()
{
	VkDevice dv{ creator->_device };

	creator->destroy_buffer(materialDataBuffer);

	for (auto& [k, v] : meshes)
	{
		creator->destroy_buffer(v->meshBuffers.indexBuffer);
		creator->destroy_buffer(v->meshBuffers.vertexBuffer);
	}

	for (auto& [k, v] : images)
	{
		if (v.image == creator->_errorCheckerboardImage.image)
			continue;

		creator->destroy_image(v);
	}

	for (auto& sampler : samplers)
	{
		vkDestroySampler(dv, sampler, nullptr);
	}
}