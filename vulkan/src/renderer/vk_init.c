/**
 * vk_init.c
 *
 * Vulkan bootstrap implementation.
 *
 * Creates instance (with validation in debug builds), surface via GLFW,
 * selects a physical device, creates a logical device with graphics +
 * present queues.  Handles MoltenVK portability on macOS.
 */

#include "vk_init.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Forward declarations (internal helpers)
// ---------------------------------------------------------------------------
static bool create_instance(VkContext *ctx);
static bool setup_debug_messenger(VkContext *ctx);
static bool create_surface(GLFWwindow *window, VkContext *ctx);
static bool pick_physical_device(VkContext *ctx);
static bool create_logical_device(VkContext *ctx);

static bool find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface,
                                uint32_t *graphicsFamily, uint32_t *presentFamily);
static bool check_device_extension_support(VkPhysicalDevice device);
static int  rate_physical_device(VkPhysicalDevice device, VkSurfaceKHR surface);

// ---------------------------------------------------------------------------
// Validation layer callback
// ---------------------------------------------------------------------------
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             type,
    const VkDebugUtilsMessengerCallbackDataEXT *data,
    void                                       *user)
{
    (void)type;
    (void)user;

    const char *level = "INFO";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        level = "ERROR";
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        level = "WARN";

    fprintf(stderr, "[Vulkan %s] %s\n", level, data->pMessage);
    return VK_FALSE;
}

// ---------------------------------------------------------------------------
// Required device extensions
// ---------------------------------------------------------------------------
static const char *DEVICE_EXTENSIONS[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};
static const uint32_t DEVICE_EXTENSION_COUNT =
    sizeof(DEVICE_EXTENSIONS) / sizeof(DEVICE_EXTENSIONS[0]);

#ifdef GV_ENABLE_VALIDATION
static const char *VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation",
};
static const uint32_t VALIDATION_LAYER_COUNT =
    sizeof(VALIDATION_LAYERS) / sizeof(VALIDATION_LAYERS[0]);
#endif

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool vk_context_create(GLFWwindow *window, VkContext *ctx)
{
    memset(ctx, 0, sizeof(*ctx));

    if (!create_instance(ctx))          return false;
#ifdef GV_ENABLE_VALIDATION
    if (!setup_debug_messenger(ctx))    return false;
#endif
    if (!create_surface(window, ctx))   return false;
    if (!pick_physical_device(ctx))     return false;
    if (!create_logical_device(ctx))    return false;

    // Cache device properties for later use
    vkGetPhysicalDeviceProperties(ctx->physicalDevice, &ctx->deviceProperties);
    vkGetPhysicalDeviceMemoryProperties(ctx->physicalDevice, &ctx->memoryProperties);

    fprintf(stderr, "[vk_init] Using GPU: %s\n", ctx->deviceProperties.deviceName);
    return true;
}

void vk_context_destroy(VkContext *ctx)
{
    if (ctx->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(ctx->device);
        vkDestroyDevice(ctx->device, NULL);
    }

#ifdef GV_ENABLE_VALIDATION
    if (ctx->debugMessenger != VK_NULL_HANDLE) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroyFunc =
            (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(ctx->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroyFunc) {
            destroyFunc(ctx->instance, ctx->debugMessenger, NULL);
        }
    }
#endif

    if (ctx->surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);

    if (ctx->instance != VK_NULL_HANDLE)
        vkDestroyInstance(ctx->instance, NULL);

    memset(ctx, 0, sizeof(*ctx));
}

void vk_device_wait_idle(const VkContext *ctx)
{
    if (ctx->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(ctx->device);
    }
}

// ---------------------------------------------------------------------------
// Instance creation
// ---------------------------------------------------------------------------
static bool create_instance(VkContext *ctx)
{
    VkApplicationInfo appInfo = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = "Gemstone Viewer",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName        = "Custom",
        .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion         = VK_API_VERSION_1_2,
    };

    // Gather required extensions from GLFW + extras
    uint32_t glfwExtCount = 0;
    const char **glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    if (!glfwExts) {
        fprintf(stderr, "[vk_init] GLFW: no Vulkan support on this platform.\n");
        return false;
    }

    // Build extension list: GLFW required + optional extras
    uint32_t extCount = glfwExtCount;
    const char *extensions[32];
    for (uint32_t i = 0; i < glfwExtCount && i < 32; i++) {
        extensions[i] = glfwExts[i];
    }

#ifdef GV_ENABLE_VALIDATION
    extensions[extCount++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
#endif

#ifdef GV_PLATFORM_MACOS
    // MoltenVK portability: enumerate non-conformant devices
    extensions[extCount++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    extensions[extCount++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
#endif

    VkInstanceCreateInfo createInfo = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &appInfo,
        .enabledExtensionCount   = extCount,
        .ppEnabledExtensionNames = extensions,
    };

#ifdef GV_PLATFORM_MACOS
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

#ifdef GV_ENABLE_VALIDATION
    createInfo.enabledLayerCount   = VALIDATION_LAYER_COUNT;
    createInfo.ppEnabledLayerNames = VALIDATION_LAYERS;

    // Enable debug messenger during instance creation/destruction
    VkDebugUtilsMessengerCreateInfoEXT debugCI = {
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity  = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType      = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback  = debug_callback,
    };
    createInfo.pNext = &debugCI;
#endif

    VkResult result = vkCreateInstance(&createInfo, NULL, &ctx->instance);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[vk_init] vkCreateInstance failed: %d\n", result);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Debug messenger (validation layers)
// ---------------------------------------------------------------------------
static bool setup_debug_messenger(VkContext *ctx)
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity  = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType      = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback  = debug_callback,
    };

    PFN_vkCreateDebugUtilsMessengerEXT createFunc =
        (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(ctx->instance, "vkCreateDebugUtilsMessengerEXT");

    if (!createFunc) {
        fprintf(stderr, "[vk_init] Warning: debug utils extension not available.\n");
        return true;  // non-fatal
    }

    VkResult result = createFunc(ctx->instance, &createInfo, NULL, &ctx->debugMessenger);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[vk_init] Warning: failed to create debug messenger: %d\n", result);
    }

    return true;
}

// ---------------------------------------------------------------------------
// Surface creation (GLFW)
// ---------------------------------------------------------------------------
static bool create_surface(GLFWwindow *window, VkContext *ctx)
{
    VkResult result = glfwCreateWindowSurface(ctx->instance, window, NULL, &ctx->surface);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[vk_init] glfwCreateWindowSurface failed: %d\n", result);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Physical device selection
// ---------------------------------------------------------------------------
static int rate_physical_device(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    uint32_t graphicsFamily, presentFamily;
    if (!find_queue_families(device, surface, &graphicsFamily, &presentFamily))
        return -1;

    if (!check_device_extension_support(device))
        return -1;

    // Check swapchain support (must have at least one format and present mode)
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, NULL);
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, NULL);
    if (formatCount == 0 || presentModeCount == 0)
        return -1;

    int score = 0;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        score += 1000;
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        score += 500;

    score += (int)(props.limits.maxImageDimension2D / 1024);

    return score;
}

static bool pick_physical_device(VkContext *ctx)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &deviceCount, NULL);
    if (deviceCount == 0) {
        fprintf(stderr, "[vk_init] No GPUs with Vulkan support found.\n");
        return false;
    }

    VkPhysicalDevice *devices = malloc(deviceCount * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(ctx->instance, &deviceCount, devices);

    int bestScore = -1;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;

    for (uint32_t i = 0; i < deviceCount; i++) {
        int score = rate_physical_device(devices[i], ctx->surface);
        if (score > bestScore) {
            bestScore = score;
            bestDevice = devices[i];
        }
    }

    free(devices);

    if (bestDevice == VK_NULL_HANDLE) {
        fprintf(stderr, "[vk_init] No suitable GPU found.\n");
        return false;
    }

    ctx->physicalDevice = bestDevice;
    return true;
}

// ---------------------------------------------------------------------------
// Queue family discovery
// ---------------------------------------------------------------------------
static bool find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface,
                                uint32_t *graphicsFamily, uint32_t *presentFamily)
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);

    VkQueueFamilyProperties *families = malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, families);

    bool foundGraphics = false;
    bool foundPresent = false;

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            *graphicsFamily = i;
            foundGraphics = true;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) {
            *presentFamily = i;
            foundPresent = true;
        }

        // Prefer a single queue family that supports both
        if (foundGraphics && foundPresent && *graphicsFamily == *presentFamily)
            break;
    }

    free(families);
    return foundGraphics && foundPresent;
}

// ---------------------------------------------------------------------------
// Device extension checks
// ---------------------------------------------------------------------------
static bool check_device_extension_support(VkPhysicalDevice device)
{
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extCount, NULL);

    VkExtensionProperties *available = malloc(extCount * sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(device, NULL, &extCount, available);

    bool allFound = true;
    for (uint32_t r = 0; r < DEVICE_EXTENSION_COUNT; r++) {
        bool found = false;
        for (uint32_t a = 0; a < extCount; a++) {
            if (strcmp(DEVICE_EXTENSIONS[r], available[a].extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            allFound = false;
            break;
        }
    }

    free(available);
    return allFound;
}

// ---------------------------------------------------------------------------
// Logical device creation
// ---------------------------------------------------------------------------
static bool create_logical_device(VkContext *ctx)
{
    uint32_t graphicsFamily, presentFamily;
    find_queue_families(ctx->physicalDevice, ctx->surface,
                        &graphicsFamily, &presentFamily);

    ctx->graphicsFamily = graphicsFamily;
    ctx->presentFamily = presentFamily;

    // Build unique queue create infos
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCIs[2];
    uint32_t queueCICount = 0;

    queueCIs[queueCICount++] = (VkDeviceQueueCreateInfo){
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = graphicsFamily,
        .queueCount       = 1,
        .pQueuePriorities = &queuePriority,
    };

    if (presentFamily != graphicsFamily) {
        queueCIs[queueCICount++] = (VkDeviceQueueCreateInfo){
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = presentFamily,
            .queueCount       = 1,
            .pQueuePriorities = &queuePriority,
        };
    }

    // Device extensions: required + portability subset on macOS
    uint32_t devExtCount = DEVICE_EXTENSION_COUNT;
    const char *devExtensions[16];
    for (uint32_t i = 0; i < DEVICE_EXTENSION_COUNT; i++) {
        devExtensions[i] = DEVICE_EXTENSIONS[i];
    }

#ifdef GV_PLATFORM_MACOS
    // Check if portability subset is available and enable it
    {
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(ctx->physicalDevice, NULL, &extCount, NULL);
        VkExtensionProperties *available = malloc(extCount * sizeof(VkExtensionProperties));
        vkEnumerateDeviceExtensionProperties(ctx->physicalDevice, NULL, &extCount, available);

        for (uint32_t i = 0; i < extCount; i++) {
            if (strcmp(available[i].extensionName,
                       "VK_KHR_portability_subset") == 0) {
                devExtensions[devExtCount++] = "VK_KHR_portability_subset";
                break;
            }
        }
        free(available);
    }
#endif

    // Enable features needed for the gem shader
    VkPhysicalDeviceFeatures deviceFeatures = {
        .samplerAnisotropy = VK_TRUE,
        .fillModeNonSolid  = VK_TRUE,  // for wireframe debug
    };

    VkDeviceCreateInfo createInfo = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = queueCICount,
        .pQueueCreateInfos       = queueCIs,
        .enabledExtensionCount   = devExtCount,
        .ppEnabledExtensionNames = devExtensions,
        .pEnabledFeatures        = &deviceFeatures,
    };

    VkResult result = vkCreateDevice(ctx->physicalDevice, &createInfo, NULL, &ctx->device);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[vk_init] vkCreateDevice failed: %d\n", result);
        return false;
    }

    vkGetDeviceQueue(ctx->device, graphicsFamily, 0, &ctx->graphicsQueue);
    vkGetDeviceQueue(ctx->device, presentFamily, 0, &ctx->presentQueue);

    return true;
}
