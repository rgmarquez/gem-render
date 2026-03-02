/**
 * vk_renderpass.c
 *
 * Main render pass: single subpass with color + depth attachments.
 * Color attachment loads to clear (black), stores for present.
 * Depth attachment loads to clear (1.0), store don't care.
 */

#include "vk_renderpass.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool vk_renderpass_create(const VkContext *ctx,
                           const VkSwapchainState *swapchain,
                           VkRenderPassState *state)
{
    memset(state, 0, sizeof(*state));

    // -- Attachment descriptions ------------------------------------------
    VkAttachmentDescription colorAttachment = {
        .format         = swapchain->imageFormat,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentDescription depthAttachment = {
        .format         = swapchain->depthFormat,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };

    // -- Subpass -----------------------------------------------------------
    VkAttachmentReference colorRef = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference depthRef = {
        .attachment = 1,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount    = 1,
        .pColorAttachments       = &colorRef,
        .pDepthStencilAttachment = &depthRef,
    };

    // -- Subpass dependency ------------------------------------------------
    VkSubpassDependency dependency = {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = 0,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };

    // -- Create render pass -----------------------------------------------
    VkRenderPassCreateInfo rpCI = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments    = attachments,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dependency,
    };

    VkResult result = vkCreateRenderPass(ctx->device, &rpCI, NULL, &state->renderPass);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[vk_renderpass] vkCreateRenderPass failed: %d\n", result);
        return false;
    }

    // -- Framebuffers (one per swapchain image) ---------------------------
    state->framebufferCount = swapchain->imageCount;
    state->framebuffers = malloc(state->framebufferCount * sizeof(VkFramebuffer));

    for (uint32_t i = 0; i < state->framebufferCount; i++) {
        VkImageView fbAttachments[] = {
            swapchain->imageViews[i],
            swapchain->depthImageView,
        };

        VkFramebufferCreateInfo fbCI = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = state->renderPass,
            .attachmentCount = 2,
            .pAttachments    = fbAttachments,
            .width           = swapchain->extent.width,
            .height          = swapchain->extent.height,
            .layers          = 1,
        };

        if (vkCreateFramebuffer(ctx->device, &fbCI, NULL, &state->framebuffers[i]) != VK_SUCCESS) {
            fprintf(stderr, "[vk_renderpass] Failed to create framebuffer %u.\n", i);
            return false;
        }
    }

    return true;
}

void vk_renderpass_destroy(const VkContext *ctx, VkRenderPassState *state)
{
    if (ctx->device == VK_NULL_HANDLE) return;

    if (state->framebuffers) {
        for (uint32_t i = 0; i < state->framebufferCount; i++) {
            vkDestroyFramebuffer(ctx->device, state->framebuffers[i], NULL);
        }
        free(state->framebuffers);
    }

    if (state->renderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(ctx->device, state->renderPass, NULL);

    memset(state, 0, sizeof(*state));
}
