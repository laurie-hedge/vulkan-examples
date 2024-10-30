#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <vector>

static constexpr uint32_t WINDOW_WIDTH = 800;
static constexpr uint32_t WINDOW_HEIGHT = 600;
static char const * const APP_NAME = "Ray Tracing Test";
static const std::vector<char const *> VALIDATION_LAYERS = {
	"VK_LAYER_KHRONOS_validation"
};
static const std::vector<char const *> REQUIRED_EXTENSIONS = {
	VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
	VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
	VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
	VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
	VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
	VK_KHR_SPIRV_1_4_EXTENSION_NAME,
	VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

struct RayTracingScratchBuffer {
	uint64_t device_address = 0;
	VkBuffer handle = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct AccelerationStructure {
	VkAccelerationStructureKHR handle;
	uint64_t device_address = 0;
	VkDeviceMemory memory;
	VkBuffer buffer;
};

struct UniformData {
	glm::mat4 view_inverse;
	glm::mat4 proj_inverse;
};

struct ExtFuncs {
	PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;
	PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT;
	PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;
	PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
	PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
	PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
	PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
	PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
	PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR;
	PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
	PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
	PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;
} ext;

struct Context {
	GLFWwindow* window;
	VkInstance instance;
	VkDebugUtilsMessengerEXT debug_messenger;
	VkSurfaceKHR surface;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	VkDevice device;
	VkQueue graphics_queue;
	VkQueue present_queue;
	VkSwapchainKHR swap_chain;
	std::vector<VkImage> swap_chain_images;
	std::vector<VkImageView> swap_chain_image_views;
	VkCommandPool command_pool;
	VkCommandBuffer command_buffer;
	VkDescriptorSetLayout descriptor_set_layout;
	VkPipelineLayout pipeline_layout;
	VkPipeline graphics_pipeline;
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shader_groups;
	VkSemaphore image_available_semaphore;
	VkSemaphore render_finished_semaphore;
	VkFence in_flight_fence;
	VkImage storage_image;
	VkImageView storage_image_view;
	VkDeviceMemory storage_image_memory;
	VkDescriptorPool descriptor_pool;
	VkDescriptorSet descriptor_set;
	VkBuffer raygen_shader_binding_table_buffer;
	VkBuffer miss_shader_binding_table_buffer;
	VkBuffer hit_shader_binding_table_buffer;
	VkDeviceMemory raygen_shader_binding_table_buffer_memory;
	VkDeviceMemory miss_shader_binding_table_buffer_memory;
	VkDeviceMemory hit_shader_binding_table_buffer_memory;

	VkBuffer uniform_buffer;
	VkDeviceMemory uniform_buffer_memory;
	VkBuffer vertex_buffer;
	VkDeviceMemory vertex_buffer_memory;
	VkBuffer index_buffer;
	VkDeviceMemory index_buffer_memory;
	VkBuffer transform_buffer;
	VkDeviceMemory transform_buffer_memory;
	AccelerationStructure bottom_level_as;
	AccelerationStructure top_level_as;

	VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_pipeline_properties;
	VkFormat swap_chain_image_format;
	VkExtent2D swap_chain_extent;
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
	ext.vkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetInstanceProcAddr(ctx.instance, "vkGetBufferDeviceAddressKHR");
	if(!ext.vkGetBufferDeviceAddressKHR) {
		error_quit("vkGetBufferDeviceAddressKHR function not found");
	}
	ext.vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetInstanceProcAddr(ctx.instance, "vkCreateAccelerationStructureKHR");
	if(!ext.vkCreateAccelerationStructureKHR) {
		error_quit("vkCreateAccelerationStructureKHR function not found");
	}
	ext.vkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetInstanceProcAddr(ctx.instance, "vkDestroyAccelerationStructureKHR");
	if(!ext.vkDestroyAccelerationStructureKHR) {
		error_quit("vkDestroyAccelerationStructureKHR function not found");
	}
	ext.vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetInstanceProcAddr(ctx.instance, "vkGetAccelerationStructureBuildSizesKHR");
	if(!ext.vkGetAccelerationStructureBuildSizesKHR) {
		error_quit("vkGetAccelerationStructureBuildSizesKHR function not found");
	}
	ext.vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetInstanceProcAddr(ctx.instance, "vkGetAccelerationStructureDeviceAddressKHR");
	if(!ext.vkGetAccelerationStructureDeviceAddressKHR) {
		error_quit("vkGetAccelerationStructureDeviceAddressKHR function not found");
	}
	ext.vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetInstanceProcAddr(ctx.instance, "vkCmdBuildAccelerationStructuresKHR");
	if(!ext.vkCmdBuildAccelerationStructuresKHR) {
		error_quit("vkCmdBuildAccelerationStructuresKHR function not found");
	}
	ext.vkBuildAccelerationStructuresKHR = (PFN_vkBuildAccelerationStructuresKHR)vkGetInstanceProcAddr(ctx.instance, "vkBuildAccelerationStructuresKHR");
	if(!ext.vkBuildAccelerationStructuresKHR) {
		error_quit("vkBuildAccelerationStructuresKHR function not found");
	}
	ext.vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)vkGetInstanceProcAddr(ctx.instance, "vkCmdTraceRaysKHR");
	if(!ext.vkCmdTraceRaysKHR) {
		error_quit("vkCmdTraceRaysKHR function not found");
	}
	ext.vkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetInstanceProcAddr(ctx.instance, "vkGetRayTracingShaderGroupHandlesKHR");
	if(!ext.vkGetRayTracingShaderGroupHandlesKHR) {
		error_quit("vkGetRayTracingShaderGroupHandlesKHR function not found");
	}
	ext.vkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)vkGetInstanceProcAddr(ctx.instance, "vkCreateRayTracingPipelinesKHR");
	if(!ext.vkCreateRayTracingPipelinesKHR) {
		error_quit("vkCreateRayTracingPipelinesKHR function not found");
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

static void setup_surface() {
	VkResult result = glfwCreateWindowSurface(ctx.instance, ctx.window, nullptr, &ctx.surface);
	check_result(result, "Failed to create window surface");
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

	ctx.ray_tracing_pipeline_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
	VkPhysicalDeviceProperties2 device_properties {};
	device_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	device_properties.pNext = &ctx.ray_tracing_pipeline_properties;
	vkGetPhysicalDeviceProperties2(ctx.physical_device, &device_properties);

	VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address_features {};
	buffer_device_address_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
	buffer_device_address_features.bufferDeviceAddress = VK_TRUE;

	VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_device_features {};
	ray_tracing_device_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
	ray_tracing_device_features.rayTracingPipeline = VK_TRUE;
	ray_tracing_device_features.pNext = (void*)&buffer_device_address_features;

	VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features {};
	acceleration_structure_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	acceleration_structure_features.accelerationStructure = VK_TRUE;
	acceleration_structure_features.pNext = (void*)&ray_tracing_device_features;

	VkPhysicalDeviceFeatures2 device_features {};
	device_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	device_features.pNext = (void*)&acceleration_structure_features;

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
	create_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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

static void setup_graphics_pipeline() {
	VkDescriptorSetLayoutBinding acceleration_structure_layout_binding {};
	acceleration_structure_layout_binding.binding = 0;
	acceleration_structure_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	acceleration_structure_layout_binding.descriptorCount = 1;
	acceleration_structure_layout_binding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	VkDescriptorSetLayoutBinding result_image_layout_binding {};
	result_image_layout_binding.binding = 1;
	result_image_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	result_image_layout_binding.descriptorCount = 1;
	result_image_layout_binding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	VkDescriptorSetLayoutBinding uniform_buffer_binding{};
	uniform_buffer_binding.binding = 2;
	uniform_buffer_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uniform_buffer_binding.descriptorCount = 1;
	uniform_buffer_binding.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

	std::vector<VkDescriptorSetLayoutBinding> bindings = {
		acceleration_structure_layout_binding,
		result_image_layout_binding,
		uniform_buffer_binding,
	};

	VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info {};
	descriptor_set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptor_set_layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
	descriptor_set_layout_info.pBindings = bindings.data();
	VkResult result = vkCreateDescriptorSetLayout(ctx.device, &descriptor_set_layout_info, nullptr, &ctx.descriptor_set_layout);
	check_result(result, "Failed to create descriptor set layout");

	VkPipelineLayoutCreateInfo pipeline_layout_info {};
	pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &ctx.descriptor_set_layout;
	result = vkCreatePipelineLayout(ctx.device, &pipeline_layout_info, nullptr, &ctx.pipeline_layout);
	check_result(result, "Failed to create pipeline layout");

	std::vector<char> const rgen_shader_code = read_binary_file("rgen.spv");
	VkShaderModule rgen_shader_module = create_shader_module(rgen_shader_code);

	VkPipelineShaderStageCreateInfo rgen_shader_stage_info {};
	rgen_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	rgen_shader_stage_info.stage = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
	rgen_shader_stage_info.module = rgen_shader_module;
	rgen_shader_stage_info.pName = "main";

	VkRayTracingShaderGroupCreateInfoKHR shader_group {};
	shader_group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
	shader_group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
	shader_group.generalShader = 0;
	shader_group.closestHitShader = VK_SHADER_UNUSED_KHR;
	shader_group.anyHitShader = VK_SHADER_UNUSED_KHR;
	shader_group.intersectionShader = VK_SHADER_UNUSED_KHR;
	ctx.shader_groups.push_back(shader_group);

	std::vector<char> const miss_shader_code = read_binary_file("miss.spv");
	VkShaderModule miss_shader_module = create_shader_module(miss_shader_code);

	VkPipelineShaderStageCreateInfo miss_shader_stage_info {};
	miss_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	miss_shader_stage_info.stage = VK_SHADER_STAGE_MISS_BIT_KHR;
	miss_shader_stage_info.module = miss_shader_module;
	miss_shader_stage_info.pName = "main";

	shader_group.generalShader = 1;
	ctx.shader_groups.push_back(shader_group);

	std::vector<char> const hit_shader_code = read_binary_file("hit.spv");
	VkShaderModule hit_shader_module = create_shader_module(hit_shader_code);

	VkPipelineShaderStageCreateInfo hit_shader_stage_info {};
	hit_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	hit_shader_stage_info.stage = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
	hit_shader_stage_info.module = hit_shader_module;
	hit_shader_stage_info.pName = "main";

	shader_group.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
	shader_group.generalShader = VK_SHADER_UNUSED_KHR;
	shader_group.closestHitShader = 2;
	ctx.shader_groups.push_back(shader_group);

	VkPipelineShaderStageCreateInfo shader_stages[] = {
		rgen_shader_stage_info,
		miss_shader_stage_info,
		hit_shader_stage_info,
	};

	VkRayTracingPipelineCreateInfoKHR ray_tracing_pipeline_info{};
	ray_tracing_pipeline_info.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
	ray_tracing_pipeline_info.stageCount = 3;
	ray_tracing_pipeline_info.pStages = shader_stages;
	ray_tracing_pipeline_info.groupCount = static_cast<uint32_t>(ctx.shader_groups.size());
	ray_tracing_pipeline_info.pGroups = ctx.shader_groups.data();
	ray_tracing_pipeline_info.maxPipelineRayRecursionDepth = 1;
	ray_tracing_pipeline_info.layout = ctx.pipeline_layout;
	result = ext.vkCreateRayTracingPipelinesKHR(ctx.device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &ray_tracing_pipeline_info, nullptr, &ctx.graphics_pipeline);
	check_result(result, "Failed to create ray tracing pipeline");

	vkDestroyShaderModule(ctx.device, rgen_shader_module, nullptr);
	vkDestroyShaderModule(ctx.device, miss_shader_module, nullptr);
	vkDestroyShaderModule(ctx.device, hit_shader_module, nullptr);
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

	result = vkCreateFence(ctx.device, &fence_info, nullptr, &ctx.in_flight_fence);
	check_result(result, "Failed to create fence");
}

static uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties mem_properties;
	vkGetPhysicalDeviceMemoryProperties(ctx.physical_device, &mem_properties);
	for (uint32_t i = 0; i < mem_properties.memoryTypeCount; ++i) {
		if (type_filter & (1 << i)) {
			return i;
		}
	}
	error_quit("Failed to find memory type");
	return 0;
}

static void setup_storage_image() {
	VkImageCreateInfo image_info {};
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	image_info.extent.width = ctx.swap_chain_extent.width;
	image_info.extent.height = ctx.swap_chain_extent.height;
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkResult result = vkCreateImage(ctx.device, &image_info, nullptr, &ctx.storage_image);
	check_result(result, "Failed to create storage image");

	VkMemoryRequirements memory_requirements;
	vkGetImageMemoryRequirements(ctx.device, ctx.storage_image, &memory_requirements);

	VkMemoryAllocateInfo memory_alloc_info {};
	memory_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_alloc_info.allocationSize = memory_requirements.size;
	memory_alloc_info.memoryTypeIndex = find_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkAllocateMemory(ctx.device, &memory_alloc_info, nullptr, &ctx.storage_image_memory);
	check_result(result, "Failed to allocate memory for storage image");

	vkBindImageMemory(ctx.device, ctx.storage_image, ctx.storage_image_memory, 0);

	VkImageViewCreateInfo image_view_info {};
	image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	image_view_info.image = VK_NULL_HANDLE;
	image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	image_view_info.format = image_info.format;
	image_view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_view_info.subresourceRange.baseMipLevel = 0;
	image_view_info.subresourceRange.levelCount = 1;
	image_view_info.subresourceRange.baseArrayLayer = 0;
	image_view_info.subresourceRange.layerCount = 1;
	image_view_info.image = ctx.storage_image;
	result = vkCreateImageView(ctx.device, &image_view_info, nullptr, &ctx.storage_image_view);
	check_result(result, "Failed to create storage image view");

	VkCommandBufferBeginInfo begin_info {};
	begin_info.sType =  VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	result = vkBeginCommandBuffer(ctx.command_buffer, &begin_info);
	check_result(result, "Failed to begin command buffer");

	VkImageMemoryBarrier image_memory_barrier {};
	image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_memory_barrier.subresourceRange.levelCount = 1;
	image_memory_barrier.subresourceRange.layerCount = 1;
	image_memory_barrier.image = ctx.storage_image;

	vkCmdPipelineBarrier(
		ctx.command_buffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0,
		0,
		nullptr,
		0,
		nullptr,
		1,
		&image_memory_barrier
	);

	vkEndCommandBuffer(ctx.command_buffer);

	VkSubmitInfo submit_info {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &ctx.command_buffer;
	result = vkQueueSubmit(ctx.graphics_queue, 1, &submit_info, ctx.in_flight_fence);
	check_result(result, "Failed to transition image format");

	result = vkWaitForFences(ctx.device, 1, &ctx.in_flight_fence, VK_TRUE, UINT64_MAX);
	check_result(result, "Failed to wait for fence");
	vkResetFences(ctx.device, 1, &ctx.in_flight_fence);
}

static uint32_t get_memory_type(uint32_t type_bits, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memory_properties;
	vkGetPhysicalDeviceMemoryProperties(ctx.physical_device, &memory_properties);

	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
	{
		if ((type_bits & 1) == 1)
		{
			if ((memory_properties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}
		type_bits >>= 1;
	}

	error_quit("Failed to find memory type");
	return 0;
}

static void create_buffer(VkBufferUsageFlags usage_flags, VkMemoryPropertyFlags memory_property_flags, VkDeviceSize size, VkBuffer *buffer, VkDeviceMemory *memory, void *data) {
	VkBufferCreateInfo buffer_create_info = {};
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = size;
	buffer_create_info.usage = usage_flags;
	buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkResult result = vkCreateBuffer(ctx.device, &buffer_create_info, nullptr, buffer);
	check_result(result, "Failed to create buffer");

	VkMemoryRequirements mem_reqs;
	vkGetBufferMemoryRequirements(ctx.device, *buffer, &mem_reqs);

	VkMemoryAllocateInfo mem_alloc_info {};
	mem_alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	mem_alloc_info.allocationSize = mem_reqs.size;
	mem_alloc_info.memoryTypeIndex = get_memory_type(mem_reqs.memoryTypeBits, memory_property_flags);
	VkMemoryAllocateFlagsInfoKHR alloc_flags_info {};
	if (usage_flags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
		alloc_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
		alloc_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
		mem_alloc_info.pNext = &alloc_flags_info;
	}
	result = vkAllocateMemory(ctx.device, &mem_alloc_info, nullptr, memory);
	check_result(result, "Failed to allocate memory");

	if (data)
	{
		void *mapped;
		result = vkMapMemory(ctx.device, *memory, 0, size, 0, &mapped);
		check_result(result, "Failed to map memory");
		memcpy(mapped, data, size);
		if ((memory_property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
		{
			VkMappedMemoryRange mapped_range;
			mapped_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
			mapped_range.memory = *memory;
			mapped_range.offset = 0;
			mapped_range.size = size;
			vkFlushMappedMemoryRanges(ctx.device, 1, &mapped_range);
		}
		vkUnmapMemory(ctx.device, *memory);
	}

	result = vkBindBufferMemory(ctx.device, *buffer, *memory, 0);
	check_result(result, "Failed to bind buffer memory");
}

RayTracingScratchBuffer create_scratch_buffer(VkDeviceSize size) {
	RayTracingScratchBuffer scratch_buffer {};

	VkBufferCreateInfo buffer_create_info {};
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = size;
	buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	VkResult result = vkCreateBuffer(ctx.device, &buffer_create_info, nullptr, &scratch_buffer.handle);
	check_result(result, "Failed to create scratch buffer");

	VkMemoryRequirements memory_requirements {};
	vkGetBufferMemoryRequirements(ctx.device, scratch_buffer.handle, &memory_requirements);

	VkMemoryAllocateFlagsInfo memory_allocate_flags_info{};
	memory_allocate_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	memory_allocate_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

	VkMemoryAllocateInfo memory_allocate_info {};
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.pNext = &memory_allocate_flags_info;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = get_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkAllocateMemory(ctx.device, &memory_allocate_info, nullptr, &scratch_buffer.memory);
	check_result(result, "Failed to allocate memory for scratch buffer");
	result = vkBindBufferMemory(ctx.device, scratch_buffer.handle, scratch_buffer.memory, 0);
	check_result(result, "Failed to bind scratch buffer");

	VkBufferDeviceAddressInfoKHR buffer_device_address_info {};
	buffer_device_address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	buffer_device_address_info.buffer = scratch_buffer.handle;
	scratch_buffer.device_address = ext.vkGetBufferDeviceAddressKHR(ctx.device, &buffer_device_address_info);

	return scratch_buffer;
}

void free_scratch_buffer(RayTracingScratchBuffer &scratch_buffer) {
	vkFreeMemory(ctx.device, scratch_buffer.memory, nullptr);
	vkDestroyBuffer(ctx.device, scratch_buffer.handle, nullptr);
}

uint64_t get_buffer_device_address(VkBuffer buffer) {
	VkBufferDeviceAddressInfoKHR buffer_device_Address_info {};
	buffer_device_Address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	buffer_device_Address_info.buffer = buffer;
	return ext.vkGetBufferDeviceAddressKHR(ctx.device, &buffer_device_Address_info);
}

static void setup_bottom_level_acceleration_structure() {
	struct Vertex {
		float pos[3];
	};

	std::vector<Vertex> vertices = {
		{ {  1.0f,  1.0f, 0.0f } },
		{ { -1.0f,  1.0f, 0.0f } },
		{ {  0.0f, -1.0f, 0.0f } }
	};

	std::vector<uint32_t> indices = { 0, 1, 2 };

	VkTransformMatrixKHR transform_matrix = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f
	};

	create_buffer(
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		vertices.size() * sizeof(Vertex),
		&ctx.vertex_buffer,
		&ctx.vertex_buffer_memory,
		vertices.data()
	);

	create_buffer(
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		indices.size() * sizeof(uint32_t),
		&ctx.index_buffer,
		&ctx.index_buffer_memory,
		indices.data()
	);

	create_buffer(
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		sizeof(VkTransformMatrixKHR),
		&ctx.transform_buffer,
		&ctx.transform_buffer_memory,
		&transform_matrix
	);

	VkDeviceOrHostAddressConstKHR vertex_buffer_device_address {};
	VkDeviceOrHostAddressConstKHR index_buffer_device_address {};
	VkDeviceOrHostAddressConstKHR transform_buffer_device_address {};

	vertex_buffer_device_address.deviceAddress = get_buffer_device_address(ctx.vertex_buffer);
	index_buffer_device_address.deviceAddress = get_buffer_device_address(ctx.index_buffer);
	transform_buffer_device_address.deviceAddress = get_buffer_device_address(ctx.transform_buffer);

	VkAccelerationStructureGeometryKHR acceleration_structure_geometry {};
	acceleration_structure_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	acceleration_structure_geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	acceleration_structure_geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	acceleration_structure_geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	acceleration_structure_geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	acceleration_structure_geometry.geometry.triangles.vertexData = vertex_buffer_device_address;
	acceleration_structure_geometry.geometry.triangles.maxVertex = 2;
	acceleration_structure_geometry.geometry.triangles.vertexStride = sizeof(Vertex);
	acceleration_structure_geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
	acceleration_structure_geometry.geometry.triangles.indexData = index_buffer_device_address;
	acceleration_structure_geometry.geometry.triangles.transformData.deviceAddress = 0;
	acceleration_structure_geometry.geometry.triangles.transformData.hostAddress = nullptr;
	acceleration_structure_geometry.geometry.triangles.transformData = transform_buffer_device_address;

	VkAccelerationStructureBuildGeometryInfoKHR acceleration_structure_build_geometry_info {};
	acceleration_structure_build_geometry_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	acceleration_structure_build_geometry_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	acceleration_structure_build_geometry_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	acceleration_structure_build_geometry_info.geometryCount = 1;
	acceleration_structure_build_geometry_info.pGeometries = &acceleration_structure_geometry;

	const uint32_t num_triangles = 1;
	VkAccelerationStructureBuildSizesInfoKHR acceleration_structure_build_sizes_info {};
	acceleration_structure_build_sizes_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	ext.vkGetAccelerationStructureBuildSizesKHR(
		ctx.device,
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&acceleration_structure_build_geometry_info,
		&num_triangles,
		&acceleration_structure_build_sizes_info);

	VkBufferCreateInfo acceleration_structure_buffer_info {};
	acceleration_structure_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	acceleration_structure_buffer_info.size = acceleration_structure_build_sizes_info.accelerationStructureSize;
	acceleration_structure_buffer_info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	VkResult result = vkCreateBuffer(ctx.device, &acceleration_structure_buffer_info, nullptr, &ctx.bottom_level_as.buffer);
	check_result(result, "Failed to create buffer");

	VkMemoryRequirements memory_requirements {};
	vkGetBufferMemoryRequirements(ctx.device, ctx.bottom_level_as.buffer, &memory_requirements);

	VkMemoryAllocateFlagsInfo memory_allocate_flags_info {};
	memory_allocate_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	memory_allocate_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
	VkMemoryAllocateInfo memory_allocate_info {};
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.pNext = &memory_allocate_flags_info;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = get_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkAllocateMemory(ctx.device, &memory_allocate_info, nullptr, &ctx.bottom_level_as.memory);
	check_result(result, "Failed to allocate memory");

	result = vkBindBufferMemory(ctx.device, ctx.bottom_level_as.buffer, ctx.bottom_level_as.memory, 0);
	check_result(result, "Failed to bind buffer memory");

	VkAccelerationStructureCreateInfoKHR acceleration_structure_info {};
	acceleration_structure_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	acceleration_structure_info.buffer = ctx.bottom_level_as.buffer;
	acceleration_structure_info.size = acceleration_structure_build_sizes_info.accelerationStructureSize;
	acceleration_structure_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	ext.vkCreateAccelerationStructureKHR(ctx.device, &acceleration_structure_info, nullptr, &ctx.bottom_level_as.handle);

	RayTracingScratchBuffer scratch_buffer = create_scratch_buffer(acceleration_structure_build_sizes_info.buildScratchSize);

	VkAccelerationStructureBuildGeometryInfoKHR acceleration_build_geometry_info {};
	acceleration_build_geometry_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	acceleration_build_geometry_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	acceleration_build_geometry_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	acceleration_build_geometry_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	acceleration_build_geometry_info.dstAccelerationStructure = ctx.bottom_level_as.handle;
	acceleration_build_geometry_info.geometryCount = 1;
	acceleration_build_geometry_info.pGeometries = &acceleration_structure_geometry;
	acceleration_build_geometry_info.scratchData.deviceAddress = scratch_buffer.device_address;

	VkAccelerationStructureBuildRangeInfoKHR acceleration_structure_build_range_info{};
	acceleration_structure_build_range_info.primitiveCount = num_triangles;
	acceleration_structure_build_range_info.primitiveOffset = 0;
	acceleration_structure_build_range_info.firstVertex = 0;
	acceleration_structure_build_range_info.transformOffset = 0;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR*> acceleration_build_structure_range_infos { &acceleration_structure_build_range_info };

	VkCommandBufferBeginInfo begin_info {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	result = vkBeginCommandBuffer(ctx.command_buffer, &begin_info);
	check_result(result, "Failed to begin recording command buffer");

	ext.vkCmdBuildAccelerationStructuresKHR(
			ctx.command_buffer,
			1,
			&acceleration_build_geometry_info,
			acceleration_build_structure_range_infos.data());

	result = vkEndCommandBuffer(ctx.command_buffer);
	check_result(result, "Failed to end command buffer");

	VkSubmitInfo submit_info {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &ctx.command_buffer;
	result = vkQueueSubmit(ctx.graphics_queue, 1, &submit_info, ctx.in_flight_fence);
	check_result(result, "Failed to submit draw command");

	result = vkWaitForFences(ctx.device, 1, &ctx.in_flight_fence, VK_TRUE, UINT64_MAX);
	check_result(result, "Failed to wait for fence");
	vkResetFences(ctx.device, 1, &ctx.in_flight_fence);

	VkAccelerationStructureDeviceAddressInfoKHR acceleration_device_address_info {};
	acceleration_device_address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	acceleration_device_address_info.accelerationStructure = ctx.bottom_level_as.handle;
	ctx.bottom_level_as.device_address = ext.vkGetAccelerationStructureDeviceAddressKHR(ctx.device, &acceleration_device_address_info);

	free_scratch_buffer(scratch_buffer);
}

static void setup_top_level_acceleration_structure() {
	VkTransformMatrixKHR transform_matrix = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f
	};

	VkAccelerationStructureInstanceKHR instance {};
	instance.transform = transform_matrix;
	instance.instanceCustomIndex = 0;
	instance.mask = 0xFF;
	instance.instanceShaderBindingTableRecordOffset = 0;
	instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	instance.accelerationStructureReference = ctx.bottom_level_as.device_address;

	VkBuffer instances_buffer;
	VkDeviceMemory instances_buffer_memory;
	create_buffer(
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		sizeof(VkAccelerationStructureInstanceKHR),
		&instances_buffer,
		&instances_buffer_memory,
		&instance
	);

	VkDeviceOrHostAddressConstKHR instance_data_device_address {};
	instance_data_device_address.deviceAddress = get_buffer_device_address(instances_buffer);

	VkAccelerationStructureGeometryKHR acceleration_structure_geometry {};
	acceleration_structure_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	acceleration_structure_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	acceleration_structure_geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
	acceleration_structure_geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	acceleration_structure_geometry.geometry.instances.arrayOfPointers = VK_FALSE;
	acceleration_structure_geometry.geometry.instances.data = instance_data_device_address;

	VkAccelerationStructureBuildGeometryInfoKHR acceleration_structure_build_geometry_info {};
	acceleration_structure_build_geometry_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	acceleration_structure_build_geometry_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	acceleration_structure_build_geometry_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	acceleration_structure_build_geometry_info.geometryCount = 1;
	acceleration_structure_build_geometry_info.pGeometries = &acceleration_structure_geometry;

	uint32_t primitive_count = 1;

	VkAccelerationStructureBuildSizesInfoKHR acceleration_structure_build_sizes_info {};
	acceleration_structure_build_sizes_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	ext.vkGetAccelerationStructureBuildSizesKHR(
		ctx.device, 
		VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&acceleration_structure_build_geometry_info,
		&primitive_count,
		&acceleration_structure_build_sizes_info
	);

	VkBufferCreateInfo acceleration_structure_buffer_info {};
	acceleration_structure_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	acceleration_structure_buffer_info.size = acceleration_structure_build_sizes_info.accelerationStructureSize;
	acceleration_structure_buffer_info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	VkResult result = vkCreateBuffer(ctx.device, &acceleration_structure_buffer_info, nullptr, &ctx.top_level_as.buffer);
	check_result(result, "Failed to create buffer");

	VkMemoryRequirements memory_requirements {};
	vkGetBufferMemoryRequirements(ctx.device, ctx.top_level_as.buffer, &memory_requirements);
	VkMemoryAllocateFlagsInfo memory_allocate_flags_info {};
	memory_allocate_flags_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	memory_allocate_flags_info.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
	VkMemoryAllocateInfo memory_allocate_info {};
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.pNext = &memory_allocate_flags_info;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = get_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkAllocateMemory(ctx.device, &memory_allocate_info, nullptr, &ctx.top_level_as.memory);
	check_result(result, "Failed to allocate memory");

	result = vkBindBufferMemory(ctx.device, ctx.top_level_as.buffer, ctx.top_level_as.memory, 0);
	check_result(result, "Failed to bind buffer memory");

	VkAccelerationStructureCreateInfoKHR acceleration_structure_info{};
	acceleration_structure_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	acceleration_structure_info.buffer = ctx.top_level_as.buffer;
	acceleration_structure_info.size = acceleration_structure_build_sizes_info.accelerationStructureSize;
	acceleration_structure_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	ext.vkCreateAccelerationStructureKHR(ctx.device, &acceleration_structure_info, nullptr, &ctx.top_level_as.handle);

	RayTracingScratchBuffer scratch_buffer = create_scratch_buffer(acceleration_structure_build_sizes_info.buildScratchSize);

	VkAccelerationStructureBuildGeometryInfoKHR acceleration_build_geometry_info {};
	acceleration_build_geometry_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	acceleration_build_geometry_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	acceleration_build_geometry_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	acceleration_build_geometry_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	acceleration_build_geometry_info.dstAccelerationStructure = ctx.top_level_as.handle;
	acceleration_build_geometry_info.geometryCount = 1;
	acceleration_build_geometry_info.pGeometries = &acceleration_structure_geometry;
	acceleration_build_geometry_info.scratchData.deviceAddress = scratch_buffer.device_address;

	VkAccelerationStructureBuildRangeInfoKHR acceleration_structure_build_range_info {};
	acceleration_structure_build_range_info.primitiveCount = 1;
	acceleration_structure_build_range_info.primitiveOffset = 0;
	acceleration_structure_build_range_info.firstVertex = 0;
	acceleration_structure_build_range_info.transformOffset = 0;
	std::vector<VkAccelerationStructureBuildRangeInfoKHR*> acceleration_build_structure_range_infos = { &acceleration_structure_build_range_info };

	VkCommandBufferBeginInfo begin_info {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	result = vkBeginCommandBuffer(ctx.command_buffer, &begin_info);
	check_result(result, "Failed to begin recording command buffer");

	ext.vkCmdBuildAccelerationStructuresKHR(
		ctx.command_buffer,
		1,
		&acceleration_build_geometry_info,
		acceleration_build_structure_range_infos.data()
	);

	result = vkEndCommandBuffer(ctx.command_buffer);
	check_result(result, "Failed to end command buffer");

	VkSubmitInfo submit_info {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &ctx.command_buffer;
	result = vkQueueSubmit(ctx.graphics_queue, 1, &submit_info, ctx.in_flight_fence);
	check_result(result, "Failed to submit draw command");

	result = vkWaitForFences(ctx.device, 1, &ctx.in_flight_fence, VK_TRUE, UINT64_MAX);
	check_result(result, "Failed to wait for fence");
	// vkResetFences(ctx.device, 1, &ctx.in_flight_fence); // don't reset since this is last usage before main loop

	VkAccelerationStructureDeviceAddressInfoKHR acceleration_device_address_info{};
	acceleration_device_address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	acceleration_device_address_info.accelerationStructure = ctx.top_level_as.handle;
	ctx.top_level_as.device_address = ext.vkGetAccelerationStructureDeviceAddressKHR(ctx.device, &acceleration_device_address_info);

	free_scratch_buffer(scratch_buffer);

	vkFreeMemory(ctx.device, instances_buffer_memory, nullptr);
	vkDestroyBuffer(ctx.device, instances_buffer, nullptr);
}

static void setup_uniform_buffer() {
	const float fov = glm::radians(60.0f);
	const float aspect = (float)ctx.swap_chain_extent.width / (float)ctx.swap_chain_extent.height;
	const glm::vec3 position = glm::vec3(0.0f, 0.0f, -2.5f);
	const glm::mat4 translate = glm::translate(glm::mat4(1.0f), position);

	UniformData uniform_data;
	uniform_data.proj_inverse = glm::inverse(glm::perspective(fov, aspect, 0.1f, 512.0f));
	uniform_data.view_inverse = glm::inverse(translate);

	create_buffer(
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		sizeof(UniformData),
		&ctx.uniform_buffer,
		&ctx.uniform_buffer_memory,
		&uniform_data
	);
}

static uint32_t align_to(uint32_t size, uint32_t alignment) {
	if ((size % alignment) == 0) {
		return size;
	}
	return ((size / alignment) + 1) * alignment;
}

static void setup_shader_binding_table() {
	const uint32_t handle_size = ctx.ray_tracing_pipeline_properties.shaderGroupHandleSize;
	const uint32_t handle_size_aligned = align_to(
		ctx.ray_tracing_pipeline_properties.shaderGroupHandleSize,
		ctx.ray_tracing_pipeline_properties.shaderGroupHandleAlignment
	);
	const uint32_t group_count = static_cast<uint32_t>(ctx.shader_groups.size());
	const uint32_t sbt_size = group_count * handle_size_aligned;

	std::vector<uint8_t> shader_handle_storage(sbt_size);
	VkResult result = ext.vkGetRayTracingShaderGroupHandlesKHR(
		ctx.device,
		ctx.graphics_pipeline,
		0,
		group_count,
		sbt_size,
		shader_handle_storage.data()
	);
	check_result(result, "Failed to get ray tracing shader group handles");

	const VkBufferUsageFlags buffer_usage_flags = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	const VkMemoryPropertyFlags memory_usage_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	create_buffer(
		buffer_usage_flags,
		memory_usage_flags,
		handle_size,
		&ctx.raygen_shader_binding_table_buffer,
		&ctx.raygen_shader_binding_table_buffer_memory,
		shader_handle_storage.data()
	);

	create_buffer(
		buffer_usage_flags,
		memory_usage_flags,
		handle_size,
		&ctx.miss_shader_binding_table_buffer,
		&ctx.miss_shader_binding_table_buffer_memory,
		shader_handle_storage.data() + handle_size_aligned
	);

	create_buffer(
		buffer_usage_flags,
		memory_usage_flags,
		handle_size,
		&ctx.hit_shader_binding_table_buffer,
		&ctx.hit_shader_binding_table_buffer_memory,
		shader_handle_storage.data() + (handle_size_aligned * 2)
	);
}

static void setup_descriptor_sets() {
	std::vector<VkDescriptorPoolSize> pool_sizes = {
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }
	};
	VkDescriptorPoolCreateInfo descriptor_pool_info {};
	descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptor_pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	descriptor_pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
	descriptor_pool_info.pPoolSizes = pool_sizes.data();
	descriptor_pool_info.maxSets = 1;
	VkResult result = vkCreateDescriptorPool(ctx.device, &descriptor_pool_info, nullptr, &ctx.descriptor_pool);
	check_result(result, "Failed to create descriptor pool");

	VkDescriptorSetAllocateInfo descriptor_set_allocate_info {};
	descriptor_set_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptor_set_allocate_info.descriptorPool = ctx.descriptor_pool;
	descriptor_set_allocate_info.descriptorSetCount = 1;
	descriptor_set_allocate_info.pSetLayouts = &ctx.descriptor_set_layout;
	result = vkAllocateDescriptorSets(ctx.device, &descriptor_set_allocate_info, &ctx.descriptor_set);
	check_result(result, "Failed to allocate descriptor set");

	VkWriteDescriptorSetAccelerationStructureKHR descriptor_acceleration_structure_info {};
	descriptor_acceleration_structure_info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	descriptor_acceleration_structure_info.accelerationStructureCount = 1;
	descriptor_acceleration_structure_info.pAccelerationStructures = &ctx.top_level_as.handle;

	VkWriteDescriptorSet acceleration_structure_write{};
	acceleration_structure_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	acceleration_structure_write.pNext = &descriptor_acceleration_structure_info;
	acceleration_structure_write.dstSet = ctx.descriptor_set;
	acceleration_structure_write.dstBinding = 0;
	acceleration_structure_write.descriptorCount = 1;
	acceleration_structure_write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

	VkDescriptorImageInfo storage_image_descriptor {};
	storage_image_descriptor.imageView = ctx.storage_image_view;
	storage_image_descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet result_image_write {};
	result_image_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	result_image_write.dstSet = ctx.descriptor_set;
	result_image_write.dstBinding = 1;
	result_image_write.descriptorCount = 1;
	result_image_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	result_image_write.pImageInfo = &storage_image_descriptor;

	VkDescriptorBufferInfo buffer_info {};
	buffer_info.buffer = ctx.uniform_buffer;
	buffer_info.offset = 0;
	buffer_info.range = sizeof(UniformData);

	VkWriteDescriptorSet uniform_buffer_write {};
	uniform_buffer_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	uniform_buffer_write.dstSet = ctx.descriptor_set;
	uniform_buffer_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uniform_buffer_write.dstBinding = 2;
	uniform_buffer_write.descriptorCount = 1;
	uniform_buffer_write.pBufferInfo = &buffer_info;

	VkWriteDescriptorSet write_descriptor_sets[] = {
		acceleration_structure_write,
		result_image_write,
		uniform_buffer_write
	};

	vkUpdateDescriptorSets(ctx.device, 3, write_descriptor_sets, 0, nullptr);
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
	setup_command_pool();
	setup_command_buffer();
	setup_sync_objects();
	setup_storage_image();
	setup_bottom_level_acceleration_structure();
	setup_top_level_acceleration_structure();
	setup_uniform_buffer();
	setup_graphics_pipeline();
	setup_shader_binding_table();
	setup_descriptor_sets();
}

static void record_command_buffer(uint32_t image_index) {
	VkCommandBufferBeginInfo begin_info {};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	VkResult result = vkBeginCommandBuffer(ctx.command_buffer, &begin_info);
	check_result(result, "Failed to begin recording command buffer");

	const uint32_t handle_size_aligned = align_to(
		ctx.ray_tracing_pipeline_properties.shaderGroupHandleSize,
		ctx.ray_tracing_pipeline_properties.shaderGroupHandleAlignment
	);

	VkStridedDeviceAddressRegionKHR raygen_shader_sbt_entry {};
	raygen_shader_sbt_entry.deviceAddress = get_buffer_device_address(ctx.raygen_shader_binding_table_buffer);
	raygen_shader_sbt_entry.stride = handle_size_aligned;
	raygen_shader_sbt_entry.size = handle_size_aligned;

	VkStridedDeviceAddressRegionKHR miss_shader_sbt_entry {};
	miss_shader_sbt_entry.deviceAddress = get_buffer_device_address(ctx.miss_shader_binding_table_buffer);
	miss_shader_sbt_entry.stride = handle_size_aligned;
	miss_shader_sbt_entry.size = handle_size_aligned;

	VkStridedDeviceAddressRegionKHR hit_shader_sbt_entry {};
	hit_shader_sbt_entry.deviceAddress = get_buffer_device_address(ctx.hit_shader_binding_table_buffer);
	hit_shader_sbt_entry.stride = handle_size_aligned;
	hit_shader_sbt_entry.size = handle_size_aligned;

	VkStridedDeviceAddressRegionKHR callable_shader_sbt_entry {};

	vkCmdBindPipeline(ctx.command_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, ctx.graphics_pipeline);

	vkCmdBindDescriptorSets(
		ctx.command_buffer,
		VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
		ctx.pipeline_layout,
		0,
		1,
		&ctx.descriptor_set,
		0,
		nullptr
	);

	ext.vkCmdTraceRaysKHR(
		ctx.command_buffer,
		&raygen_shader_sbt_entry,
		&miss_shader_sbt_entry,
		&hit_shader_sbt_entry,
		&callable_shader_sbt_entry,
		ctx.swap_chain_extent.width,
		ctx.swap_chain_extent.height,
		1
	);

	VkImageMemoryBarrier image_memory_barrier {};
	image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	image_memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_memory_barrier.subresourceRange.baseMipLevel = 0;
	image_memory_barrier.subresourceRange.levelCount = 1;
	image_memory_barrier.subresourceRange.baseArrayLayer = 0;
	image_memory_barrier.subresourceRange.layerCount = 1;
	image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	image_memory_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	image_memory_barrier.image = ctx.storage_image;
	vkCmdPipelineBarrier(
		ctx.command_buffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0,
		0,
		nullptr,
		0,
		nullptr,
		1,
		&image_memory_barrier
	);

	image_memory_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	image_memory_barrier.image = ctx.swap_chain_images[image_index];
	vkCmdPipelineBarrier(
		ctx.command_buffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0,
		0,
		nullptr,
		0,
		nullptr,
		1,
		&image_memory_barrier
	);

	VkImageCopy image_copy {};
	image_copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_copy.srcSubresource.layerCount = 1;
	image_copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	image_copy.dstSubresource.layerCount = 1;
	image_copy.extent.width = ctx.swap_chain_extent.width;
	image_copy.extent.height = ctx.swap_chain_extent.height;
	image_copy.extent.depth = 1;
	vkCmdCopyImage(
		ctx.command_buffer,
		ctx.storage_image,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		ctx.swap_chain_images[image_index],
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&image_copy
	);

	image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	image_memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	image_memory_barrier.image = ctx.swap_chain_images[image_index];
	vkCmdPipelineBarrier(
		ctx.command_buffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0,
		0,
		nullptr,
		0,
		nullptr,
		1,
		&image_memory_barrier
	);

	image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	image_memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	image_memory_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_memory_barrier.image = ctx.storage_image;
	vkCmdPipelineBarrier(
		ctx.command_buffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		0,
		0,
		nullptr,
		0,
		nullptr,
		1,
		&image_memory_barrier
	);

	result = vkEndCommandBuffer(ctx.command_buffer);
	check_result(result, "Failed to record command buffer");
}

static void draw_frame() {
	vkWaitForFences(ctx.device, 1, &ctx.in_flight_fence, VK_TRUE, UINT64_MAX);
	vkResetFences(ctx.device, 1, &ctx.in_flight_fence);

	uint32_t image_index;
	vkAcquireNextImageKHR(ctx.device, ctx.swap_chain, UINT64_MAX, ctx.image_available_semaphore, VK_NULL_HANDLE, &image_index);

	vkResetCommandBuffer(ctx.command_buffer, 0);
	record_command_buffer(image_index);

	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;

	VkSubmitInfo submit_info {};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.waitSemaphoreCount = 1;
	submit_info.pWaitSemaphores = &ctx.image_available_semaphore;
	submit_info.pWaitDstStageMask = &wait_stage;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &ctx.command_buffer;
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores = &ctx.render_finished_semaphore;
	VkResult result = vkQueueSubmit(ctx.graphics_queue, 1, &submit_info, ctx.in_flight_fence);
	check_result(result, "Failed to submit draw command");

	VkPresentInfoKHR present_info {};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = &ctx.render_finished_semaphore;
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &ctx.swap_chain;
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

	vkDestroyDescriptorPool(ctx.device, ctx.descriptor_pool, nullptr);

	vkDestroyPipeline(ctx.device, ctx.graphics_pipeline, nullptr);
	vkDestroyPipelineLayout(ctx.device, ctx.pipeline_layout, nullptr);
	vkDestroyDescriptorSetLayout(ctx.device, ctx.descriptor_set_layout, nullptr);

	ext.vkDestroyAccelerationStructureKHR(ctx.device, ctx.top_level_as.handle, nullptr);
	vkFreeMemory(ctx.device, ctx.top_level_as.memory, nullptr);
	vkDestroyBuffer(ctx.device, ctx.top_level_as.buffer, nullptr);

	ext.vkDestroyAccelerationStructureKHR(ctx.device, ctx.bottom_level_as.handle, nullptr);
	vkFreeMemory(ctx.device, ctx.bottom_level_as.memory, nullptr);
	vkDestroyBuffer(ctx.device, ctx.bottom_level_as.buffer, nullptr);

	vkFreeMemory(ctx.device, ctx.raygen_shader_binding_table_buffer_memory, nullptr);
	vkFreeMemory(ctx.device, ctx.miss_shader_binding_table_buffer_memory, nullptr);
	vkFreeMemory(ctx.device, ctx.hit_shader_binding_table_buffer_memory, nullptr);
	vkFreeMemory(ctx.device, ctx.uniform_buffer_memory, nullptr);
	vkFreeMemory(ctx.device, ctx.vertex_buffer_memory, nullptr);
	vkFreeMemory(ctx.device, ctx.index_buffer_memory, nullptr);
	vkFreeMemory(ctx.device, ctx.transform_buffer_memory, nullptr);

	vkDestroyBuffer(ctx.device, ctx.raygen_shader_binding_table_buffer, nullptr);
	vkDestroyBuffer(ctx.device, ctx.miss_shader_binding_table_buffer, nullptr);
	vkDestroyBuffer(ctx.device, ctx.hit_shader_binding_table_buffer, nullptr);
	vkDestroyBuffer(ctx.device, ctx.uniform_buffer, nullptr);
	vkDestroyBuffer(ctx.device, ctx.vertex_buffer, nullptr);
	vkDestroyBuffer(ctx.device, ctx.index_buffer, nullptr);
	vkDestroyBuffer(ctx.device, ctx.transform_buffer, nullptr);

	vkDestroyImageView(ctx.device, ctx.storage_image_view, nullptr);
	vkFreeMemory(ctx.device, ctx.storage_image_memory, nullptr);
	vkDestroyImage(ctx.device, ctx.storage_image, nullptr);

	vkDestroySemaphore(ctx.device, ctx.image_available_semaphore, nullptr);
	vkDestroySemaphore(ctx.device, ctx.render_finished_semaphore, nullptr);
	vkDestroyFence(ctx.device, ctx.in_flight_fence, nullptr);

	vkDestroyCommandPool(ctx.device, ctx.command_pool, nullptr);

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
