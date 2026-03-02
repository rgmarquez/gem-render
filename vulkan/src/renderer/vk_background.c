/**
 * vk_background.c
 *
 * Off-screen background render target for screen-space transmission.
 *
 * Each frame in flight gets its own VkImage so that frame N can write the
 * background while frame N-1's image is still being read by the gem shader.
 *
 * Synchronisation strategy:
 *   The background render pass declares:
 *     initialLayout = VK_IMAGE_LAYOUT_UNDEFINED   (discard stale content)
 *     finalLayout   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
 *   A subpass dependency (subpass 0 → VK_SUBPASS_EXTERNAL) inserts the memory
 *   barrier needed before the subsequent gem render pass reads the image. No
 *   explicit vkCmdPipelineBarrier is required in the command buffer.
 */

#include "vk_background.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Background image format — matches the env map (UNORM linear). */
#define BG_FORMAT VK_FORMAT_R8G8B8A8_UNORM

/* ---------------------------------------------------------------------------
 * Memory helpers (local copy — same pattern as vk_buffer.c / vk_environment.c)
 * ---------------------------------------------------------------------- */

static uint32_t find_memory_type(const VkContext *ctx,
                                  uint32_t         typeFilter,
                                  VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(ctx->physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    fprintf(stderr, "[vk_background] No suitable memory type found.\n");
    return UINT32_MAX;
}

/* ---------------------------------------------------------------------------
 * Per-frame image creation / destruction
 * ---------------------------------------------------------------------- */

static bool create_frame_images(const VkContext *ctx,
                                  VkExtent2D       extent,
                                  GvBackground    *bg)
{
    for (uint32_t i = 0; i < GV_MAX_FRAMES_IN_FLIGHT; i++) {

        /* Image ---------------------------------------------------------- */
        VkImageCreateInfo imgCI = {
            .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType     = VK_IMAGE_TYPE_2D,
            .format        = BG_FORMAT,
            .extent        = { extent.width, extent.height, 1 },
            .mipLevels     = 1,
            .arrayLayers   = 1,
            .samples       = VK_SAMPLE_COUNT_1_BIT,
            .tiling        = VK_IMAGE_TILING_OPTIMAL,
            .usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                             VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        if (vkCreateImage(ctx->device, &imgCI, NULL, &bg->images[i]) != VK_SUCCESS) {
            fprintf(stderr, "[vk_background] vkCreateImage failed (frame %u).\n", i);
            return false;
        }

        /* Memory --------------------------------------------------------- */
        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(ctx->device, bg->images[i], &memReqs);

        VkMemoryAllocateInfo allocInfo = {
            .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize  = memReqs.size,
            .memoryTypeIndex = find_memory_type(ctx, memReqs.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };
        if (allocInfo.memoryTypeIndex == UINT32_MAX) return false;

        if (vkAllocateMemory(ctx->device, &allocInfo, NULL, &bg->memories[i]) != VK_SUCCESS) {
            fprintf(stderr, "[vk_background] vkAllocateMemory failed (frame %u).\n", i);
            return false;
        }
        vkBindImageMemory(ctx->device, bg->images[i], bg->memories[i], 0);

        /* Image view ----------------------------------------------------- */
        VkImageViewCreateInfo viewCI = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = bg->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = BG_FORMAT,
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };

        if (vkCreateImageView(ctx->device, &viewCI, NULL, &bg->views[i]) != VK_SUCCESS) {
            fprintf(stderr, "[vk_background] vkCreateImageView failed (frame %u).\n", i);
            return false;
        }

        /* Framebuffer ---------------------------------------------------- */
        VkFramebufferCreateInfo fbCI = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = bg->renderPass,
            .attachmentCount = 1,
            .pAttachments    = &bg->views[i],
            .width           = extent.width,
            .height          = extent.height,
            .layers          = 1,
        };

        if (vkCreateFramebuffer(ctx->device, &fbCI, NULL, &bg->framebuffers[i]) != VK_SUCCESS) {
            fprintf(stderr, "[vk_background] vkCreateFramebuffer failed (frame %u).\n", i);
            return false;
        }
    }

    return true;
}

static void destroy_frame_images(const VkContext *ctx, GvBackground *bg)
{
    for (uint32_t i = 0; i < GV_MAX_FRAMES_IN_FLIGHT; i++) {
        if (bg->framebuffers[i] != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(ctx->device, bg->framebuffers[i], NULL);
            bg->framebuffers[i] = VK_NULL_HANDLE;
        }
        if (bg->views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(ctx->device, bg->views[i], NULL);
            bg->views[i] = VK_NULL_HANDLE;
        }
        if (bg->images[i] != VK_NULL_HANDLE) {
            vkDestroyImage(ctx->device, bg->images[i], NULL);
            bg->images[i] = VK_NULL_HANDLE;
        }
        if (bg->memories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(ctx->device, bg->memories[i], NULL);
            bg->memories[i] = VK_NULL_HANDLE;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

bool gv_background_create(const VkContext        *ctx,
                           const VkSwapchainState *swapchain,
                           GvBackground           *bg)
{
    memset(bg, 0, sizeof(*bg));
    bg->extent = swapchain->extent;

    /* Render pass -------------------------------------------------------- */
    /*
     * Single colour attachment. No depth — we only need the background colour
     * texel, not depth information, for the screen-space refraction lookup.
     *
     * Synchronisation: the subpass dependency from subpass 0 → EXTERNAL
     * (written at the end of the pass) covers the memory ordering between
     * this render pass's colour writes and the subsequent gem shader reads.
     * No explicit vkCmdPipelineBarrier is needed between the two render passes.
     */
    VkAttachmentDescription colorAtt = {
        .format         = BG_FORMAT,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,    /* discard stale content */
        .finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };

    VkAttachmentReference colorRef = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &colorRef,
    };

    /* Dependency: colour writes in this pass must be visible to fragment
     * shader reads in the following gem render pass.                       */
    VkSubpassDependency deps[2] = {
        {
            /* External → subpass 0: ensure previous reads are done before we
             * start writing (not strictly needed for the empty-scene case but
             * required for correctness when scene objects exist).           */
            .srcSubpass    = VK_SUBPASS_EXTERNAL,
            .dstSubpass    = 0,
            .srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        },
        {
            /* Subpass 0 → external: colour writes before gem shader reads. */
            .srcSubpass    = 0,
            .dstSubpass    = VK_SUBPASS_EXTERNAL,
            .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        },
    };

    VkRenderPassCreateInfo rpCI = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &colorAtt,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 2,
        .pDependencies   = deps,
    };

    if (vkCreateRenderPass(ctx->device, &rpCI, NULL, &bg->renderPass) != VK_SUCCESS) {
        fprintf(stderr, "[vk_background] vkCreateRenderPass failed.\n");
        return false;
    }

    /* Sampler ------------------------------------------------------------ */
    VkSamplerCreateInfo samplerCI = {
        .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter               = VK_FILTER_LINEAR,
        .minFilter               = VK_FILTER_LINEAR,
        .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .anisotropyEnable        = VK_FALSE,
        .maxAnisotropy           = 1.0f,
        .compareEnable           = VK_FALSE,
        .minLod                  = 0.0f,
        .maxLod                  = 0.0f,
        .borderColor             = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    if (vkCreateSampler(ctx->device, &samplerCI, NULL, &bg->sampler) != VK_SUCCESS) {
        fprintf(stderr, "[vk_background] vkCreateSampler failed.\n");
        return false;
    }

    /* Per-frame images, views, framebuffers ------------------------------ */
    if (!create_frame_images(ctx, swapchain->extent, bg)) return false;

    fprintf(stderr, "[vk_background] Created %ux%u background targets (%u frames).\n",
            swapchain->extent.width, swapchain->extent.height,
            GV_MAX_FRAMES_IN_FLIGHT);
    return true;
}

bool gv_background_resize(const VkContext        *ctx,
                           const VkSwapchainState *swapchain,
                           GvBackground           *bg)
{
    /* Device must be idle before calling (enforced by app_recreate_swapchain). */
    destroy_frame_images(ctx, bg);
    bg->extent = swapchain->extent;
    return create_frame_images(ctx, swapchain->extent, bg);
}

void gv_background_destroy(const VkContext *ctx, GvBackground *bg)
{
    if (!ctx || ctx->device == VK_NULL_HANDLE) return;

    destroy_frame_images(ctx, bg);

    if (bg->sampler    != VK_NULL_HANDLE)
        vkDestroySampler(ctx->device, bg->sampler, NULL);
    if (bg->renderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(ctx->device, bg->renderPass, NULL);

    memset(bg, 0, sizeof(*bg));
}
