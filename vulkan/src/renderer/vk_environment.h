/**
 * vk_environment.h
 *
 * Procedural studio cube map environment.
 *
 * Approximates Three.js RoomEnvironment: warm-white ceiling, neutral walls,
 * a bright key-light hotspot, a cooler fill, and a dark floor. The asymmetric
 * hotspot placement is what drives per-facet colour variation on the gem: each
 * facet's outward normal reflects a different part of the environment map, so
 * adjacent facets receive visibly different shading even without direct lights.
 *
 * Format: VK_FORMAT_R8G8B8A8_UNORM, 64 × 64 per face, 6 layers (cube).
 * Values are linear (no gamma encoding). Multiply by envMapIntensity in the
 * fragment shader to control overall env brightness independently of the map.
 */

#ifndef VK_ENVIRONMENT_H
#define VK_ENVIRONMENT_H

#include "vk_init.h"
#include "vk_command.h"
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * Handle
 * ---------------------------------------------------------------------- */
typedef struct GvEnvMap {
    VkImage        image;
    VkDeviceMemory memory;
    VkImageView    view;
    VkSampler      sampler;
} GvEnvMap;

/* ---------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

/**
 * Generate a procedural cube map and upload it to the GPU.
 *
 * Uses a one-shot command buffer from @p commandPool for the staging upload
 * and image layout transitions. The command buffer is submitted and waited on
 * before returning.
 *
 * @param ctx         Vulkan context (device, queues, memory properties).
 * @param commandPool Pool used for the staging upload command buffer.
 * @param out         Caller-owned output; must be freed with gv_environment_destroy().
 * @return true on success.
 */
bool gv_environment_create(const VkContext     *ctx,
                            VkCommandPool        commandPool,
                            GvEnvMap            *out);

/**
 * Destroy all Vulkan objects owned by the environment map.
 * Safe to call on a zero-initialised struct.
 */
void gv_environment_destroy(const VkContext *ctx, GvEnvMap *env);

#endif /* VK_ENVIRONMENT_H */
