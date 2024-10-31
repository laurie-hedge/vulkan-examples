#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define WINDOW_WIDTH  800
#define WINDOW_HEIGHT 600
#define APP_NAME      "Onscreen Mesh Shader Example"

struct {
	PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;
	PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT;
	PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT;
} ext;

#define LOAD_EXTENSION_FUNC(FuncName) \
	ext.FuncName = (PFN_##FuncName)vkGetInstanceProcAddr(instance, #FuncName); \
	if (!ext.FuncName) return false

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

bool run_rasterizer() {
	// create window
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	GLFWwindow *window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, APP_NAME, NULL, NULL);

	// create vulkan instance
	VkApplicationInfo app_info = {
		.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName   = APP_NAME,
		.applicationVersion = VK_MAKE_VERSION(1, 0, 0),
		.pEngineName        = "No Engine",
		.engineVersion      = VK_MAKE_VERSION(1, 0, 0),
		.apiVersion         = VK_API_VERSION_1_1,
	};

	uint32_t glfw_extension_count = 0;
	char const **glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
	char const *extension_names[glfw_extension_count+1];
	for (uint32_t i = 0; i < glfw_extension_count; ++i) {
		extension_names[i] = glfw_extensions[i];
	}
	extension_names[glfw_extension_count] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

	char const *validation_layers[] = { "VK_LAYER_KHRONOS_validation" };

	VkInstanceCreateInfo instance_create_info = {
		.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo        = &app_info,
		.enabledLayerCount       = 1,
		.ppEnabledLayerNames     = validation_layers,
		.enabledExtensionCount   = glfw_extension_count + 1,
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

	// create surface
	VkSurfaceKHR surface;
	if (glfwCreateWindowSurface(instance, window, NULL, &surface) != VK_SUCCESS) {
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

	#define NUM_REQUIRED_EXTENSIONS 4
	char const *required_extensions[NUM_REQUIRED_EXTENSIONS] = {
		VK_EXT_MESH_SHADER_EXTENSION_NAME,
		VK_KHR_SPIRV_1_4_EXTENSION_NAME,
		VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	uint32_t graphics_queue_index, present_queue_index;
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
		present_queue_index  = UINT32_MAX;
		for (uint32_t j = 0; j < queue_family_count; ++j) {
			if (queue_family_properties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				graphics_queue_index = j;
			}
			VkBool32 present_support = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(physical_devices[i], j, surface, &present_support);
			if (present_support) {
				present_queue_index = j;
			}
		}
		if (graphics_queue_index == UINT32_MAX || present_queue_index == UINT32_MAX) {
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
	VkDeviceQueueCreateInfo device_queue_create_infos[2] = {
		{
			.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = graphics_queue_index,
			.queueCount       = 1,
			.pQueuePriorities = &queue_priority,
		},
		{
			.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = present_queue_index,
			.queueCount       = 1,
			.pQueuePriorities = &queue_priority,
		}
	};

	uint32_t queue_indices[2] = { graphics_queue_index, present_queue_index };
	uint32_t const num_queues = graphics_queue_index == present_queue_index ? 1 : 2;

	VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_device_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
		.meshShader = VK_TRUE,
	};

	VkPhysicalDeviceFeatures2 device_features = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = (void*)&mesh_shader_device_features,
	};

	VkDeviceCreateInfo device_create_info = {
		.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext                   = (void*)&device_features,
		.queueCreateInfoCount    = num_queues,
		.pQueueCreateInfos       = device_queue_create_infos,
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

	// get queues from device
	VkQueue graphics_queue;
	vkGetDeviceQueue(device, graphics_queue_index, 0, &graphics_queue);
	VkQueue present_queue;
	vkGetDeviceQueue(device, present_queue_index, 0, &present_queue);

	// create swap chain
	VkSurfaceCapabilitiesKHR swap_chain_capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &swap_chain_capabilities);

	uint32_t surface_format_count;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &surface_format_count, NULL);
	VkSurfaceFormatKHR surface_formats[surface_format_count];
	vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &surface_format_count, surface_formats);

	VkSurfaceFormatKHR surface_format = surface_formats[0];
	for (uint32_t i = 0; i < surface_format_count; ++i) {
		if (surface_formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
		    surface_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			surface_format = surface_formats[i];
			break;
		}
	}

	uint32_t present_mode_count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, NULL);
	if (present_mode_count == 0) {
		return false;
	}
	VkPresentModeKHR present_modes[present_mode_count];
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, present_modes);

	VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
	for (uint32_t i = 0; i < present_mode_count; ++i) {
		if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
			present_mode = present_modes[i];
			break;
		}
	}

	VkExtent2D surface_extent = swap_chain_capabilities.currentExtent;
	if (swap_chain_capabilities.currentExtent.width == UINT32_MAX) {
		int width, height;
		glfwGetFramebufferSize(window, &width, &height);
		surface_extent.width  = width > swap_chain_capabilities.maxImageExtent.width
		                      ? swap_chain_capabilities.maxImageExtent.width
		                      : width < swap_chain_capabilities.minImageExtent.width
		                      ? swap_chain_capabilities.minImageExtent.width
		                      : width;
		surface_extent.height = height > swap_chain_capabilities.maxImageExtent.height
		                      ? swap_chain_capabilities.maxImageExtent.height
		                      : height < swap_chain_capabilities.minImageExtent.height
		                      ? swap_chain_capabilities.minImageExtent.height
		                      : height;
	}

	uint32_t image_count = swap_chain_capabilities.minImageCount + 1;
	if (swap_chain_capabilities.maxImageCount > 0 && image_count > swap_chain_capabilities.maxImageCount) {
		image_count = swap_chain_capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR swapchain_create_info = {
		.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface               = surface,
		.minImageCount         = image_count,
		.imageFormat           = surface_format.format,
		.imageColorSpace       = surface_format.colorSpace,
		.imageExtent           = surface_extent,
		.imageArrayLayers      = 1,
		.imageUsage            = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode      = num_queues > 1 ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = num_queues,
		.pQueueFamilyIndices   = queue_indices,
		.preTransform          = swap_chain_capabilities.currentTransform,
		.compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode           = present_mode,
		.clipped               = VK_TRUE,
		.oldSwapchain          = VK_NULL_HANDLE,
	};

	VkSwapchainKHR swap_chain;
	if (vkCreateSwapchainKHR(device, &swapchain_create_info, NULL, &swap_chain) != VK_SUCCESS) {
		return false;
	}

	// get swap chain images
	vkGetSwapchainImagesKHR(device, swap_chain, &image_count, NULL);
	VkImage swap_chain_images[image_count];
	vkGetSwapchainImagesKHR(device, swap_chain, &image_count, swap_chain_images);

	// create swap chain image views
	VkImageView swap_chain_image_views[image_count];
	for (uint32_t i = 0; i < image_count; ++i) {
		VkImageViewCreateInfo image_view_create_info = {
			.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image                           = swap_chain_images[i],
			.viewType                        = VK_IMAGE_VIEW_TYPE_2D,
			.format                          = surface_format.format,
			.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY,
			.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY,
			.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY,
			.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY,
			.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.baseMipLevel   = 0,
			.subresourceRange.levelCount     = 1,
			.subresourceRange.baseArrayLayer = 0,
			.subresourceRange.layerCount     = 1,
		};
		if (vkCreateImageView(device, &image_view_create_info, NULL, &swap_chain_image_views[i]) != VK_SUCCESS) {
			return false;
		}

	}

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

	// create semaphores
	VkSemaphoreCreateInfo semaphore_create_info = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};

	VkSemaphore image_available_semaphore;
	if (vkCreateSemaphore(device, &semaphore_create_info, NULL, &image_available_semaphore) != VK_SUCCESS) {
		return false;
	}
	VkSemaphore render_finished_semaphore;
	if (vkCreateSemaphore(device, &semaphore_create_info, NULL, &render_finished_semaphore) != VK_SUCCESS) {
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

	// create render pass
	VkAttachmentDescription colour_attachment_description = {
		.format         = surface_format.format,
		.samples        = VK_SAMPLE_COUNT_1_BIT,
		.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
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
	VkShaderModule mesh_shader_module;
	create_shader_module(device, "mesh.spv", &mesh_shader_module);

	VkShaderModule frag_shader_module;
	create_shader_module(device, "frag.spv", &frag_shader_module);
	
	// create rasterization pipeline
	VkPipelineShaderStageCreateInfo shader_stage_create_infos[2] = {
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
		.width    = (float)surface_extent.width,
		.height   = (float)surface_extent.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	VkRect2D scissor = {
		.offset  = { 0, 0 },
		.extent  = surface_extent,
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
		.stageCount          = 2,
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

	// create swap chain framebuffers
	VkFramebuffer swap_chain_framebuffers[image_count];
	for (uint32_t i = 0; i < image_count; ++i) {
		VkFramebufferCreateInfo framebuffer_create_info = {
			.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass      = render_pass,
			.attachmentCount = 1,
			.pAttachments    = &swap_chain_image_views[i],
			.width           = surface_extent.width,
			.height          = surface_extent.height,
			.layers          = 1,
		};

		if (vkCreateFramebuffer(device,
		                        &framebuffer_create_info,
		                        NULL,
		                        &swap_chain_framebuffers[i]) != VK_SUCCESS) {
			return false;
		}
	}

	// main app loop
	while (!glfwWindowShouldClose(window)) {
		// handle window system events
		glfwPollEvents();

		// acquire next swap chain image
		uint32_t swap_chain_image_index;
		vkAcquireNextImageKHR(device,
		                      swap_chain,
		                      UINT64_MAX,
		                      image_available_semaphore,
		                      VK_NULL_HANDLE,
		                      &swap_chain_image_index);

		// record command buffer
		VkCommandBufferBeginInfo command_buffer_begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};
		if (vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) != VK_SUCCESS) {
			return false;
		}

		VkClearValue clear_color = {{{ 0.0f, 0.0f, 0.0f, 1.0f }}};
		VkRenderPassBeginInfo render_pass_begin_info = {
			.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass        = render_pass,
			.framebuffer       = swap_chain_framebuffers[swap_chain_image_index],
			.renderArea.offset = { 0, 0 },
			.renderArea.extent = surface_extent,
			.clearValueCount   = 1,
			.pClearValues      = &clear_color,
		};
		vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);

		ext.vkCmdDrawMeshTasksEXT(command_buffer, 1, 1, 1);

		vkCmdEndRenderPass(command_buffer);

		if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
			return false;
		}

		VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submit_info = {
			.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount   = 1,
			.pWaitSemaphores      = &image_available_semaphore,
			.pWaitDstStageMask    = &wait_stage,
			.commandBufferCount   = 1,
			.pCommandBuffers      = &command_buffer,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores    = &render_finished_semaphore,
		};

		if (vkQueueSubmit(graphics_queue, 1, &submit_info, fence) != VK_SUCCESS) {
			return false;
		}

		VkPresentInfoKHR present_info = {
			.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores    = &render_finished_semaphore,
			.swapchainCount     = 1,
			.pSwapchains        = &swap_chain,
			.pImageIndices      = &swap_chain_image_index,
		};
		vkQueuePresentKHR(present_queue, &present_info);

		vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
		vkResetFences(device, 1, &fence);
	}

	// wait for all renders to finish before cleanup
	vkDeviceWaitIdle(device);

	// free all resources
	for (uint32_t i = 0; i < image_count; ++i) {
		vkDestroyFramebuffer(device, swap_chain_framebuffers[i], NULL);
	}
	vkDestroyPipeline(device, graphics_pipeline, NULL);
	vkDestroyPipelineLayout(device, pipeline_layout, NULL);
	vkDestroyRenderPass(device, render_pass, NULL);
	for (uint32_t i = 0; i < image_count; ++i) {
		vkDestroyImageView(device, swap_chain_image_views[i], NULL);
	}
	vkDestroyFence(device, fence, NULL);
	vkDestroySemaphore(device, render_finished_semaphore, NULL);
	vkDestroySemaphore(device, image_available_semaphore, NULL);
	vkDestroyCommandPool(device, command_pool, NULL);
	vkDestroySwapchainKHR(device, swap_chain, NULL);
	vkDestroyDevice(device, NULL);
	vkDestroySurfaceKHR(instance, surface, NULL);
	ext.vkDestroyDebugUtilsMessengerEXT(instance, debug_messenger, NULL);
	vkDestroyInstance(instance, NULL);
	glfwDestroyWindow(window);
	glfwTerminate();

	// report successful render
	return true;
}

int main() {
	if (!run_rasterizer()) {
		fputs("run failed\n", stderr);
		return 1;
	}
	return 0;
}
