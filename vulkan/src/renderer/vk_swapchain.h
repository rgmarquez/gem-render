/**
 * vk_swapchain.h
 *
 * Swapchain creation, recreation on resize, and image view management.
 */

#ifndef VK_SWAPCHAIN_H
#define VK_SWAPCHAIN_H

#include "vk_init.h"
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Swapchain state
// ---------------------------------------------------------------------------
typedef struct VkSwapchainState {
    VkSwapchainKHR   swapchain;
    VkFormat         imageFormat;
    VkExtent2D       extent;

    uint32_t         imageCount;
    VkImage         *images;       // owned by swapchain (not destroyed manually)
    VkImageView     *imageViews;   // we own these

    // Depth buffer
    VkImage          depthImage;
    VkDeviceMemory   depthMemory;
    VkImageView      depthImageView;
    VkFormat         depthFormat;
} VkSwapchainState;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * Create (or recreate) the swapchain and associated image views.
 *
 * @param ctx     Vulkan context (device, surface, etc.)
 * @param window  GLFW window (for framebuffer size query)
 * @param state   Output state — caller must call vk_swapchain_destroy().
 *                If state->swapchain is non-null, the old swapchain is
 *                passed as oldSwapchain for a smooth transition.
 * @return true on success.
 */
bool vk_swapchain_create(const VkContext *ctx, GLFWwindow *window,
                          VkSwapchainState *state);

/**
 * Destroy swapchain, image views, and depth buffer.
 * Safe to call on a zero-initialised struct.
 */
void vk_swapchain_destroy(const VkContext *ctx, VkSwapchainState *state);

#endif /* VK_SWAPCHAIN_H */
