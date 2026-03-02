/**
 * vk_buffer.h
 *
 * GPU buffer management: vertex buffers, uniform buffers, staging uploads.
 * Uses raw Vulkan memory allocation (VMA integration deferred to Phase 4+).
 */

#ifndef VK_BUFFER_H
#define VK_BUFFER_H

#include "vk_init.h"
#include "vk_command.h"
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Buffer handle
// ---------------------------------------------------------------------------
typedef struct GvBuffer {
    VkBuffer       buffer;
    VkDeviceMemory memory;
    VkDeviceSize   size;
    void          *mapped;  // non-null if persistently mapped (uniform buffers)
} GvBuffer;

// ---------------------------------------------------------------------------
// Creation helpers
// ---------------------------------------------------------------------------

/**
 * Create a GPU buffer with the specified usage and memory properties.
 */
bool gv_buffer_create(const VkContext *ctx, VkDeviceSize size,
                       VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags memProps,
                       GvBuffer *out);

/**
 * Destroy a buffer and free its memory.
 */
void gv_buffer_destroy(const VkContext *ctx, GvBuffer *buf);

/**
 * Create a device-local vertex buffer by uploading data through a staging buffer.
 */
bool gv_buffer_create_vertex(const VkContext *ctx, VkCommandPool pool,
                              const void *data, VkDeviceSize size,
                              GvBuffer *out);

/**
 * Create a host-visible, persistently mapped uniform buffer.
 */
bool gv_buffer_create_uniform(const VkContext *ctx, VkDeviceSize size,
                               GvBuffer *out);

/**
 * Copy data to a mapped uniform buffer.
 */
void gv_buffer_update(GvBuffer *buf, const void *data, VkDeviceSize size);

#endif /* VK_BUFFER_H */
