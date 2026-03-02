/**
 * vk_environment.c
 *
 * Procedural studio cube map environment — implementation.
 *
 * Generation: for each of the 6 cube faces, the direction vector for every
 * 64×64 texel is computed using the Vulkan / OpenGL cube-map face-selection
 * tables, then evaluated against a simple analytic studio model (base
 * luminance gradient + four Gaussian hotspot "lights"). Values are stored as
 * VK_FORMAT_R8G8B8A8_UNORM linear in [0, 1]; bright studio lights with peaks
 * above 1.0 are clamped. The fragment shader multiplies by envMapIntensity
 * (typically 2.0–2.5 for diamond) to recover the HDR range before tone
 * mapping.
 *
 * Upload: pixel data is written to a host-visible staging buffer, then copied
 * to a device-local VK_IMAGE_TYPE_2D / CUBE image via a one-shot command
 * buffer before the first frame renders.
 */

#include "vk_environment.h"
#include "vk_command.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */
#define ENV_SIZE        256             /* texels per cube face edge */
#define ENV_CHANNELS    4               /* RGBA */
#define ENV_FACE_BYTES  (ENV_SIZE * ENV_SIZE * ENV_CHANNELS)
#define ENV_TOTAL_BYTES (ENV_FACE_BYTES * 6)

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/** Identical logic to vk_buffer.c; duplicated to keep modules self-contained. */
static uint32_t find_memory_type(const VkContext      *ctx,
                                  uint32_t              typeFilter,
                                  VkMemoryPropertyFlags properties)
{
    for (uint32_t i = 0; i < ctx->memoryProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (ctx->memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    fprintf(stderr, "[vk_environment] No suitable memory type found.\n");
    return 0;
}

/**
 * Convert a cube-face texel (face 0–5, pixel x,y) to a normalised world-space
 * direction vector.
 *
 * Face order: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z  (matches Vulkan/OpenGL).
 *
 * The Vulkan image coordinate system has v=0 at the TOP of the image, which
 * is opposite to the OpenGL cube-map tc convention (tc=+1 = top of face in
 * world space). A v-flip (t_gl = -t) corrects for this so that in world
 * space: face +Y centre → (0,+1,0) and face -Y centre → (0,-1,0).
 */
static void face_pixel_to_dir(int face, int px, int py, int size,
                               float *dx, float *dy, float *dz)
{
    /* Texel centre in normalised [0, 1] image coordinates */
    float u = (px + 0.5f) / (float)size;
    float v = (py + 0.5f) / (float)size;

    /* NDC in [-1, 1]: s is the horizontal (sc), t is the vertical (tc) */
    float s    =  2.0f * u - 1.0f;
    float t    =  2.0f * v - 1.0f;
    float t_gl = -t;  /* flip V: Vulkan v=0 is top; OpenGL tc=+1 is top */

    /* Apply OpenGL Table 8.19 (same convention used by Vulkan spec): */
    switch (face) {
        case 0: *dx =  1.0f; *dy = -t_gl; *dz = -s;    break; /* +X */
        case 1: *dx = -1.0f; *dy = -t_gl; *dz =  s;    break; /* -X */
        case 2: *dx =  s;    *dy =  1.0f; *dz =  t_gl; break; /* +Y */
        case 3: *dx =  s;    *dy = -1.0f; *dz = -t_gl; break; /* -Y */
        case 4: *dx =  s;    *dy = -t_gl; *dz =  1.0f; break; /* +Z */
        case 5: *dx = -s;    *dy = -t_gl; *dz = -1.0f; break; /* -Z */
        default: *dx = 0; *dy = 1; *dz = 0; return;
    }

    float len = sqrtf(*dx * *dx + *dy * *dy + *dz * *dz);
    if (len > 1e-5f) { *dx /= len; *dy /= len; *dz /= len; }
}

/**
 * Analytic studio environment model.
 *
 * Parameterised to approximate Three.js RoomEnvironment: a neutral studio
 * "room" with a warm key light, cooler fill, rim accent, broad ceiling panel,
 * and back-wall accent.  The asymmetric placement ensures adjacent facets
 * (pointing in slightly different directions) receive noticeably different
 * irradiance, which is the primary source of per-facet variation in the JS
 * reference.
 *
 * All output channels are linear and clamped to [0, 1] for UNORM storage.
 * The fragment shader multiplies by ENV_STORED_RANGE (5.0) to recover
 * effective HDR brightness, then by envMapIntensity per preset.
 */
static void sample_environment(float dx, float dy, float dz,
                                uint8_t *out_rgba)
{
    /* Base luminance: gradient from dim floor to brighter ceiling */
    float base = 0.06f + 0.30f * (dy * 0.5f + 0.5f);   /* ~0.06 (floor) - 0.36 (ceiling) */

    float r = base, g = base, b = base;

    /* --- Key light --------------------------------------------------------
     * Bright white hotspot, upper front-right.  Creates the main specular
     * "fire" patch that dominates reflections on the crown facets. */
    {
        float kx = dx - 0.15f, ky = dy - 0.75f, kz = dz - 0.65f;
        float w   = expf(-4.0f * (kx*kx + ky*ky + kz*kz));
        r += w * 0.90f;
        g += w * 0.88f;
        b += w * 0.82f;  /* very slightly warm */
    }

    /* --- Fill light -------------------------------------------------------
     * Left side, slightly cool.  Provides a second distinct bright region. */
    {
        float fx = dx + 0.70f, fy = dy - 0.35f, fz = dz + 0.25f;
        float w  = expf(-4.5f * (fx*fx + fy*fy + fz*fz));
        r += w * 0.38f;
        g += w * 0.42f;
        b += w * 0.52f;  /* cool blue-white */
    }

    /* --- Ceiling panel ----------------------------------------------------
     * Broad warm overhead light (like a studio softbox).  Adds a large
     * high-luminance region distinct from the point-like key/fill. */
    {
        float cy = dy - 1.0f;
        float w  = expf(-3.0f * cy * cy) * expf(-0.5f * (dx*dx + dz*dz));
        r += w * 0.60f;
        g += w * 0.55f;
        b += w * 0.45f;  /* warm */
    }

    /* --- Rim / back light -------------------------------------------------
     * Behind and to the right.  Adds a third reflective patch so the
     * pavilion facets (pointing backward) have something bright to reflect. */
    {
        float rx = dx - 0.35f, ry = dy - 0.15f, rz = dz + 0.80f;
        float w  = expf(-5.0f * (rx*rx + ry*ry + rz*rz));
        r += w * 0.30f;
        g += w * 0.32f;
        b += w * 0.38f;
    }

    /* --- Back wall accent -------------------------------------------------
     * Smaller bright patch on the opposite back-left wall.  Ensures the
     * lower pavilion facets pick up yet another distinct reflection. */
    {
        float bx = dx + 0.30f, by = dy - 0.20f, bz = dz + 0.90f;
        float w  = expf(-6.0f * (bx*bx + by*by + bz*bz));
        r += w * 0.25f;
        g += w * 0.22f;
        b += w * 0.18f;
    }

    /* --- Warm ground bounce -----------------------------------------------
     * Subtle upward-facing warm contribution from the "floor". */
    {
        float gx = dx - 0.15f, gy = dy + 0.85f, gz = dz - 0.25f;
        float w  = expf(-5.0f * (gx*gx + gy*gy + gz*gz));
        r += w * 0.22f;
        g += w * 0.18f;
        b += w * 0.12f;  /* warm */
    }

    /* Clamp to [0, 1] (fragment shader * ENV_STORED_RANGE recovers HDR range) */
    out_rgba[0] = (uint8_t)(fminf(r, 1.0f) * 255.0f + 0.5f);
    out_rgba[1] = (uint8_t)(fminf(g, 1.0f) * 255.0f + 0.5f);
    out_rgba[2] = (uint8_t)(fminf(b, 1.0f) * 255.0f + 0.5f);
    out_rgba[3] = 255;
}

/** Insert a VkImageMemoryBarrier to transition @p image between layouts. */
static void image_barrier(VkCommandBuffer      cmd,
                           VkImage              image,
                           VkImageLayout        oldLayout,
                           VkImageLayout        newLayout,
                           VkAccessFlags        srcAccess,
                           VkAccessFlags        dstAccess,
                           VkPipelineStageFlags srcStage,
                           VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout           = oldLayout,
        .newLayout           = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 6,
        },
        .srcAccessMask = srcAccess,
        .dstAccessMask = dstAccess,
    };

    vkCmdPipelineBarrier(cmd, srcStage, dstStage,
                         0, 0, NULL, 0, NULL, 1, &barrier);
}

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

bool gv_environment_create(const VkContext *ctx,
                            VkCommandPool   commandPool,
                            GvEnvMap       *out)
{
    memset(out, 0, sizeof(*out));

    /* ------------------------------------------------------------------
     * 1. Generate pixel data for all six faces on the CPU.
     * ------------------------------------------------------------------ */
    uint8_t *pixels = malloc(ENV_TOTAL_BYTES);
    if (!pixels) {
        fprintf(stderr, "[vk_environment] Out of memory for pixel data.\n");
        return false;
    }

    for (int face = 0; face < 6; face++) {
        uint8_t *facePtr = pixels + face * ENV_FACE_BYTES;
        for (int py = 0; py < ENV_SIZE; py++) {
            for (int px = 0; px < ENV_SIZE; px++) {
                float dx, dy, dz;
                face_pixel_to_dir(face, px, py, ENV_SIZE, &dx, &dy, &dz);
                uint8_t *texel = facePtr + (py * ENV_SIZE + px) * ENV_CHANNELS;
                sample_environment(dx, dy, dz, texel);
            }
        }
    }

    /* ------------------------------------------------------------------
     * 2. Create a host-visible staging buffer and copy pixel data.
     * ------------------------------------------------------------------ */
    VkBufferCreateInfo stagingCI = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = ENV_TOTAL_BYTES,
        .usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VkBuffer       stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;

    if (vkCreateBuffer(ctx->device, &stagingCI, NULL, &stagingBuf) != VK_SUCCESS) {
        fprintf(stderr, "[vk_environment] Failed to create staging buffer.\n");
        free(pixels);
        return false;
    }

    VkMemoryRequirements stagingReqs;
    vkGetBufferMemoryRequirements(ctx->device, stagingBuf, &stagingReqs);

    VkMemoryAllocateInfo stagingAllocInfo = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = stagingReqs.size,
        .memoryTypeIndex = find_memory_type(ctx, stagingReqs.memoryTypeBits,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };

    if (vkAllocateMemory(ctx->device, &stagingAllocInfo, NULL, &stagingMem) != VK_SUCCESS ||
        vkBindBufferMemory(ctx->device, stagingBuf, stagingMem, 0) != VK_SUCCESS) {
        fprintf(stderr, "[vk_environment] Failed to allocate / bind staging memory.\n");
        vkDestroyBuffer(ctx->device, stagingBuf, NULL);
        free(pixels);
        return false;
    }

    void *mapped;
    vkMapMemory(ctx->device, stagingMem, 0, ENV_TOTAL_BYTES, 0, &mapped);
    memcpy(mapped, pixels, ENV_TOTAL_BYTES);
    vkUnmapMemory(ctx->device, stagingMem);
    free(pixels);

    /* ------------------------------------------------------------------
     * 3. Create a device-local cube image.
     * ------------------------------------------------------------------ */
    VkImageCreateInfo imageCI = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags         = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = VK_FORMAT_R8G8B8A8_UNORM,
        .extent        = { ENV_SIZE, ENV_SIZE, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 6,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(ctx->device, &imageCI, NULL, &out->image) != VK_SUCCESS) {
        fprintf(stderr, "[vk_environment] Failed to create cube image.\n");
        goto cleanup_staging;
    }

    VkMemoryRequirements imageReqs;
    vkGetImageMemoryRequirements(ctx->device, out->image, &imageReqs);

    VkMemoryAllocateInfo imageAllocInfo = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = imageReqs.size,
        .memoryTypeIndex = find_memory_type(ctx, imageReqs.memoryTypeBits,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    if (vkAllocateMemory(ctx->device, &imageAllocInfo, NULL, &out->memory) != VK_SUCCESS ||
        vkBindImageMemory(ctx->device, out->image, out->memory, 0) != VK_SUCCESS) {
        fprintf(stderr, "[vk_environment] Failed to allocate / bind image memory.\n");
        goto cleanup_staging;
    }

    /* ------------------------------------------------------------------
     * 4. Upload: layout transition → copy → layout transition.
     * ------------------------------------------------------------------ */
    VkCommandBuffer cmd = vk_begin_single_command(ctx, commandPool);

    /* UNDEFINED → TRANSFER_DST_OPTIMAL */
    image_barrier(cmd, out->image,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  0, VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);

    /* Copy all 6 faces from the contiguous staging buffer */
    VkBufferImageCopy regions[6];
    for (uint32_t face = 0; face < 6; face++) {
        regions[face] = (VkBufferImageCopy){
            .bufferOffset      = (VkDeviceSize)(face * ENV_FACE_BYTES),
            .bufferRowLength   = 0,
            .bufferImageHeight = 0,
            .imageSubresource  = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel       = 0,
                .baseArrayLayer = face,
                .layerCount     = 1,
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { ENV_SIZE, ENV_SIZE, 1 },
        };
    }

    vkCmdCopyBufferToImage(cmd, stagingBuf, out->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 6, regions);

    /* TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL */
    image_barrier(cmd, out->image,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    vk_end_single_command(ctx, commandPool, cmd);

    /* Staging resources no longer needed after the GPU-side copy. */
    vkDestroyBuffer(ctx->device, stagingBuf, NULL);
    vkFreeMemory(ctx->device, stagingMem, NULL);

    /* ------------------------------------------------------------------
     * 5. Image view — cube type, 6 layers.
     * ------------------------------------------------------------------ */
    VkImageViewCreateInfo viewCI = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = out->image,
        .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
        .format   = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 6,
        },
    };

    if (vkCreateImageView(ctx->device, &viewCI, NULL, &out->view) != VK_SUCCESS) {
        fprintf(stderr, "[vk_environment] Failed to create cube image view.\n");
        return false;
    }

    /* ------------------------------------------------------------------
     * 6. Sampler — bilinear, seamless cube edges, repeat/clamp irrelevant.
     * ------------------------------------------------------------------ */
    VkSamplerCreateInfo samplerCI = {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter               = VK_FILTER_LINEAR,
        .minFilter               = VK_FILTER_LINEAR,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias              = 0.0f,
        .anisotropyEnable        = VK_FALSE,
        .compareEnable           = VK_FALSE,
        .minLod                  = 0.0f,
        .maxLod                  = 0.0f,
        .borderColor             = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        .unnormalizedCoordinates = VK_FALSE,
    };

    if (vkCreateSampler(ctx->device, &samplerCI, NULL, &out->sampler) != VK_SUCCESS) {
        fprintf(stderr, "[vk_environment] Failed to create cube sampler.\n");
        return false;
    }

    fprintf(stderr, "[vk_environment] Procedural cube map created (%dx%d × 6).\n",
            ENV_SIZE, ENV_SIZE);
    return true;

cleanup_staging:
    vkDestroyBuffer(ctx->device, stagingBuf, NULL);
    if (stagingMem != VK_NULL_HANDLE) vkFreeMemory(ctx->device, stagingMem, NULL);
    return false;
}

void gv_environment_destroy(const VkContext *ctx, GvEnvMap *env)
{
    if (ctx->device == VK_NULL_HANDLE || env == NULL) return;

    if (env->sampler  != VK_NULL_HANDLE) vkDestroySampler(ctx->device, env->sampler, NULL);
    if (env->view     != VK_NULL_HANDLE) vkDestroyImageView(ctx->device, env->view, NULL);
    if (env->image    != VK_NULL_HANDLE) vkDestroyImage(ctx->device, env->image, NULL);
    if (env->memory   != VK_NULL_HANDLE) vkFreeMemory(ctx->device, env->memory, NULL);

    memset(env, 0, sizeof(*env));
}
