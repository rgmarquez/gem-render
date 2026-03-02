/**
 * vk_command.c
 *
 * Command pool, per-frame command buffers, synchronisation objects,
 * and one-shot command helper for staging uploads.
 */

#include "vk_command.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool vk_framesync_create(const VkContext *ctx, uint32_t swapchainImageCount,
                         VkFrameSync *sync)
{
    memset(sync, 0, sizeof(*sync));
    sync->renderFinishedCount = swapchainImageCount;

    // Command pool
    VkCommandPoolCreateInfo poolCI = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->graphicsFamily,
    };

    if (vkCreateCommandPool(ctx->device, &poolCI, NULL, &sync->commandPool) != VK_SUCCESS) {
        fprintf(stderr, "[vk_command] Failed to create command pool.\n");
        return false;
    }

    // Command buffers (one per frame in flight)
    VkCommandBufferAllocateInfo allocInfo = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = sync->commandPool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = GV_MAX_FRAMES_IN_FLIGHT,
    };

    if (vkAllocateCommandBuffers(ctx->device, &allocInfo, sync->commandBuffers) != VK_SUCCESS) {
        fprintf(stderr, "[vk_command] Failed to allocate command buffers.\n");
        return false;
    }

    // Synchronisation primitives
    VkSemaphoreCreateInfo semCI = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkFenceCreateInfo fenceCI = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,  // start signaled so first wait succeeds
    };

    // Per frame-in-flight: imageAvailable semaphore + submission fence
    for (uint32_t i = 0; i < GV_MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(ctx->device, &semCI, NULL, &sync->imageAvailable[i]) != VK_SUCCESS ||
            vkCreateFence(ctx->device, &fenceCI, NULL, &sync->inFlightFences[i]) != VK_SUCCESS) {
            fprintf(stderr, "[vk_command] Failed to create sync objects for frame %u.\n", i);
            return false;
        }
    }

    // Per swapchain image: renderFinished semaphore
    for (uint32_t i = 0; i < swapchainImageCount; i++) {
        if (vkCreateSemaphore(ctx->device, &semCI, NULL, &sync->renderFinished[i]) != VK_SUCCESS) {
            fprintf(stderr, "[vk_command] Failed to create renderFinished semaphore %u.\n", i);
            return false;
        }
    }

    // Initially no image is in flight
    for (uint32_t i = 0; i < GV_MAX_SWAPCHAIN_IMAGES; i++) {
        sync->imagesInFlight[i] = VK_NULL_HANDLE;
    }

    return true;
}

void vk_framesync_destroy(const VkContext *ctx, VkFrameSync *sync)
{
    if (ctx->device == VK_NULL_HANDLE) return;

    // Per swapchain image: renderFinished semaphores
    for (uint32_t i = 0; i < sync->renderFinishedCount; i++) {
        if (sync->renderFinished[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(ctx->device, sync->renderFinished[i], NULL);
    }

    // Per frame-in-flight: imageAvailable semaphores + fences
    for (uint32_t i = 0; i < GV_MAX_FRAMES_IN_FLIGHT; i++) {
        if (sync->imageAvailable[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(ctx->device, sync->imageAvailable[i], NULL);
        if (sync->inFlightFences[i] != VK_NULL_HANDLE)
            vkDestroyFence(ctx->device, sync->inFlightFences[i], NULL);
    }

    if (sync->commandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(ctx->device, sync->commandPool, NULL);

    memset(sync, 0, sizeof(*sync));
}

// ---------------------------------------------------------------------------
// One-shot command buffer
// ---------------------------------------------------------------------------

VkCommandBuffer vk_begin_single_command(const VkContext *ctx, VkCommandPool pool)
{
    VkCommandBufferAllocateInfo allocInfo = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx->device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

void vk_end_single_command(const VkContext *ctx, VkCommandPool pool,
                            VkCommandBuffer cmd)
{
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cmd,
    };

    vkQueueSubmit(ctx->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->graphicsQueue);

    vkFreeCommandBuffers(ctx->device, pool, 1, &cmd);
}
