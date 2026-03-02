/**
 * vk_init.h
 *
 * Vulkan bootstrap: instance, debug messenger, surface, physical device,
 * logical device, and queue handles.
 *
 * Handles MoltenVK portability extensions on macOS.
 */

#ifndef VK_INIT_H
#define VK_INIT_H

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdbool.h>

// ---------------------------------------------------------------------------
// Context — owns all core Vulkan objects for the application lifetime
// ---------------------------------------------------------------------------
typedef struct VkContext {
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR             surface;
    VkPhysicalDevice         physicalDevice;
    VkDevice                 device;

    // Queue handles
    VkQueue                  graphicsQueue;
    VkQueue                  presentQueue;
    uint32_t                 graphicsFamily;
    uint32_t                 presentFamily;

    // Physical device properties (cached for later use)
    VkPhysicalDeviceProperties      deviceProperties;
    VkPhysicalDeviceMemoryProperties memoryProperties;
} VkContext;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * Create Vulkan instance, pick physical device, create logical device.
 *
 * @param window  GLFW window (needed for surface creation).
 * @param ctx     Output context — caller owns, must call vk_context_destroy().
 * @return true on success, false on fatal error (logged to stderr).
 */
bool vk_context_create(GLFWwindow *window, VkContext *ctx);

/**
 * Destroy all Vulkan objects owned by the context.
 * Safe to call on a zero-initialised struct.
 */
void vk_context_destroy(VkContext *ctx);

/**
 * Wait for the device to be idle (e.g. before cleanup or swapchain recreate).
 */
void vk_device_wait_idle(const VkContext *ctx);

#endif /* VK_INIT_H */
