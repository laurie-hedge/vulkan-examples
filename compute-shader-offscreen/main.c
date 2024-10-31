#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define IMAGE_WIDTH  768
#define IMAGE_HEIGHT 512

struct {
	PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;
	PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT;
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

bool generate_image(uint8_t *texel_buffer, uint16_t width_px, uint16_t height_px) {
	// create vulkan instance
	VkApplicationInfo app_info = {
		.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName   = "Offscreen Compute Shader Example",
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

	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	uint32_t compute_queue_index;
	for (uint32_t i = 0; i < physical_device_count; ++i) {
		VkPhysicalDeviceProperties device_properties;
		vkGetPhysicalDeviceProperties(physical_devices[i], &device_properties);
		if (device_properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
		    device_properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
			continue;
		}

		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, NULL);
		VkQueueFamilyProperties queue_family_properties[queue_family_count];
		vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i],
		                                         &queue_family_count,
		                                         queue_family_properties);
		compute_queue_index = UINT32_MAX;
		for (uint32_t j = 0; j < queue_family_count; ++j) {
			if (queue_family_properties[j].queueFlags & VK_QUEUE_COMPUTE_BIT) {
				compute_queue_index = j;
				break;
			}
		}
		if (compute_queue_index == UINT32_MAX) {
			continue;
		}

		physical_device = physical_devices[i];
		break;
	}

	if (physical_device == VK_NULL_HANDLE) {
		return false;
	}

	// create device
	float const queue_priority = 1.0f;
	VkDeviceQueueCreateInfo device_queue_create_info = {
		.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = compute_queue_index,
		.queueCount       = 1,
		.pQueuePriorities = &queue_priority,
	};

	VkDeviceCreateInfo device_create_info = {
		.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount    = 1,
		.pQueueCreateInfos       = &device_queue_create_info,
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

	// get compute queue from device
	VkQueue compute_queue;
	vkGetDeviceQueue(device, compute_queue_index, 0, &compute_queue);

	// create command pool
	VkCommandPoolCreateInfo command_pool_create_info = {
		.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = compute_queue_index,
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

	// create destination buffer for image data
	uint32_t const image_buffer_size = width_px * height_px * 4;

	VkBufferCreateInfo image_buffer_create_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size  = image_buffer_size,
		.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	};

	VkBuffer image_buffer;
	if (vkCreateBuffer(device, &image_buffer_create_info, NULL, &image_buffer) != VK_SUCCESS) {
		return false;
	}

	vkGetBufferMemoryRequirements(device, image_buffer, &memory_requirements);

	uint32_t const memory_types_matching_requirements =
		memory_requirements.memoryTypeBits & host_coherent_memory_types;
	if (memory_types_matching_requirements == 0) {
		return false;
	}

	VkMemoryAllocateInfo image_buffer_memory_allocate_info = {
		.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize  = memory_requirements.size,
		.memoryTypeIndex = __builtin_ctz(memory_types_matching_requirements),
	};

	VkDeviceMemory image_buffer_memory;
	if (vkAllocateMemory(device,
	                     &image_buffer_memory_allocate_info,
	                     NULL,
	                     &image_buffer_memory) != VK_SUCCESS) {
		return false;
	}

	if (vkBindBufferMemory(device, image_buffer, image_buffer_memory, 0) != VK_SUCCESS) {
		return false;
	}

	// create descriptor set layout
	VkDescriptorSetLayoutBinding descriptor_set_layout_binding = {
		.binding         = 0,
		.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
		.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT,
	};

	VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {
		.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings    = &descriptor_set_layout_binding,
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

	// create shader module
	VkShaderModule comp_shader_module;
	create_shader_module(device, "comp.spv", &comp_shader_module);

	// create compute pipeline
	VkComputePipelineCreateInfo compute_pipeline_create_info = {
		.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.layout       = pipeline_layout,
		.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT,
		.stage.module = comp_shader_module,
		.stage.pName  = "main",
	};

	VkPipeline compute_pipeline;
	if (vkCreateComputePipelines(device,
	                             VK_NULL_HANDLE,
	                             1,
	                             &compute_pipeline_create_info,
	                             NULL,
	                             &compute_pipeline) != VK_SUCCESS) {
		return false;
	}

	// free shader module
	vkDestroyShaderModule(device, comp_shader_module, NULL);

	// create descriptor pool
	VkDescriptorPoolSize descriptor_pool_size = {
		.type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.descriptorCount = 1,
	};

	VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
		.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.poolSizeCount = 1,
		.pPoolSizes    = &descriptor_pool_size,
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
	VkDescriptorImageInfo descriptor_image_info = {
		.imageView   = image_view,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VkWriteDescriptorSet write_descriptor_set = {
		.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet          = descriptor_set,
		.dstBinding      = 0,
		.descriptorCount = 1,
		.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.pImageInfo      = &descriptor_image_info,
	};

	vkUpdateDescriptorSets(device, 1, &write_descriptor_set, 0, NULL);

	// record command buffer
	VkCommandBufferBeginInfo command_buffer_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
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

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline);

	vkCmdBindDescriptorSets(
		command_buffer,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		pipeline_layout,
		0,
		1,
		&descriptor_set,
		0,
		NULL
	);

	vkCmdDispatch(command_buffer, width_px / 32, height_px / 32, 1);

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
	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers    = &command_buffer,
	};
	if (vkQueueSubmit(compute_queue, 1, &submit_info, fence) != VK_SUCCESS) {
		return false;
	}

	if (vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
		return false;
	}

	// read back image data into output buffer
	void *mapped;
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
	vkDestroyPipeline(device, compute_pipeline, NULL);
	vkDestroyPipelineLayout(device, pipeline_layout, NULL);
	vkDestroyDescriptorSetLayout(device, descriptor_set_layout, NULL);
	vkFreeMemory(device, image_buffer_memory, NULL);
	vkDestroyBuffer(device, image_buffer, NULL);
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
	if (!generate_image(texel_buffer, IMAGE_WIDTH, IMAGE_HEIGHT)) {
		fputs("render failed\n", stderr);
		return 1;
	}
	save_rgb8_image_to_ppm("image.ppm", IMAGE_WIDTH, IMAGE_HEIGHT, texel_buffer);
	free(texel_buffer);
	return 0;
}
