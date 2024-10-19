#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <vector>

#define USE_MESH_SHADER 1

static constexpr uint32_t WINDOW_WIDTH = 800;
static constexpr uint32_t WINDOW_HEIGHT = 600;
static char const * const APP_NAME = "Mesh Shader Test";
static const std::vector<char const *> VALIDATION_LAYERS = {
	"VK_LAYER_KHRONOS_validation"
};
static const std::vector<char const *> REQUIRED_EXTENSIONS = {
	VK_EXT_MESH_SHADER_EXTENSION_NAME,
	VK_KHR_SPIRV_1_4_EXTENSION_NAME,
	VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

struct ExtFuncs {
	PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;
	PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT;
	PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasksEXT;
} ext;

struct Context {
	GLFWwindow* window;
	VkInstance instance;
	VkDebugUtilsMessengerEXT debug_messenger;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkSurfaceKHR surface;
	VkDevice device;
	VkQueue graphics_queue;
	VkQueue present_queue;
	VkSwapchainKHR swap_chain;
	std::vector<VkImage> swap_chain_images;
	VkFormat swap_chain_image_format;
	VkExtent2D swap_chain_extent;
	std::vector<VkImageView> swap_chain_image_views;
	VkRenderPass render_pass;
	VkPipelineLayout pipeline_layout;
	VkPipeline graphics_pipeline;
	std::vector<VkFramebuffer> swap_chain_framebuffers;
	VkCommandPool command_pool;
	VkCommandBuffer command_buffer;
	VkSemaphore image_available_semaphore;
	VkSemaphore render_finished_semaphore;
	VkFence in_flight_fence;
} ctx;

static void error_quit(char const *error_message) {
	std::cerr << error_message << std::endl;
	exit(-1);
}

static void check_result(VkResult result, char const *error_message) {
	if (result != VK_SUCCESS) {
		error_quit(error_message);
	}
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
	VkDebugUtilsMessageTypeFlagsEXT message_type,
	VkDebugUtilsMessengerCallbackDataEXT const *callback_data,
	void *user_data) {
	if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		std::cerr << "validation layer: " << callback_data->pMessage << std::endl;
	}
	return VK_FALSE;
}

static std::vector<char> read_binary_file(char const *filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) {
		error_quit("Failed to read file");
	}

	size_t file_size = (size_t)file.tellg();
	std::vector<char> buffer(file_size);

	file.seekg(0);
	file.read(buffer.data(), file_size);

	file.close();

	return buffer;
}

static void check_validation_layer_support() {
	uint32_t layer_count;
	vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

	std::vector<VkLayerProperties> available_layers(layer_count);
	vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

	for (char const * layer_name : VALIDATION_LAYERS) {
		bool layer_found = false;

		for (auto const &layer_properties : available_layers) {
			if (strcmp(layer_name, layer_properties.layerName) == 0) {
				layer_found = true;
				break;
			}
		}

		if (!layer_found) {
			error_quit("Validation layers unsupported");
		}
	}
}

static void setup_window() {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // prevent creating OpenGL context
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);   // disallow window resizing

	ctx.window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, APP_NAME, nullptr, nullptr);
}

static void setup_instance() {
	check_validation_layer_support();

	VkApplicationInfo app_info {};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = APP_NAME;
	app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.pEngineName = "No Engine";
	app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.apiVersion = VK_API_VERSION_1_1;

	std::vector<char const *> extension_names;
	uint32_t glfw_extension_count = 0;
	char const **glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
	for (uint32_t i = 0; i < glfw_extension_count; ++i) {
		extension_names.push_back(glfw_extensions[i]);
	}
	extension_names.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	VkInstanceCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo = &app_info;
	create_info.enabledExtensionCount = static_cast<uint32_t>(extension_names.size());
	create_info.ppEnabledExtensionNames = extension_names.data();
	create_info.enabledLayerCount = 0;
	create_info.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
	create_info.ppEnabledLayerNames = VALIDATION_LAYERS.data();

	VkResult result = vkCreateInstance(&create_info, nullptr, &ctx.instance);
	check_result(result, "Failed to create instance");
}

static void setup_extension_functions() {
	ext.vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(ctx.instance, "vkCreateDebugUtilsMessengerEXT");
	if (!ext.vkCreateDebugUtilsMessengerEXT) {
		error_quit("vkCreateDebugUtilsMessengerEXT function not found");
	}
	ext.vkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(ctx.instance, "vkDestroyDebugUtilsMessengerEXT");
	if (!ext.vkDestroyDebugUtilsMessengerEXT) {
		error_quit("vkDestroyDebugUtilsMessengerEXT function not found");
	}
	ext.vkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT)vkGetInstanceProcAddr(ctx.instance, "vkCmdDrawMeshTasksEXT");
	if (!ext.vkCmdDrawMeshTasksEXT) {
		error_quit("vkCmdDrawMeshTasksEXT function not found");
	}
}

static void setup_debug_messenger() {
	VkDebugUtilsMessengerCreateInfoEXT create_info {};
	create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	create_info.pfnUserCallback = debug_callback;
	create_info.pUserData = nullptr;
	VkResult result = ext.vkCreateDebugUtilsMessengerEXT(ctx.instance, &create_info, nullptr, &ctx.debug_messenger);
	check_result(result, "Failed to create debug messenger");
}

struct QueueFamilyIndices {
	std::optional<uint32_t> graphics_family;
	std::optional<uint32_t> present_family;
};

static QueueFamilyIndices find_queues(VkPhysicalDevice device) {
	QueueFamilyIndices indices;
	uint32_t queue_family_count = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
	std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());
	for (uint32_t i = 0; i < queue_family_count; ++i) {
		if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphics_family = i;
		}
		VkBool32 present_support = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, ctx.surface, &present_support);
		if (present_support) {
			indices.present_family = i;
		}
	}
	return indices;
}

struct SwapChainSupportDetails {
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> present_modes;
};

static SwapChainSupportDetails find_swap_chains(VkPhysicalDevice device) {
	SwapChainSupportDetails details;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, ctx.surface, &details.capabilities);

	uint32_t format_count;
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, ctx.surface, &format_count, nullptr);
	details.formats.resize(format_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, ctx.surface, &format_count, details.formats.data());

	uint32_t present_mode_count;
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, ctx.surface, &present_mode_count, nullptr);
	if (present_mode_count != 0) {
		details.present_modes.resize(present_mode_count);
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, ctx.surface, &present_mode_count, details.present_modes.data());
	}

	return details;
}

static void setup_surface() {
	VkResult result = glfwCreateWindowSurface(ctx.instance, ctx.window, nullptr, &ctx.surface);
	check_result(result, "Failed to create window surface");
}

static bool device_usable(VkPhysicalDevice device) {
	VkPhysicalDeviceProperties device_properties;
	vkGetPhysicalDeviceProperties(device, &device_properties);

	if (device_properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
		return false;
	}

	uint32_t extension_count = 0;
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);
	std::vector<VkExtensionProperties> extensions(extension_count);
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, extensions.data());
	for (char const *required_extension : REQUIRED_EXTENSIONS) {
		bool extension_found = false;
		for (auto const &extension : extensions) {
			if (strcmp(required_extension, extension.extensionName) == 0) {
				extension_found = true;
				break;
			}
		}
		if (!extension_found) {
			return false;
		}
	}

	const auto queue_families = find_queues(device);
	if (!queue_families.graphics_family || !queue_families.present_family) {
		return false;
	}

	const auto swap_chain_support = find_swap_chains(device);
	if (swap_chain_support.formats.empty() || swap_chain_support.present_modes.empty()) {
		return false;
	}

	return true;
}

static void setup_device() {
	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(ctx.instance, &device_count, nullptr);
	if (device_count == 0) {
		error_quit("No devices found");
	}

	std::vector<VkPhysicalDevice> devices(device_count);
	vkEnumeratePhysicalDevices(ctx.instance, &device_count, devices.data());
	for (auto const &device : devices) {
		if (device_usable(device)) {
			ctx.physical_device = device;
			break;
		}
	}
	if (ctx.physical_device == VK_NULL_HANDLE) {
		error_quit("No usable device found");
	}

	static float const queue_priority = 1.0f;
	const auto queue_families = find_queues(ctx.physical_device);

	std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
	std::set<uint32_t> unique_queue_families = { queue_families.graphics_family.value(), queue_families.present_family.value() };

	for (uint32_t queue_family : unique_queue_families) {
		VkDeviceQueueCreateInfo queue_create_info {};
		queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_create_info.queueFamilyIndex = queue_family;
		queue_create_info.queueCount = 1;
		queue_create_info.pQueuePriorities = &queue_priority;
		queue_create_infos.push_back(queue_create_info);
	}

	VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_device_features {};
	mesh_shader_device_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
	mesh_shader_device_features.meshShader = VK_TRUE;

	VkPhysicalDeviceFeatures2 device_features {};
	device_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	device_features.pNext = (void*)&mesh_shader_device_features;

	VkDeviceCreateInfo device_create_info {};
	device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	device_create_info.pNext = (void*)&device_features;
	device_create_info.pQueueCreateInfos = queue_create_infos.data();
	device_create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
	device_create_info.enabledExtensionCount = static_cast<uint32_t>(REQUIRED_EXTENSIONS.size());
	device_create_info.ppEnabledExtensionNames = REQUIRED_EXTENSIONS.data();
	device_create_info.enabledLayerCount = static_cast<uint32_t>(VALIDATION_LAYERS.size());
	device_create_info.ppEnabledLayerNames = VALIDATION_LAYERS.data();

	VkResult result = vkCreateDevice(ctx.physical_device, &device_create_info, nullptr, &ctx.device);
	check_result(result, "Failed to create device");

	vkGetDeviceQueue(ctx.device, *queue_families.graphics_family, 0, &ctx.graphics_queue);
	vkGetDeviceQueue(ctx.device, *queue_families.present_family, 0, &ctx.present_queue);
}

static VkSurfaceFormatKHR select_swap_surface_format(std::vector<VkSurfaceFormatKHR> const &available_formats) {
	for (auto const &available_format : available_formats) {
		if (available_format.format == VK_FORMAT_B8G8R8A8_SRGB && available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			return available_format;
		}
	}
	return available_formats[0];
}

static VkPresentModeKHR select_swap_present_mode(std::vector<VkPresentModeKHR> const &available_present_modes) {
	for (auto const &available_present_mode : available_present_modes) {
		if (available_present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
			return available_present_mode;
		}
	}
	return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D select_swap_extent(VkSurfaceCapabilitiesKHR const &capabilities) {
	if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
		return capabilities.currentExtent;
	} else {
		int width, height;
		glfwGetFramebufferSize(ctx.window, &width, &height);
		VkExtent2D pixel_extent = {
			std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
			std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
		};
		return pixel_extent;
	}
}

static void setup_swap_chain() {
	SwapChainSupportDetails swap_chain_support = find_swap_chains(ctx.physical_device);
	VkSurfaceFormatKHR surface_format = select_swap_surface_format(swap_chain_support.formats);
	VkPresentModeKHR present_mode = select_swap_present_mode(swap_chain_support.present_modes);
	VkExtent2D extent = select_swap_extent(swap_chain_support.capabilities);
	uint32_t image_count = swap_chain_support.capabilities.minImageCount + 1;
	if (swap_chain_support.capabilities.maxImageCount > 0 && image_count > swap_chain_support.capabilities.maxImageCount) {
		image_count = swap_chain_support.capabilities.maxImageCount;
	}

	QueueFamilyIndices indices = find_queues(ctx.physical_device);
	uint32_t queue_family_indices[] = { indices.graphics_family.value(), indices.present_family.value() };

	VkSwapchainCreateInfoKHR create_info {};
	create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	create_info.surface = ctx.surface;
	create_info.minImageCount = image_count;
	create_info.imageFormat = surface_format.format;
	create_info.imageColorSpace = surface_format.colorSpace;
	create_info.imageExtent = extent;
	create_info.imageArrayLayers = 1;
	create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if (indices.graphics_family != indices.present_family) {
		create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		create_info.queueFamilyIndexCount = 2;
		create_info.pQueueFamilyIndices = queue_family_indices;
	} else {
		create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	}
	create_info.preTransform = swap_chain_support.capabilities.currentTransform;
	create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	create_info.presentMode = present_mode;
	create_info.clipped = VK_TRUE;
	create_info.oldSwapchain = VK_NULL_HANDLE;

	VkResult result = vkCreateSwapchainKHR(ctx.device, &create_info, nullptr, &ctx.swap_chain);
	check_result(result, "Failed to create swap chain");

	vkGetSwapchainImagesKHR(ctx.device, ctx.swap_chain, &image_count, nullptr);
	ctx.swap_chain_images.resize(image_count);
	vkGetSwapchainImagesKHR(ctx.device, ctx.swap_chain, &image_count, ctx.swap_chain_images.data());

	ctx.swap_chain_image_format = surface_format.format;
	ctx.swap_chain_extent = extent;
}

static void setup_image_views() {
	ctx.swap_chain_image_views.resize(ctx.swap_chain_images.size());
	for (size_t i = 0; i < ctx.swap_chain_images.size(); ++i) {
		VkImageViewCreateInfo create_info {};
		create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		create_info.image = ctx.swap_chain_images[i];
		create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		create_info.format = ctx.swap_chain_image_format;
		create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		create_info.subresourceRange.baseMipLevel = 0;
		create_info.subresourceRange.levelCount = 1;
		create_info.subresourceRange.baseArrayLayer = 0;
		create_info.subresourceRange.layerCount = 1;
		VkResult result = vkCreateImageView(ctx.device, &create_info, nullptr, &ctx.swap_chain_image_views[i]);
		check_result(result, "Failed to create image view from swap chain image");
	}
}

static VkShaderModule create_shader_module(std::vector<char> const shader_code) {
	VkShaderModuleCreateInfo create_info {};
	create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	create_info.codeSize = shader_code.size();
	create_info.pCode = reinterpret_cast<const uint32_t *>(shader_code.data());

	VkShaderModule shader_module;
	VkResult result = vkCreateShaderModule(ctx.device, &create_info, nullptr, &shader_module);
	check_result(result, "Failed to create shader module");

	return shader_module;
}

static void setup_render_pass() {
	VkAttachmentDescription color_attachment {};
	color_attachment.format = ctx.swap_chain_image_format;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref {};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;

	VkSubpassDependency dependency {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo render_pass_info {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	render_pass_info.dependencyCount = 1;
	render_pass_info.pDependencies = &dependency;

	VkResult result = vkCreateRenderPass(ctx.device, &render_pass_info, nullptr, &ctx.render_pass);
	check_result(result, "Failed to create render pass");
}

static void setup_graphics_pipeline() {
#if USE_MESH_SHADER
	std::vector<char> const mesh_shader_code = read_binary_file("mesh.spv");
	VkShaderModule mesh_shader_module = create_shader_module(mesh_shader_code);

	VkPipelineShaderStageCreateInfo mesh_shader_stage_info {};
	mesh_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	mesh_shader_stage_info.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
	mesh_shader_stage_info.module = mesh_shader_module;
	mesh_shader_stage_info.pName = "main";
#else
	std::vector<char> const vert_shader_code = read_binary_file("vert.spv");
	VkShaderModule vert_shader_module = create_shader_module(vert_shader_code);

	VkPipelineShaderStageCreateInfo vert_shader_stage_info {};
	vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vert_shader_stage_info.module = vert_shader_module;
	vert_shader_stage_info.pName = "main";
#endif

	std::vector<char> const frag_shader_code = read_binary_file("frag.spv");
	VkShaderModule frag_shader_module = create_shader_module(frag_shader_code);

	VkPipelineShaderStageCreateInfo frag_shader_stage_info {};
	frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	frag_shader_stage_info.module = frag_shader_module;
	frag_shader_stage_info.pName = "main";

	VkPipelineShaderStageCreateInfo shader_stages[] = {
#if USE_MESH_SHADER
		mesh_shader_stage_info,
#else
		vert_shader_stage_info,
#endif
		frag_shader_stage_info,
	};

	std::vector<VkDynamicState> dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamic_state {};
	dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
	dynamic_state.pDynamicStates = dynamic_states.data();

#if !USE_MESH_SHADER
	VkPipelineVertexInputStateCreateInfo vertex_input_info {};
	vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_info.vertexBindingDescriptionCount = 0;
	vertex_input_info.vertexAttributeDescriptionCount = 0;

	VkPipelineInputAssemblyStateCreateInfo input_assembly {};
	input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	input_assembly.primitiveRestartEnable = VK_FALSE;
#endif

	VkViewport viewport {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)ctx.swap_chain_extent.width;
	viewport.height = (float)ctx.swap_chain_extent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor {};
	scissor.offset = { 0, 0 };
	scissor.extent = ctx.swap_chain_extent;

	VkPipelineViewportStateCreateInfo viewport_state {};
	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizer {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState color_blend_attachment {};
	color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	color_blend_attachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo color_blending {};
	color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	color_blending.logicOpEnable = VK_FALSE;
	color_blending.attachmentCount = 1;
	color_blending.pAttachments = &color_blend_attachment;

	VkPipelineLayoutCreateInfo pipeline_layout_info {};
	pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	VkResult result = vkCreatePipelineLayout(ctx.device, &pipeline_layout_info, nullptr, &ctx.pipeline_layout);
	check_result(result, "Failed to create pipeline layout");

	VkGraphicsPipelineCreateInfo pipeline_create_info {};
	pipeline_create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline_create_info.stageCount = 2;
	pipeline_create_info.pStages = shader_stages;
	pipeline_create_info.pViewportState = &viewport_state;
	pipeline_create_info.pRasterizationState = &rasterizer;
	pipeline_create_info.pMultisampleState = &multisampling;
	pipeline_create_info.pColorBlendState = &color_blending;
	pipeline_create_info.pDynamicState = &dynamic_state;
	pipeline_create_info.layout = ctx.pipeline_layout;
	pipeline_create_info.renderPass = ctx.render_pass;
	pipeline_create_info.subpass = 0;
#if !USE_MESH_SHADER
	pipeline_create_info.pVertexInputState = &vertex_input_info;
	pipeline_create_info.pInputAssemblyState = &input_assembly;
#endif
	result = vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pipeline_create_info, nullptr, &ctx.graphics_pipeline);
	check_result(result, "Failed to create graphics pipeline");

	vkDestroyShaderModule(ctx.device, frag_shader_module, nullptr);
#if USE_MESH_SHADER
	vkDestroyShaderModule(ctx.device, mesh_shader_module, nullptr);
#else
	vkDestroyShaderModule(ctx.device, vert_shader_module, nullptr);
#endif
}

static void setup_framebuffers() {
	ctx.swap_chain_framebuffers.resize(ctx.swap_chain_image_views.size());
	for (size_t i = 0; i < ctx.swap_chain_image_views.size(); ++i) {
		VkFramebufferCreateInfo framebuffer_info {};
		framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_info.renderPass = ctx.render_pass;
		framebuffer_info.attachmentCount = 1;
		framebuffer_info.pAttachments = &ctx.swap_chain_image_views[i];
		framebuffer_info.width = ctx.swap_chain_extent.width;
		framebuffer_info.height = ctx.swap_chain_extent.height;
		framebuffer_info.layers = 1;

		VkResult result = vkCreateFramebuffer(ctx.device, &framebuffer_info, nullptr, &ctx.swap_chain_framebuffers[i]);
		check_result(result, "Failed to create framebuffer");
	}
}

static void setup_command_pool() {
	QueueFamilyIndices queue_family_indices = find_queues(ctx.physical_device);

	VkCommandPoolCreateInfo pool_info {};
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = *queue_family_indices.graphics_family;
	VkResult result = vkCreateCommandPool(ctx.device, &pool_info, nullptr, &ctx.command_pool);
	check_result(result, "Failed to create command pool");
}

static void setup_command_buffer() {
	VkCommandBufferAllocateInfo alloc_info {};
	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.commandPool = ctx.command_pool;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;
	VkResult result = vkAllocateCommandBuffers(ctx.device, &alloc_info, &ctx.command_buffer);
	check_result(result, "Failed to allocate command buffer");
}

static void setup_sync_objects() {
	VkSemaphoreCreateInfo semaphore_info {};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkResult result = vkCreateSemaphore(ctx.device, &semaphore_info, nullptr, &ctx.image_available_semaphore);
	check_result(result, "Failed to create semaphore");
	result = vkCreateSemaphore(ctx.device, &semaphore_info, nullptr, &ctx.render_finished_semaphore);
	check_result(result, "Failed to create semaphore");

	VkFenceCreateInfo fence_info {};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	result = vkCreateFence(ctx.device, &fence_info, nullptr, &ctx.in_flight_fence);
	check_result(result, "Failed to create fence");
}

static void setup() {
	setup_window();
	setup_instance();
	setup_extension_functions();
	setup_debug_messenger();
	setup_surface();
	setup_device();
	setup_swap_chain();
	setup_image_views();
	setup_render_pass();
	setup_graphics_pipeline();
	setup_framebuffers();
	setup_command_pool();
	setup_command_buffer();
	setup_sync_objects();
}

static void record_command_buffer(VkCommandBuffer command_buffer, uint32_t image_index) {
	VkCommandBufferBeginInfo begin_info {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	VkResult result = vkBeginCommandBuffer(command_buffer, &begin_info);
	check_result(result, "Failed to begin recording command buffer");

	VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
	VkRenderPassBeginInfo render_pass_info{};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	render_pass_info.renderPass = ctx.render_pass;
	render_pass_info.framebuffer = ctx.swap_chain_framebuffers[image_index];
	render_pass_info.renderArea.offset = {0, 0};
	render_pass_info.renderArea.extent = ctx.swap_chain_extent;
	render_pass_info.clearValueCount = 1;
	render_pass_info.pClearValues = &clear_color;
	vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.graphics_pipeline);

	VkViewport viewport {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(ctx.swap_chain_extent.width);
	viewport.height = static_cast<float>(ctx.swap_chain_extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(command_buffer, 0, 1, &viewport);

	VkRect2D scissor {};
	scissor.offset = {0, 0};
	scissor.extent = ctx.swap_chain_extent;
	vkCmdSetScissor(command_buffer, 0, 1, &scissor);

#if USE_MESH_SHADER
	ext.vkCmdDrawMeshTasksEXT(command_buffer, 1, 1, 1);
#else
	vkCmdDraw(command_buffer, 3, 1, 0, 0);
#endif

	vkCmdEndRenderPass(command_buffer);

	result = vkEndCommandBuffer(command_buffer);
	check_result(result, "Failed to record command buffer");
}

static void draw_frame() {
	vkWaitForFences(ctx.device, 1, &ctx.in_flight_fence, VK_TRUE, UINT64_MAX);
	vkResetFences(ctx.device, 1, &ctx.in_flight_fence);

	uint32_t image_index;
	vkAcquireNextImageKHR(ctx.device, ctx.swap_chain, UINT64_MAX, ctx.image_available_semaphore, VK_NULL_HANDLE, &image_index);

	vkResetCommandBuffer(ctx.command_buffer, 0);
	record_command_buffer(ctx.command_buffer, image_index);

	VkSemaphore wait_semaphores[] = { ctx.image_available_semaphore };
	VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	VkSemaphore signal_semaphores[] = { ctx.render_finished_semaphore };

	VkSubmitInfo submit_info {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = wait_semaphores;
	submit_info.pWaitDstStageMask = wait_stages;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &ctx.command_buffer;
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = signal_semaphores;
	VkResult result = vkQueueSubmit(ctx.graphics_queue, 1, &submit_info, ctx.in_flight_fence);
	check_result(result, "Failed to submit draw command");

	VkSwapchainKHR swap_chains[] = { ctx.swap_chain };

	VkPresentInfoKHR present_info {};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = signal_semaphores;
	present_info.swapchainCount = 1;
	present_info.pSwapchains = swap_chains;
	present_info.pImageIndices = &image_index;
	vkQueuePresentKHR(ctx.present_queue, &present_info);
}

static bool tick() {
	if (glfwWindowShouldClose(ctx.window)) {
		return false;
	}

	glfwPollEvents();

	draw_frame();

	return true;
}

static void shutdown() {
	vkDeviceWaitIdle(ctx.device);

	vkDestroySemaphore(ctx.device, ctx.image_available_semaphore, nullptr);
	vkDestroySemaphore(ctx.device, ctx.render_finished_semaphore, nullptr);
	vkDestroyFence(ctx.device, ctx.in_flight_fence, nullptr);

	vkDestroyCommandPool(ctx.device, ctx.command_pool, nullptr);

	for (auto framebuffer : ctx.swap_chain_framebuffers) {
		vkDestroyFramebuffer(ctx.device, framebuffer, nullptr);
	}

	vkDestroyPipeline(ctx.device, ctx.graphics_pipeline, nullptr);
	vkDestroyPipelineLayout(ctx.device, ctx.pipeline_layout, nullptr);

	vkDestroyRenderPass(ctx.device, ctx.render_pass, nullptr);

	for (auto image_view : ctx.swap_chain_image_views) {
		vkDestroyImageView(ctx.device, image_view, nullptr);
	}

	vkDestroySwapchainKHR(ctx.device, ctx.swap_chain, nullptr);

	vkDestroyDevice(ctx.device, nullptr);

	vkDestroySurfaceKHR(ctx.instance, ctx.surface, nullptr);

	ext.vkDestroyDebugUtilsMessengerEXT(ctx.instance, ctx.debug_messenger, nullptr);

	vkDestroyInstance(ctx.instance, nullptr);

	glfwDestroyWindow(ctx.window);
	glfwTerminate();
}

int main() {
	setup();
	while (tick());
	shutdown();
	return 0;
}
