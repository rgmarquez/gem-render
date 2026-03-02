/**
 * vk_background.h
 *
 * Off-screen background render target used for screen-space transmission.
 *
 * Renders the scene (without transmissive geometry) into a 2D texture each
 * frame, mirroring Three.js's OpaqueRenderTarget approach. The gem fragment
 * shader samples this texture through a screen-space projected refracted ray.
 *
 * Format:    VK_FORMAT_R8G8B8A8_UNORM, 2D, swapchain extent.
 * Images:    GV_MAX_FRAMES_IN_FLIGHT — one per frame in flight.
 * Alpha:     Cleared to 0.0 (empty — no scene geometry present).
 *            When scene objects are rendered here they write alpha = 1.0,
 *            which the gem shader uses to blend screen-space over env-based
 *            refraction.
 *
 * Lifecycle note:
 *   - gv_background_create() / gv_background_destroy() follow the swapchain.
 *   - gv_background_resize() can be called on swapchain recreation; it
 *     destroys and recreates the extent-dependent resources (image, view,
 *     framebuffer) while keeping the render pass and sampler alive.
 */

#ifndef VK_BACKGROUND_H
#define VK_BACKGROUND_H

#include "vk_init.h"
#include "vk_swapchain.h"
#include "vk_command.h"   /* GV_MAX_FRAMES_IN_FLIGHT */
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * Handle
 * ---------------------------------------------------------------------- */

typedef struct GvBackground {
    VkImage        images[GV_MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory memories[GV_MAX_FRAMES_IN_FLIGHT];
    VkImageView    views[GV_MAX_FRAMES_IN_FLIGHT];

    VkSampler      sampler;      // shared — extent-independent
    VkRenderPass   renderPass;   // single colour attachment — extent-independent
    VkFramebuffer  framebuffers[GV_MAX_FRAMES_IN_FLIGHT];

    VkExtent2D     extent;       // current image dimensions
} GvBackground;

/* ---------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/**
 * Allocate the background images, render pass, framebuffers, and sampler.
 *
 * Must be called after the swapchain is ready. Caller owns the output and
 * must release it with gv_background_destroy().
 */
bool gv_background_create(const VkContext        *ctx,
                           const VkSwapchainState *swapchain,
                           GvBackground           *bg);

/**
 * Destroy and recreate the extent-dependent resources (images, views,
 * framebuffers) to match a resized swapchain. The render pass and sampler
 * are reused. The device must be idle before calling.
 *
 * After returning, callers should re-write binding 3 of all descriptor sets
 * that reference the background views (use vk_pipeline_update_background).
 */
bool gv_background_resize(const VkContext        *ctx,
                           const VkSwapchainState *swapchain,
                           GvBackground           *bg);

/**
 * Release all Vulkan objects. Safe on a zero-initialised struct.
 */
void gv_background_destroy(const VkContext *ctx, GvBackground *bg);

#endif /* VK_BACKGROUND_H */
