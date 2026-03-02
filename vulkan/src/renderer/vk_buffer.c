/**
 * vk_buffer.c
 *
 * Buffer creation, staging upload, and uniform buffer management.
 */

#include "vk_buffer.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal: find a memory type matching requirements
// ---------------------------------------------------------------------------
static uint32_t find_memory_type(const VkContext *ctx, uint32_t typeFilter,
                                  VkMemoryPropertyFlags properties)
{
    for (uint32_t i = 0; i < ctx->memoryProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (ctx->memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    fprintf(stderr, "[vk_buffer] Failed to find suitable memory type.\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool gv_buffer_create(const VkContext *ctx, VkDeviceSize size,
                       VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags memProps,
                       GvBuffer *out)
{
    memset(out, 0, sizeof(*out));
    out->size = size;

    VkBufferCreateInfo bufCI = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(ctx->device, &bufCI, NULL, &out->buffer) != VK_SUCCESS) {
        fprintf(stderr, "[vk_buffer] Failed to create buffer.\n");
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(ctx->device, out->buffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memReqs.size,
        .memoryTypeIndex = find_memory_type(ctx, memReqs.memoryTypeBits, memProps),
    };

    if (vkAllocateMemory(ctx->device, &allocInfo, NULL, &out->memory) != VK_SUCCESS) {
        fprintf(stderr, "[vk_buffer] Failed to allocate buffer memory.\n");
        vkDestroyBuffer(ctx->device, out->buffer, NULL);
        out->buffer = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(ctx->device, out->buffer, out->memory, 0);
    return true;
}

void gv_buffer_destroy(const VkContext *ctx, GvBuffer *buf)
{
    if (ctx->device == VK_NULL_HANDLE) return;

    if (buf->mapped) {
        vkUnmapMemory(ctx->device, buf->memory);
        buf->mapped = NULL;
    }
    if (buf->buffer != VK_NULL_HANDLE)
        vkDestroyBuffer(ctx->device, buf->buffer, NULL);
    if (buf->memory != VK_NULL_HANDLE)
        vkFreeMemory(ctx->device, buf->memory, NULL);

    memset(buf, 0, sizeof(*buf));
}

bool gv_buffer_create_vertex(const VkContext *ctx, VkCommandPool pool,
                              const void *data, VkDeviceSize size,
                              GvBuffer *out)
{
    // Create staging buffer (host-visible)
    GvBuffer staging;
    if (!gv_buffer_create(ctx, size,
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           &staging)) {
        return false;
    }

    // Copy data to staging
    void *mapped;
    vkMapMemory(ctx->device, staging.memory, 0, size, 0, &mapped);
    memcpy(mapped, data, (size_t)size);
    vkUnmapMemory(ctx->device, staging.memory);

    // Create device-local vertex buffer
    if (!gv_buffer_create(ctx, size,
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           out)) {
        gv_buffer_destroy(ctx, &staging);
        return false;
    }

    // Copy staging -> device local via command buffer
    VkCommandBuffer cmd = vk_begin_single_command(ctx, pool);

    VkBufferCopy copyRegion = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size      = size,
    };
    vkCmdCopyBuffer(cmd, staging.buffer, out->buffer, 1, &copyRegion);

    vk_end_single_command(ctx, pool, cmd);

    gv_buffer_destroy(ctx, &staging);
    return true;
}

bool gv_buffer_create_uniform(const VkContext *ctx, VkDeviceSize size,
                               GvBuffer *out)
{
    if (!gv_buffer_create(ctx, size,
                           VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           out)) {
        return false;
    }

    // Persistently map for per-frame updates
    vkMapMemory(ctx->device, out->memory, 0, size, 0, &out->mapped);
    return true;
}

void gv_buffer_update(GvBuffer *buf, const void *data, VkDeviceSize size)
{
    if (buf->mapped && data) {
        memcpy(buf->mapped, data, (size_t)size);
    }
}
