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
	PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT;
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

bool render_image(uint8_t *texel_buffer, uint16_t width_px, uint16_t height_px) {
	// create vulkan instance
	VkApplicationInfo app_info = {
		.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName   = "Offscreen Task Shader Example",
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
	LOAD_EXTENSION_FUNC(vkCmdDrawMeshTasksEXT);

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

	#define NUM_REQUIRED_EXTENSIONS 3
	char const *required_extensions[NUM_REQUIRED_EXTENSIONS] = {
		VK_EXT_MESH_SHADER_EXTENSION_NAME,
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
	float const queue_priority = 1.0f;
	VkDeviceQueueCreateInfo device_queue_create_info = {
		.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = graphics_queue_index,
		.queueCount       = 1,
		.pQueuePriorities = &queue_priority,
	};

	VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_device_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
		.taskShader = VK_TRUE,
		.meshShader = VK_TRUE,
	};

	VkPhysicalDeviceFeatures2 device_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = (void*)&mesh_shader_device_features,
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
		.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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

	// create render pass
	VkAttachmentDescription colour_attachment_description = {
		.format         = VK_FORMAT_R8G8B8A8_UNORM,
		.samples        = VK_SAMPLE_COUNT_1_BIT,
		.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout    = VK_IMAGE_LAYOUT_GENERAL,
	};

	VkAttachmentReference colour_attachment_ref = {
		.attachment = 0,
		.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	VkSubpassDescription subpass_description = {
		.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments    = &colour_attachment_ref,
	};

	VkSubpassDependency subpass_dependency = {
		.srcSubpass    = VK_SUBPASS_EXTERNAL,
		.dstSubpass    = 0,
		.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	};

	VkRenderPassCreateInfo render_pass_create_info = {
		.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments    = &colour_attachment_description,
		.subpassCount    = 1,
		.pSubpasses      = &subpass_description,
		.dependencyCount = 1,
		.pDependencies   = &subpass_dependency,
	};

	VkRenderPass render_pass;
	if (vkCreateRenderPass(device, &render_pass_create_info, NULL, &render_pass) != VK_SUCCESS) {
		return false;
	}

	// create pipeline layout
	VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
		.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
	};

	VkPipelineLayout pipeline_layout;
	if (vkCreatePipelineLayout(device, &pipeline_layout_create_info, NULL, &pipeline_layout) != VK_SUCCESS) {
		return false;
	}

	// create shader modules
	VkShaderModule task_shader_module;
	create_shader_module(device, "task.spv", &task_shader_module);

	VkShaderModule mesh_shader_module;
	create_shader_module(device, "mesh.spv", &mesh_shader_module);

	VkShaderModule frag_shader_module;
	create_shader_module(device, "frag.spv", &frag_shader_module);
	
	// create rasterization pipeline
	VkPipelineShaderStageCreateInfo shader_stage_create_infos[3] = {
		{
			.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage  = VK_SHADER_STAGE_TASK_BIT_EXT,
			.module = task_shader_module,
			.pName  = "main",
		},
		{
			.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage  = VK_SHADER_STAGE_MESH_BIT_EXT,
			.module = mesh_shader_module,
			.pName  = "main",
		},
		{
			.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = frag_shader_module,
			.pName  = "main",
		}
	};

	VkViewport viewport = {
		.x        = 0.0f,
		.y        = 0.0f,
		.width    = (float)width_px,
		.height   = (float)height_px,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent.width  = width_px,
		.extent.height = height_px,
	};

	VkPipelineViewportStateCreateInfo viewport_state_create_info = {
		.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports    = &viewport,
		.scissorCount  = 1,
		.pScissors     = &scissor,
	};

	VkPipelineRasterizationStateCreateInfo rasterization_state_create_info = {
		.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable        = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode             = VK_POLYGON_MODE_FILL,
		.lineWidth               = 1.0f,
		.cullMode                = VK_CULL_MODE_BACK_BIT,
		.frontFace               = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable         = VK_FALSE,
	};

	VkPipelineMultisampleStateCreateInfo multisampling_state_create_info = {
		.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.sampleShadingEnable  = VK_FALSE,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	VkPipelineColorBlendAttachmentState colour_blend_attachment_state = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		.blendEnable = VK_FALSE,
	};

	VkPipelineColorBlendStateCreateInfo color_blend_state_create_info = {
		.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable   = VK_FALSE,
		.attachmentCount = 1,
		.pAttachments    = &colour_blend_attachment_state,
	};

	VkGraphicsPipelineCreateInfo graphics_pipeline_create_info = {
		.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount          = 3,
		.pStages             = shader_stage_create_infos,
		.pViewportState      = &viewport_state_create_info,
		.pRasterizationState = &rasterization_state_create_info,
		.pMultisampleState   = &multisampling_state_create_info,
		.pColorBlendState    = &color_blend_state_create_info,
		.layout              = pipeline_layout,
		.renderPass          = render_pass,
		.subpass             = 0,
	};

	VkPipeline graphics_pipeline;
	if (vkCreateGraphicsPipelines(device,
	                              VK_NULL_HANDLE,
	                              1,
	                              &graphics_pipeline_create_info,
	                              NULL,
	                              &graphics_pipeline) != VK_SUCCESS) {
		return false;
	}

	// free shader modules
	vkDestroyShaderModule(device, frag_shader_module, NULL);
	vkDestroyShaderModule(device, mesh_shader_module, NULL);
	vkDestroyShaderModule(device, task_shader_module, NULL);

	// create framebuffer
	VkFramebufferCreateInfo framebuffer_create_info = {
		.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass      = render_pass,
		.attachmentCount = 1,
		.pAttachments    = &image_view,
		.width           = width_px,
		.height          = height_px,
		.layers          = 1,
	};

	VkFramebuffer framebuffer;
	if (vkCreateFramebuffer(device,
	                        &framebuffer_create_info,
	                        NULL,
	                        &framebuffer) != VK_SUCCESS) {
		return false;
	}

	// record command buffer
	VkCommandBufferBeginInfo command_buffer_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	if (vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) != VK_SUCCESS) {
		return false;
	}

	VkClearValue clear_color = {{{ 0.0f, 0.0f, 0.0f, 1.0f }}};
	VkRenderPassBeginInfo render_pass_begin_info = {
		.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass               = render_pass,
		.framebuffer              = framebuffer,
		.renderArea.offset        = { 0, 0 },
		.renderArea.extent.width  = width_px,
		.renderArea.extent.height = height_px,
		.clearValueCount          = 1,
		.pClearValues             = &clear_color,
	};
	vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

	ext.vkCmdDrawMeshTasksEXT(command_buffer, 1, 1, 1);

	vkCmdEndRenderPass(command_buffer);

	VkImageMemoryBarrier image_memory_barrier = {
		.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.oldLayout                   = VK_IMAGE_LAYOUT_GENERAL,
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
	if (vkQueueSubmit(graphics_queue, 1, &submit_info, fence) != VK_SUCCESS) {
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
	vkDestroyFramebuffer(device, framebuffer, NULL);
	vkDestroyPipeline(device, graphics_pipeline, NULL);
	vkDestroyPipelineLayout(device, pipeline_layout, NULL);
	vkDestroyRenderPass(device, render_pass, NULL);
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
	if (!render_image(texel_buffer, IMAGE_WIDTH, IMAGE_HEIGHT)) {
		fputs("render failed\n", stderr);
		return 1;
	}
	save_rgb8_image_to_ppm("image.ppm", IMAGE_WIDTH, IMAGE_HEIGHT, texel_buffer);
	free(texel_buffer);
	return 0;
}
