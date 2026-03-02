/**
 * vk_renderpass.h
 *
 * Render pass creation and framebuffer management.
 * Phase 1-3: single render pass with color + depth attachments.
 */

#ifndef VK_RENDERPASS_H
#define VK_RENDERPASS_H

#include "vk_init.h"
#include "vk_swapchain.h"
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Render pass + framebuffers
// ---------------------------------------------------------------------------
typedef struct VkRenderPassState {
    VkRenderPass  renderPass;
    VkFramebuffer *framebuffers;   // one per swapchain image
    uint32_t       framebufferCount;
} VkRenderPassState;

/**
 * Create the main render pass (color + depth) and framebuffers.
 */
bool vk_renderpass_create(const VkContext *ctx,
                           const VkSwapchainState *swapchain,
                           VkRenderPassState *state);

/**
 * Destroy render pass and framebuffers.
 */
void vk_renderpass_destroy(const VkContext *ctx, VkRenderPassState *state);

#endif /* VK_RENDERPASS_H */
