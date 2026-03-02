/**
 * vk_command.h
 *
 * Command pool, per-frame command buffers, and synchronisation primitives.
 */

#ifndef VK_COMMAND_H
#define VK_COMMAND_H

#include "vk_init.h"
#include <stdbool.h>

#define GV_MAX_FRAMES_IN_FLIGHT 2
#define GV_MAX_SWAPCHAIN_IMAGES 8

// ---------------------------------------------------------------------------
// Frame synchronisation state
// ---------------------------------------------------------------------------
typedef struct VkFrameSync {
    VkCommandPool   commandPool;
    VkCommandBuffer commandBuffers[GV_MAX_FRAMES_IN_FLIGHT];

    // Per frame-in-flight: acquire semaphore + submission fence
    VkSemaphore     imageAvailable[GV_MAX_FRAMES_IN_FLIGHT];
    VkFence         inFlightFences[GV_MAX_FRAMES_IN_FLIGHT];

    // Per swapchain image: render-finished semaphore (avoids reuse while
    // presentation engine still references the semaphore for a given image)
    VkSemaphore     renderFinished[GV_MAX_SWAPCHAIN_IMAGES];
    uint32_t        renderFinishedCount;  // == swapchain image count

    // Tracks which in-flight fence is associated with each swapchain image.
    // VK_NULL_HANDLE means the image is not in flight.
    VkFence         imagesInFlight[GV_MAX_SWAPCHAIN_IMAGES];

    uint32_t        currentFrame;  // toggles 0..MAX_FRAMES-1
} VkFrameSync;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool vk_framesync_create(const VkContext *ctx, uint32_t swapchainImageCount,
                         VkFrameSync *sync);
void vk_framesync_destroy(const VkContext *ctx, VkFrameSync *sync);

// ---------------------------------------------------------------------------
// One-shot command buffer helpers (for staging uploads, layout transitions)
// ---------------------------------------------------------------------------

VkCommandBuffer vk_begin_single_command(const VkContext *ctx, VkCommandPool pool);
void            vk_end_single_command(const VkContext *ctx, VkCommandPool pool,
                                       VkCommandBuffer cmd);

#endif /* VK_COMMAND_H */
