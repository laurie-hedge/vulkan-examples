#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define IMAGE_WIDTH  800
#define IMAGE_HEIGHT 600

struct {
	PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;
	PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT;
	PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;
	PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
	PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
	PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
	PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
	PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
	PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
	PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
	PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;
} ext;

#define LOAD_EXTENSION_FUNC(FuncName) \
	ext.FuncName = (PFN_##FuncName)vkGetInstanceProcAddr(instance, #FuncName); \
	if (!ext.FuncName) return false

void save_rgb8_image_to_ppm(char const *filename,
                            uint16_t width_px,
                            uint16_t height_px,
                            uint8_t *texel_buffer) {
	FILE *file = fopen(filename, "wb");
	fprintf(file, "P6 %d %d 255\n", width_px, height_px);
	fwrite(texel_buffer, 3, width_px * height_px, file);
	fclose(file);
}

char *load_binary_file(char const *filename, size_t *size) {
	FILE *file = fopen(filename, "rb");
	if (!file) {
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	*size = ftell(file);
	fseek(file, 0, SEEK_SET);

	char *buffer = malloc(*size);
	fread(buffer, 1, *size, file);

	fclose(file);

	return buffer;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
	VkDebugUtilsMessageTypeFlagsEXT message_type,
	VkDebugUtilsMessengerCallbackDataEXT const *callback_data,
	void *user_data) {
	if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		fprintf(stderr, "validation layer: %s\n", callback_data->pMessage);
	}
	return VK_FALSE;
}

bool create_buffer(VkDevice device,
                   uint32_t usable_memory_types,
                   VkDeviceSize buffer_size,
                   VkBufferUsageFlags usage_flags,
                   VkBuffer *buffer,
                   VkDeviceMemory *buffer_memory,
                   VkDeviceAddress *device_address,
                   void const *data) {
	VkBufferCreateInfo buffer_create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size  = buffer_size,
		.usage = usage_flags,
	};
	if (vkCreateBuffer(device, &buffer_create_info, NULL, buffer) != VK_SUCCESS) {
		return false;
	}

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(device, *buffer, &memory_requirements);

	uint32_t const memory_types_matching_requirements =
		memory_requirements.memoryTypeBits & usable_memory_types;
	if (memory_types_matching_requirements == 0) {
		return false;
	}

	VkMemoryAllocateFlagsInfo memory_alloc_flags_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
		.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR,
	};

	VkMemoryAllocateInfo buffer_memory_allocate_info = {
		.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext           = &memory_alloc_flags_info,
		.allocationSize  = memory_requirements.size,
		.memoryTypeIndex = __builtin_ctz(memory_types_matching_requirements),
	};
	if (vkAllocateMemory(device, &buffer_memory_allocate_info, NULL, buffer_memory) != VK_SUCCESS) {
		return false;
	}

	if (data) {
		void *mapped;
		if (vkMapMemory(device, *buffer_memory, 0, buffer_size, 0, &mapped) != VK_SUCCESS) {
			return false;
		}
		memcpy(mapped, data, buffer_size);
		vkUnmapMemory(device, *buffer_memory);
	}

	if (vkBindBufferMemory(device, *buffer, *buffer_memory, 0) != VK_SUCCESS) {
		return false;
	}

	if (device_address) {
		VkBufferDeviceAddressInfoKHR buffer_device_address_info = {
			.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = *buffer,
		};
		*device_address = ext.vkGetBufferDeviceAddressKHR(device, &buffer_device_address_info);
	}

	return true;
}

bool create_shader_module(VkDevice device, char const *filename, VkShaderModule *shader_module) {
	size_t shader_code_size;
	void *shader_code = load_binary_file(filename, &shader_code_size);
	if (!shader_code) {
		return false;
	}

	VkShaderModuleCreateInfo shader_module_create_info = {
		.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = shader_code_size,
		.pCode    = shader_code,
	};

	VkResult result = vkCreateShaderModule(device, &shader_module_create_info, NULL, shader_module);
	free(shader_code);
	return result == VK_SUCCESS;
}

bool ray_trace_image(uint8_t *texel_buffer, uint16_t width_px, uint16_t height_px) {
	// create vulkan instance
	VkApplicationInfo app_info = {
		.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName   = "Offscreen Ray Tracing Example",
		.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
		.pEngineName        = "No Engine",
		.engineVersion      = VK_MAKE_VERSION(1, 0, 0),
		.apiVersion         = VK_API_VERSION_1_1,
	};

	char const *extension_names[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
	char const *validation_layers[] = { "VK_LAYER_KHRONOS_validation" };

	VkInstanceCreateInfo instance_create_info = {
		.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo        = &app_info,
		.enabledLayerCount       = 1,
		.ppEnabledLayerNames     = validation_layers,
		.enabledExtensionCount   = 1,
		.ppEnabledExtensionNames = extension_names,
	};

	VkInstance instance;
	if (vkCreateInstance(&instance_create_info, NULL, &instance) != VK_SUCCESS) {
		return false;
	}

	// load extension functions
	LOAD_EXTENSION_FUNC(vkCreateDebugUtilsMessengerEXT);
	LOAD_EXTENSION_FUNC(vkDestroyDebugUtilsMessengerEXT);
	LOAD_EXTENSION_FUNC(vkGetBufferDeviceAddressKHR);
	LOAD_EXTENSION_FUNC(vkCreateAccelerationStructureKHR);
	LOAD_EXTENSION_FUNC(vkDestroyAccelerationStructureKHR);
	LOAD_EXTENSION_FUNC(vkGetAccelerationStructureBuildSizesKHR);
	LOAD_EXTENSION_FUNC(vkGetAccelerationStructureDeviceAddressKHR);
	LOAD_EXTENSION_FUNC(vkCmdBuildAccelerationStructuresKHR);
	LOAD_EXTENSION_FUNC(vkCmdTraceRaysKHR);
	LOAD_EXTENSION_FUNC(vkGetRayTracingShaderGroupHandlesKHR);
	LOAD_EXTENSION_FUNC(vkCreateRayTracingPipelinesKHR);

	// setup debug messenger
	VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = {
		.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = debug_callback,
	};

	VkDebugUtilsMessengerEXT debug_messenger;
	if (ext.vkCreateDebugUtilsMessengerEXT(instance,
                                           &debug_messenger_create_info,
                                           NULL,
                                           &debug_messenger) != VK_SUCCESS) {
		return false;
	}

	// select physical device
	uint32_t physical_device_count = 0;
	vkEnumeratePhysicalDevices(instance, &physical_device_count, NULL);
	if (physical_device_count == 0) {
		return false;
	}

	VkPhysicalDevice physical_devices[physical_device_count];
	vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices);

	#define NUM_REQUIRED_EXTENSIONS 7
	char const *required_extensions[NUM_REQUIRED_EXTENSIONS] = {
		VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
		VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
		VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
		VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
		VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
		VK_KHR_SPIRV_1_4_EXTENSION_NAME,
		VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME
	};

	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	uint32_t graphics_queue_index;
	for (uint32_t i = 0; i < physical_device_count; ++i) {
		VkPhysicalDeviceProperties device_properties;
		vkGetPhysicalDeviceProperties(physical_devices[i], &device_properties);
		if (device_properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
		    device_properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
			continue;
		}

		uint32_t extension_count = 0;
		vkEnumerateDeviceExtensionProperties(physical_devices[i], NULL, &extension_count, NULL);
		VkExtensionProperties extensions[extension_count];
		vkEnumerateDeviceExtensionProperties(physical_devices[i], NULL, &extension_count, extensions);
		size_t extensions_found = 0;
		for (size_t j = 0; j < NUM_REQUIRED_EXTENSIONS; ++j) {
			for (uint32_t k = 0; k < extension_count; ++k) {
				if (strcmp(required_extensions[j], extensions[k].extensionName) == 0) {
					extensions_found += 1;
					break;
				}
			}
		}
		if (extensions_found < NUM_REQUIRED_EXTENSIONS) {
			continue;
		}

		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, NULL);
		VkQueueFamilyProperties queue_family_properties[queue_family_count];
		vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i],
		                                         &queue_family_count,
		                                         queue_family_properties);
		graphics_queue_index = UINT32_MAX;
		for (uint32_t j = 0; j < queue_family_count; ++j) {
			if (queue_family_properties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				graphics_queue_index = j;
				break;
			}
		}
		if (graphics_queue_index == UINT32_MAX) {
			continue;
		}

		physical_device = physical_devices[i];
		break;
	}

	if (physical_device == VK_NULL_HANDLE) {
		return false;
	}

	// create device
	VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_pipeline_properties = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
	};
	VkPhysicalDeviceProperties2 device_properties = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
		.pNext = &ray_tracing_pipeline_properties,
	};
	vkGetPhysicalDeviceProperties2(physical_device, &device_properties);

	float const queue_priority = 1.0f;
	VkDeviceQueueCreateInfo device_queue_create_info = {
		.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = graphics_queue_index,
		.queueCount       = 1,
		.pQueuePriorities = &queue_priority,
	};

	VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address_features = {
		.sType               = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
		.bufferDeviceAddress = VK_TRUE,
	};

	VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_device_features = {
		.sType              = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
		.rayTracingPipeline = VK_TRUE,
		.pNext              = (void*)&buffer_device_address_features,
	};

	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features = {
		.sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
		.accelerationStructure = VK_TRUE,
		.pNext                 = (void*)&ray_tracing_device_features,
	};

	VkPhysicalDeviceFeatures2 device_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = (void*)&acceleration_structure_features,
	};

	VkDeviceCreateInfo device_create_info = {
		.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext                   = (void*)&device_features,
		.queueCreateInfoCount    = 1,
		.pQueueCreateInfos       = &device_queue_create_info,
		.enabledExtensionCount   = NUM_REQUIRED_EXTENSIONS,
		.ppEnabledExtensionNames = required_extensions,
		.enabledLayerCount       = 1,
		.ppEnabledLayerNames     = validation_layers,
	};

	VkDevice device;
	if (vkCreateDevice(physical_device, &device_create_info, NULL, &device) != VK_SUCCESS) {
		return false;
	}

	// find host coherent memory types
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
	
	uint32_t host_coherent_memory_types = 0;
	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
		if ((memory_properties.memoryTypes[i].propertyFlags &
			 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
			(memory_properties.memoryTypes[i].propertyFlags &
			 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
			) {
			host_coherent_memory_types |= 1 << i;
		}
	}

	// get graphics queue from device
	VkQueue graphics_queue;
	vkGetDeviceQueue(device, graphics_queue_index, 0, &graphics_queue);

	// create command pool
	VkCommandPoolCreateInfo command_pool_create_info = {
		.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = graphics_queue_index,
	};
	VkCommandPool command_pool;
	if (vkCreateCommandPool(device, &command_pool_create_info, NULL, &command_pool) != VK_SUCCESS) {
		return false;
	}

	// create command buffer
	VkCommandBufferAllocateInfo command_buffer_alloc_info = {
		.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool        = command_pool,
		.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VkCommandBuffer command_buffer;
	if (vkAllocateCommandBuffers(device, &command_buffer_alloc_info, &command_buffer) != VK_SUCCESS) {
		return false;
	}

	// create fence
	VkFenceCreateInfo fence_create_info = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	};

	VkFence fence;
	if (vkCreateFence(device, &fence_create_info, NULL, &fence) != VK_SUCCESS) {
		return false;
	}

	// create image
	VkImageCreateInfo image_create_info = {
		.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType     = VK_IMAGE_TYPE_2D,
		.format        = VK_FORMAT_R8G8B8A8_UNORM,
		.extent.width  = width_px,
		.extent.height = height_px,
		.extent.depth  = 1,
		.mipLevels     = 1,
		.arrayLayers   = 1,
		.samples       = VK_SAMPLE_COUNT_1_BIT,
		.tiling        = VK_IMAGE_TILING_OPTIMAL,
		.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		.sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VkImage image;
	if (vkCreateImage(device, &image_create_info, NULL, &image) != VK_SUCCESS) {
		return false;
	}

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements(device, image, &memory_requirements);

	uint32_t usable_memory_bits = memory_requirements.memoryTypeBits & host_coherent_memory_types;
	if (usable_memory_bits == 0) {
		return false;
	}

	VkMemoryAllocateInfo memory_alloc_info = {
		.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize  = memory_requirements.size,
		.memoryTypeIndex = __builtin_ctz(usable_memory_bits),
	};
	VkDeviceMemory image_memory;
	if (vkAllocateMemory(device, &memory_alloc_info, NULL, &image_memory) != VK_SUCCESS) {
		return false;
	}

	if (vkBindImageMemory(device, image, image_memory, 0) != VK_SUCCESS) {
		return false;
	}

	// create image view
	VkImageViewCreateInfo image_view_create_info = {
		.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image                           = VK_NULL_HANDLE,
		.viewType                        = VK_IMAGE_VIEW_TYPE_2D,
		.format                          = VK_FORMAT_R8G8B8A8_UNORM,
		.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY,
		.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY,
		.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY,
		.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY,
		.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.baseMipLevel   = 0,
		.subresourceRange.levelCount     = 1,
		.subresourceRange.baseArrayLayer = 0,
		.subresourceRange.layerCount     = 1,
		.image                           = image,
	};

	VkImageView image_view;
	if (vkCreateImageView(device, &image_view_create_info, NULL, &image_view) != VK_SUCCESS) {
		return false;
	}

	// create vertex buffer
	float const vertices[] = {
		 1.0f,  1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f,
		 0.0f, -1.0f, 0.0f
	};

	VkBuffer vertex_buffer;
	VkDeviceMemory vertex_buffer_memory;
	VkDeviceOrHostAddressConstKHR vertex_buffer_device_address;
	if (!create_buffer(device,
	                   host_coherent_memory_types,
	                   sizeof(vertices),
	                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
	                   &vertex_buffer,
	                   &vertex_buffer_memory,
	                   &vertex_buffer_device_address.deviceAddress,
	                   vertices)) {
		return false;
	}

	// create index buffer
	uint32_t const indices[] = { 0, 1, 2 };

	VkBuffer index_buffer;
	VkDeviceMemory index_buffer_memory;
	VkDeviceOrHostAddressConstKHR index_buffer_device_address;
	if (!create_buffer(device,
	                   host_coherent_memory_types,
	                   sizeof(indices),
	                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
	                   &index_buffer,
	                   &index_buffer_memory,
	                   &index_buffer_device_address.deviceAddress,
	                   indices)) {
		return false;
	}

	// create transform matrix buffer
	VkTransformMatrixKHR transform_matrix = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f
	};

	VkBuffer transform_matrix_buffer;
	VkDeviceMemory transform_matrix_buffer_memory;
	VkDeviceOrHostAddressConstKHR transform_matrix_buffer_device_address;
	if (!create_buffer(device,
	                   host_coherent_memory_types,
	                   sizeof(transform_matrix),
	                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
	                   &transform_matrix_buffer,
	                   &transform_matrix_buffer_memory,
	                   &transform_matrix_buffer_device_address.deviceAddress,
	                   &transform_matrix)) {
		return false;
	}

	// create bottom level acceleration structure buffer
	VkAccelerationStructureGeometryKHR bottom_level_acceleration_structure_geometry = {
		.sType                            = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.flags                            = VK_GEOMETRY_OPAQUE_BIT_KHR,
		.geometryType                     = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
		.geometry.triangles.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
		.geometry.triangles.vertexFormat  = VK_FORMAT_R32G32B32_SFLOAT,
		.geometry.triangles.vertexData    = vertex_buffer_device_address,
		.geometry.triangles.maxVertex     = 2,
		.geometry.triangles.vertexStride  = sizeof(float) * 3,
		.geometry.triangles.indexType     = VK_INDEX_TYPE_UINT32,
		.geometry.triangles.indexData     = index_buffer_device_address,
		.geometry.triangles.transformData = transform_matrix_buffer_device_address,
	};

	VkAccelerationStructureBuildGeometryInfoKHR bottom_level_acceleration_structure_build_geometry_info = {
		.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
		.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = 1,
		.pGeometries   = &bottom_level_acceleration_structure_geometry,
	};

	uint32_t const num_triangles = 1;

	VkAccelerationStructureBuildSizesInfoKHR acceleration_structure_build_sizes_info = {
		.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
	};
	ext.vkGetAccelerationStructureBuildSizesKHR(
		device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&bottom_level_acceleration_structure_build_geometry_info,
		&num_triangles,
		&acceleration_structure_build_sizes_info
	);

	VkBuffer bottom_level_acceleration_structure_buffer;
	VkDeviceMemory bottom_level_acceleration_structure_buffer_memory;
	if (!create_buffer(device,
	                   host_coherent_memory_types,
	                   acceleration_structure_build_sizes_info.accelerationStructureSize,
	                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
	                   &bottom_level_acceleration_structure_buffer,
	                   &bottom_level_acceleration_structure_buffer_memory,
	                   NULL, NULL)) {
		return false;
	}

	// create bottom level acceleration structure
	VkAccelerationStructureCreateInfoKHR bottom_level_acceleration_structure_create_info = {
		.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = bottom_level_acceleration_structure_buffer,
		.size   = acceleration_structure_build_sizes_info.accelerationStructureSize,
		.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
	};

	VkAccelerationStructureKHR bottom_level_acceleration_structure;
	if (ext.vkCreateAccelerationStructureKHR(device,
	                                     &bottom_level_acceleration_structure_create_info,
	                                     NULL,
	                                     &bottom_level_acceleration_structure) != VK_SUCCESS) {
		return false;
	}

	VkBuffer scratch_buffer;
	VkDeviceMemory scratch_buffer_memory;
	VkDeviceOrHostAddressKHR scratch_buffer_device_address;
	if (!create_buffer(device,
	                   host_coherent_memory_types,
	                   acceleration_structure_build_sizes_info.buildScratchSize,
	                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	                   &scratch_buffer,
	                   &scratch_buffer_memory,
	                   &scratch_buffer_device_address.deviceAddress,
	                   NULL)) {
		return false;
	}

	bottom_level_acceleration_structure_build_geometry_info.dstAccelerationStructure = bottom_level_acceleration_structure;
	bottom_level_acceleration_structure_build_geometry_info.scratchData              = scratch_buffer_device_address;

	VkAccelerationStructureBuildRangeInfoKHR bottom_level_acceleration_structure_build_range_info = {
		.primitiveCount  = num_triangles,
		.primitiveOffset = 0,
		.firstVertex     = 0,
		.transformOffset = 0,
	};
	VkAccelerationStructureBuildRangeInfoKHR const *bottom_level_acceleration_structure_build_range_infos[] = {
		&bottom_level_acceleration_structure_build_range_info,
	};

	vkResetCommandBuffer(command_buffer, 0);

	VkCommandBufferBeginInfo command_buffer_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	if (vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) != VK_SUCCESS) {
		return false;
	}

	ext.vkCmdBuildAccelerationStructuresKHR(
		command_buffer,
		1,
		&bottom_level_acceleration_structure_build_geometry_info,
		bottom_level_acceleration_structure_build_range_infos
	);

	if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
		return false;
	}

	VkSubmitInfo submit_info = {
		.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers    = &command_buffer,
	};
	if (vkQueueSubmit(graphics_queue, 1, &submit_info, fence) != VK_SUCCESS) {
		return false;
	}

	if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
		return false;
	}

	vkResetFences(device, 1, &fence);

	VkAccelerationStructureDeviceAddressInfoKHR bottom_level_acceleration_device_address_info = {
		.sType                 = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
		.accelerationStructure = bottom_level_acceleration_structure,
	};
	uint64_t const bottom_level_acceleration_structure_buffer_device_address =
		ext.vkGetAccelerationStructureDeviceAddressKHR(device, &bottom_level_acceleration_device_address_info);

	vkFreeMemory(device, scratch_buffer_memory, NULL);
	vkDestroyBuffer(device, scratch_buffer, NULL);

	// create top level acceleration structure buffer
	VkAccelerationStructureInstanceKHR acceleration_structure_instance = {
		.transform                              = transform_matrix,
		.instanceCustomIndex                    = 0,
		.mask                                   = 0xFF,
		.instanceShaderBindingTableRecordOffset = 0,
		.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
		.accelerationStructureReference         = bottom_level_acceleration_structure_buffer_device_address,
	};

	VkBuffer acceleration_structure_instance_buffer;
	VkDeviceMemory acceleration_structure_instance_buffer_memory;
	VkDeviceOrHostAddressConstKHR acceleration_structure_instance_buffer_device_address;
	if (!create_buffer(device,
	                   host_coherent_memory_types,
	                   sizeof(acceleration_structure_instance),
	                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
	                   &acceleration_structure_instance_buffer,
	                   &acceleration_structure_instance_buffer_memory,
	                   &acceleration_structure_instance_buffer_device_address.deviceAddress,
	                   &acceleration_structure_instance)) {
		return false;
	}

	VkAccelerationStructureGeometryKHR top_level_acceleration_structure_geometry = {
		.sType                              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
		.geometryType                       = VK_GEOMETRY_TYPE_INSTANCES_KHR,
		.flags                              = VK_GEOMETRY_OPAQUE_BIT_KHR,
		.geometry.instances.sType           = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
		.geometry.instances.arrayOfPointers = VK_FALSE,
		.geometry.instances.data            = acceleration_structure_instance_buffer_device_address,
	};

	VkAccelerationStructureBuildGeometryInfoKHR top_level_acceleration_structure_build_geometry_info = {
		.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
		.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
		.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
		.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
		.geometryCount = 1,
		.pGeometries   = &top_level_acceleration_structure_geometry,
	};

	uint32_t const primitive_count = 1;

	ext.vkGetAccelerationStructureBuildSizesKHR(
		device, 
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&top_level_acceleration_structure_build_geometry_info,
		&primitive_count,
		&acceleration_structure_build_sizes_info
	);

	VkBuffer top_level_acceleration_structure_buffer;
	VkDeviceMemory top_level_acceleration_structure_buffer_memory;
	if (!create_buffer(device,
	                   host_coherent_memory_types,
	                   acceleration_structure_build_sizes_info.accelerationStructureSize,
	                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
	                   &top_level_acceleration_structure_buffer,
	                   &top_level_acceleration_structure_buffer_memory,
	                   NULL, NULL)) {
		return false;
	}

	// create top level acceleration structure
	VkAccelerationStructureCreateInfoKHR top_level_acceleration_structure_info = {
		.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
		.buffer = top_level_acceleration_structure_buffer,
		.size   = acceleration_structure_build_sizes_info.accelerationStructureSize,
		.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
	};

	VkAccelerationStructureKHR top_level_acceleration_structure;
	if (ext.vkCreateAccelerationStructureKHR(device,
	                                     &top_level_acceleration_structure_info,
	                                     NULL,
	                                     &top_level_acceleration_structure) != VK_SUCCESS) {
		return false;
	}

	if (!create_buffer(device,
	                   host_coherent_memory_types,
	                   acceleration_structure_build_sizes_info.buildScratchSize,
	                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	                   &scratch_buffer,
	                   &scratch_buffer_memory,
	                   &scratch_buffer_device_address.deviceAddress,
	                   NULL)) {
		return false;
	}

	top_level_acceleration_structure_build_geometry_info.dstAccelerationStructure = top_level_acceleration_structure;
	top_level_acceleration_structure_build_geometry_info.scratchData              = scratch_buffer_device_address;

	VkAccelerationStructureBuildRangeInfoKHR top_level_acceleration_structure_build_range_info = {
		.primitiveCount  = 1,
		.primitiveOffset = 0,
		.firstVertex     = 0,
		.transformOffset = 0,
	};
	VkAccelerationStructureBuildRangeInfoKHR const *top_level_acceleration_structure_build_range_infos[] = {
		&top_level_acceleration_structure_build_range_info,
	};

	vkResetCommandBuffer(command_buffer, 0);

	if (vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) != VK_SUCCESS) {
		return false;
	}

	ext.vkCmdBuildAccelerationStructuresKHR(
		command_buffer,
		1,
		&top_level_acceleration_structure_build_geometry_info,
		top_level_acceleration_structure_build_range_infos
	);

	if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
		return false;
	}

	if (vkQueueSubmit(graphics_queue, 1, &submit_info, fence) != VK_SUCCESS) {
		return false;
	}

	if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
		return false;
	}

	vkResetFences(device, 1, &fence);

	vkFreeMemory(device, scratch_buffer_memory, NULL);
	vkDestroyBuffer(device, scratch_buffer, NULL);

	vkFreeMemory(device, acceleration_structure_instance_buffer_memory, NULL);
	vkDestroyBuffer(device, acceleration_structure_instance_buffer, NULL);

	// create destination buffer for image data
	uint32_t const image_buffer_size = width_px * height_px * 4;

	VkBuffer image_buffer;
	VkDeviceMemory image_buffer_memory;
	if (!create_buffer(device,
	                   host_coherent_memory_types,
	                   image_buffer_size,
	                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	                   &image_buffer,
	                   &image_buffer_memory,
	                   NULL, NULL)) {
		return false;
	}

	// create descriptor set layout
	VkDescriptorSetLayoutBinding descriptor_set_layout_bindings[2] = {
		{
			.binding         = 0,
			.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
			.descriptorCount = 1,
			.stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
		},
		{
			.binding         = 1,
			.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.descriptorCount = 1,
			.stageFlags      = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
		}
	};

	VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {
		.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 2,
		.pBindings    = descriptor_set_layout_bindings,
	};

	VkDescriptorSetLayout descriptor_set_layout;
	if (vkCreateDescriptorSetLayout(device,
	                                &descriptor_set_layout_create_info,
	                                NULL, &descriptor_set_layout) != VK_SUCCESS) {
		return false;
	}

	// create pipeline layout
	VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
		.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts    = &descriptor_set_layout,
	};

	VkPipelineLayout pipeline_layout;
	if (vkCreatePipelineLayout(device, &pipeline_layout_create_info, NULL, &pipeline_layout) != VK_SUCCESS) {
		return false;
	}

	// create shader modules
	VkShaderModule rgen_shader_module;
	create_shader_module(device, "rgen.spv", &rgen_shader_module);

	VkShaderModule miss_shader_module;
	create_shader_module(device, "miss.spv", &miss_shader_module);
	
	VkShaderModule hit_shader_module;
	create_shader_module(device, "hit.spv", &hit_shader_module);

	// create ray tracing pipeline
	VkPipelineShaderStageCreateInfo shader_stage_create_infos[3] = {
		{
			.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
			.module = rgen_shader_module,
			.pName  = "main",
		},
		{
			.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage  = VK_SHADER_STAGE_MISS_BIT_KHR,
			.module = miss_shader_module,
			.pName  = "main",
		},
		{
			.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
			.module = hit_shader_module,
			.pName  = "main",
		}
	};

	VkRayTracingShaderGroupCreateInfoKHR shader_group_create_infos[3] = {
		{
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader      = 0,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		},
		{
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
			.generalShader      = 1,
			.closestHitShader   = VK_SHADER_UNUSED_KHR,
			.anyHitShader       = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		},
		{
			.sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
			.type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
			.generalShader      = VK_SHADER_UNUSED_KHR,
			.closestHitShader   = 2,
			.anyHitShader       = VK_SHADER_UNUSED_KHR,
			.intersectionShader = VK_SHADER_UNUSED_KHR,
		}
	};

	VkRayTracingPipelineCreateInfoKHR ray_tracing_pipeline_create_info = {
		.sType                        = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
		.stageCount                   = 3,
		.pStages                      = shader_stage_create_infos,
		.groupCount                   = 3,
		.pGroups                      = shader_group_create_infos,
		.maxPipelineRayRecursionDepth = 1,
		.layout                       = pipeline_layout,
	};

	VkPipeline ray_tracing_pipeline;
	if (ext.vkCreateRayTracingPipelinesKHR(device,
	                                       VK_NULL_HANDLE, VK_NULL_HANDLE,
	                                       1,
	                                       &ray_tracing_pipeline_create_info,
	                                       NULL,
	                                       &ray_tracing_pipeline) != VK_SUCCESS) {
		return false;
	}

	// free shader modules
	vkDestroyShaderModule(device, hit_shader_module, NULL);
	vkDestroyShaderModule(device, miss_shader_module, NULL);
	vkDestroyShaderModule(device, rgen_shader_module, NULL);

	// create shader table buffer
	uint32_t const shader_handle_size         = ray_tracing_pipeline_properties.shaderGroupHandleSize;
	uint32_t const shader_handle_alignment    = ray_tracing_pipeline_properties.shaderGroupHandleAlignment;
	uint32_t const shader_handle_size_aligned =
	    ((shader_handle_size % shader_handle_alignment) == 0)
	    ? shader_handle_size
	    : ((shader_handle_size / shader_handle_alignment) + 1) * shader_handle_alignment;
	uint32_t const shader_table_size = shader_handle_size_aligned * 3;

	VkBuffer shader_table_buffer;
	VkDeviceMemory shader_table_buffer_memory;
	VkDeviceOrHostAddressConstKHR shader_table_buffer_device_address;
	if (!create_buffer(device,
	                   host_coherent_memory_types,
	                   shader_table_size,
	                   VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
	                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	                   &shader_table_buffer,
	                   &shader_table_buffer_memory,
	                   &shader_table_buffer_device_address.deviceAddress,
	                   NULL)) {
		return false;
	}

	void *mapped;
	if (vkMapMemory(device, shader_table_buffer_memory, 0, shader_table_size, 0, &mapped) != VK_SUCCESS) {
		return false;
	}

	for (uint32_t i = 0; i < 3; ++i) {
		if (ext.vkGetRayTracingShaderGroupHandlesKHR(
			device,
			ray_tracing_pipeline,
			i,
			1,
			shader_handle_size_aligned,
			(uint8_t *)mapped + (i * shader_handle_size_aligned)
		) != VK_SUCCESS) {
			return false;
		}
	}

	vkUnmapMemory(device, shader_table_buffer_memory);

	// create descriptor pool
	VkDescriptorPoolSize descriptor_pool_sizes[2] = {
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
	};

	VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.poolSizeCount = 2,
		.pPoolSizes    = descriptor_pool_sizes,
		.maxSets       = 1,
	};

	VkDescriptorPool descriptor_pool;
	if (vkCreateDescriptorPool(device, &descriptor_pool_create_info, NULL, &descriptor_pool) != VK_SUCCESS) {
		return false;
	}

	// allocate descriptor set
	VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
		.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool     = descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts        = &descriptor_set_layout,
	};

	VkDescriptorSet descriptor_set;
	if (vkAllocateDescriptorSets(device, &descriptor_set_allocate_info, &descriptor_set) != VK_SUCCESS) {
		return false;
	}

	// update descriptor set
	VkWriteDescriptorSetAccelerationStructureKHR write_descriptor_set_acceleration_structure = {
		.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
		.accelerationStructureCount = 1,
		.pAccelerationStructures    = &top_level_acceleration_structure,
	};

	VkDescriptorImageInfo descriptor_image_info = {
		.imageView   = image_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VkWriteDescriptorSet write_descriptor_sets[2] = {
		{
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.pNext           = &write_descriptor_set_acceleration_structure,
			.dstSet          = descriptor_set,
			.dstBinding      = 0,
			.descriptorCount = 1,
			.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
		},
		{
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet          = descriptor_set,
			.dstBinding      = 1,
			.descriptorCount = 1,
			.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			.pImageInfo      = &descriptor_image_info,
		}
	};

	vkUpdateDescriptorSets(device, 2, write_descriptor_sets, 0, NULL);

	// record command buffer
	vkResetCommandBuffer(command_buffer, 0);

	if (vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) != VK_SUCCESS) {
		return false;
	}

	VkImageMemoryBarrier image_memory_barrier = {
		.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout                   = VK_IMAGE_LAYOUT_GENERAL,
		.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED,
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.levelCount = 1,
		.subresourceRange.layerCount = 1,
		.image                       = image,
	};

	vkCmdPipelineBarrier(
		command_buffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0,
		0,
		NULL,
		0,
		NULL,
		1,
		&image_memory_barrier
	);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, ray_tracing_pipeline);

	vkCmdBindDescriptorSets(
		command_buffer,
		VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		pipeline_layout,
		0,
		1,
		&descriptor_set,
		0,
		NULL
	);

	VkStridedDeviceAddressRegionKHR raygen_shader_table_entry = {
		.deviceAddress = shader_table_buffer_device_address.deviceAddress,
		.stride        = shader_handle_size_aligned,
		.size          = shader_handle_size_aligned,
	};

	VkStridedDeviceAddressRegionKHR miss_shader_table_entry = {
		.deviceAddress = shader_table_buffer_device_address.deviceAddress + shader_handle_size_aligned,
		.stride        = shader_handle_size_aligned,
		.size          = shader_handle_size_aligned,
	};

	VkStridedDeviceAddressRegionKHR hit_shader_table_entry = {
		.deviceAddress = shader_table_buffer_device_address.deviceAddress + (shader_handle_size_aligned * 2),
		.stride        = shader_handle_size_aligned,
		.size          = shader_handle_size_aligned,
	};

	VkStridedDeviceAddressRegionKHR callable_shader_table_entry = { };

	ext.vkCmdTraceRaysKHR(
		command_buffer,
		&raygen_shader_table_entry,
		&miss_shader_table_entry,
		&hit_shader_table_entry,
		&callable_shader_table_entry,
		width_px,
		height_px,
		1
	);

	image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;

	vkCmdPipelineBarrier(
		command_buffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0,
		0,
		NULL,
		0,
		NULL,
		1,
		&image_memory_barrier
	);

	VkBufferImageCopy buffer_image_copy = {
		.bufferOffset                    = 0,
		.bufferRowLength                 = 0,
		.bufferImageHeight               = 0,
		.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
		.imageSubresource.mipLevel       = 0,
		.imageSubresource.baseArrayLayer = 0,
		.imageSubresource.layerCount     = 1,
		.imageOffset.x                   = 0,
		.imageOffset.y                   = 0,
		.imageOffset.z                   = 0,
		.imageExtent.width               = width_px,
		.imageExtent.height              = height_px,
		.imageExtent.depth               = 1,
	};

	vkCmdCopyImageToBuffer(command_buffer,
	                       image,
	                       VK_IMAGE_LAYOUT_GENERAL,
	                       image_buffer,
	                       1,
	                       &buffer_image_copy);


	if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
		return false;
	}

	// submit command buffer
	if (vkQueueSubmit(graphics_queue, 1, &submit_info, fence) != VK_SUCCESS) {
		return false;
	}

	if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
		return false;
	}

	vkResetFences(device, 1, &fence);

	// read back image data into output buffer
	if (vkMapMemory(device, image_buffer_memory, 0, image_buffer_size, 0, &mapped) != VK_SUCCESS) {
		return false;
	}

	uint8_t *image_src_data = mapped;
	for (uint32_t s = 0, d = 0; s < image_buffer_size; s += 4, d += 3) {
		texel_buffer[d+0] = image_src_data[s+0];
		texel_buffer[d+1] = image_src_data[s+1];
		texel_buffer[d+2] = image_src_data[s+2];
	}

	vkUnmapMemory(device, image_buffer_memory);

	// free all resources
	vkDestroyDescriptorPool(device, descriptor_pool, NULL);
	vkFreeMemory(device, shader_table_buffer_memory, NULL);
	vkDestroyBuffer(device, shader_table_buffer, NULL);
	vkDestroyPipeline(device, ray_tracing_pipeline, NULL);
	vkDestroyPipelineLayout(device, pipeline_layout, NULL);
	vkDestroyDescriptorSetLayout(device, descriptor_set_layout, NULL);
	vkFreeMemory(device, image_buffer_memory, NULL);
	vkDestroyBuffer(device, image_buffer, NULL);
	ext.vkDestroyAccelerationStructureKHR(device, top_level_acceleration_structure, NULL);
	vkFreeMemory(device, top_level_acceleration_structure_buffer_memory, NULL);
	vkDestroyBuffer(device, top_level_acceleration_structure_buffer, NULL);
	ext.vkDestroyAccelerationStructureKHR(device, bottom_level_acceleration_structure, NULL);
	vkFreeMemory(device, bottom_level_acceleration_structure_buffer_memory, NULL);
	vkDestroyBuffer(device, bottom_level_acceleration_structure_buffer, NULL);
	vkFreeMemory(device, transform_matrix_buffer_memory, NULL);
	vkDestroyBuffer(device, transform_matrix_buffer, NULL);
	vkFreeMemory(device, index_buffer_memory, NULL);
	vkDestroyBuffer(device, index_buffer, NULL);
	vkFreeMemory(device, vertex_buffer_memory, NULL);
	vkDestroyBuffer(device, vertex_buffer, NULL);
	vkDestroyImageView(device, image_view, NULL);
	vkFreeMemory(device, image_memory, NULL);
	vkDestroyImage(device, image, NULL);
	vkDestroyFence(device, fence, NULL);
	vkDestroyCommandPool(device, command_pool, NULL);
	vkDestroyDevice(device, NULL);
	ext.vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, NULL);
	vkDestroyInstance(instance, NULL);

	// report successful render
	return true;
}

int main() {
	uint8_t *texel_buffer = malloc(IMAGE_WIDTH * IMAGE_HEIGHT * 3);
	if (!ray_trace_image(texel_buffer, IMAGE_WIDTH, IMAGE_HEIGHT)) {
		fputs("render failed\n", stderr);
		return 1;
	}
	save_rgb8_image_to_ppm("image.ppm", IMAGE_WIDTH, IMAGE_HEIGHT, texel_buffer);
	free(texel_buffer);
	return 0;
}
