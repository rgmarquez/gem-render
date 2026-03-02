/**
 * vk_swapchain.c
 *
 * Swapchain creation with SRGB format preference, FIFO present mode,
 * depth buffer, and image view management.  Supports recreation on resize.
 */

#include "vk_swapchain.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static VkSurfaceFormatKHR choose_surface_format(VkPhysicalDevice device,
                                                  VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, NULL);
    VkSurfaceFormatKHR *formats = malloc(count * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, formats);

    // Prefer SRGB + B8G8R8A8
    VkSurfaceFormatKHR chosen = formats[0];
    for (uint32_t i = 0; i < count; i++) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = formats[i];
            break;
        }
    }

    free(formats);
    return chosen;
}

static VkPresentModeKHR choose_present_mode(VkPhysicalDevice device,
                                              VkSurfaceKHR surface)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, NULL);
    VkPresentModeKHR *modes = malloc(count * sizeof(VkPresentModeKHR));
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, modes);

    // Prefer FIFO (vsync, guaranteed available) — good default
    VkPresentModeKHR chosen = VK_PRESENT_MODE_FIFO_KHR;

    // Check if mailbox is available (lower latency triple buffering)
    for (uint32_t i = 0; i < count; i++) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            chosen = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }

    free(modes);
    return chosen;
}

static VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR *caps,
                                 GLFWwindow *window)
{
    if (caps->currentExtent.width != UINT32_MAX) {
        // Driver already set the extent (common on most platforms)
        return caps->currentExtent;
    }

    // Retina / HiDPI: use framebuffer size, not window size
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D extent = {
        .width  = (uint32_t)width,
        .height = (uint32_t)height,
    };

    // Clamp to surface capabilities
    if (extent.width < caps->minImageExtent.width)
        extent.width = caps->minImageExtent.width;
    if (extent.width > caps->maxImageExtent.width)
        extent.width = caps->maxImageExtent.width;
    if (extent.height < caps->minImageExtent.height)
        extent.height = caps->minImageExtent.height;
    if (extent.height > caps->maxImageExtent.height)
        extent.height = caps->maxImageExtent.height;

    return extent;
}

static VkFormat find_depth_format(VkPhysicalDevice device)
{
    // Preference order: D32_SFLOAT, D32_SFLOAT_S8, D24_UNORM_S8
    VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };

    for (uint32_t i = 0; i < 3; i++) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(device, candidates[i], &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return candidates[i];
        }
    }

    // Fallback — should never happen on any real GPU
    return VK_FORMAT_D32_SFLOAT;
}

static uint32_t find_memory_type(const VkContext *ctx, uint32_t typeFilter,
                                  VkMemoryPropertyFlags properties)
{
    for (uint32_t i = 0; i < ctx->memoryProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (ctx->memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    fprintf(stderr, "[vk_swapchain] Failed to find suitable memory type.\n");
    return 0;
}

static bool create_depth_resources(const VkContext *ctx, VkSwapchainState *state)
{
    state->depthFormat = find_depth_format(ctx->physicalDevice);

    // Create depth image
    VkImageCreateInfo imageCI = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .format        = state->depthFormat,
        .extent        = { state->extent.width, state->extent.height, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if (vkCreateImage(ctx->device, &imageCI, NULL, &state->depthImage) != VK_SUCCESS) {
        fprintf(stderr, "[vk_swapchain] Failed to create depth image.\n");
        return false;
    }

    // Allocate memory
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(ctx->device, state->depthImage, &memReqs);

    VkMemoryAllocateInfo allocInfo = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memReqs.size,
        .memoryTypeIndex = find_memory_type(ctx, memReqs.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    if (vkAllocateMemory(ctx->device, &allocInfo, NULL, &state->depthMemory) != VK_SUCCESS) {
        fprintf(stderr, "[vk_swapchain] Failed to allocate depth memory.\n");
        return false;
    }

    vkBindImageMemory(ctx->device, state->depthImage, state->depthMemory, 0);

    // Create image view
    VkImageViewCreateInfo viewCI = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = state->depthImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = state->depthFormat,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    if (vkCreateImageView(ctx->device, &viewCI, NULL, &state->depthImageView) != VK_SUCCESS) {
        fprintf(stderr, "[vk_swapchain] Failed to create depth image view.\n");
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool vk_swapchain_create(const VkContext *ctx, GLFWwindow *window,
                          VkSwapchainState *state)
{
    VkSwapchainKHR oldSwapchain = state->swapchain;

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->physicalDevice, ctx->surface, &caps);

    VkSurfaceFormatKHR surfaceFormat = choose_surface_format(ctx->physicalDevice, ctx->surface);
    VkPresentModeKHR   presentMode   = choose_present_mode(ctx->physicalDevice, ctx->surface);
    VkExtent2D         extent        = choose_extent(&caps, window);

    // Request one more than minimum for triple buffering if possible
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = ctx->surface,
        .minImageCount    = imageCount,
        .imageFormat      = surfaceFormat.format,
        .imageColorSpace  = surfaceFormat.colorSpace,
        .imageExtent      = extent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform     = caps.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = presentMode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = oldSwapchain,
    };

    // Handle different queue families for graphics/present
    uint32_t queueFamilies[] = { ctx->graphicsFamily, ctx->presentFamily };
    if (ctx->graphicsFamily != ctx->presentFamily) {
        createInfo.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices   = queueFamilies;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VkResult result = vkCreateSwapchainKHR(ctx->device, &createInfo, NULL, &state->swapchain);
    if (result != VK_SUCCESS) {
        fprintf(stderr, "[vk_swapchain] vkCreateSwapchainKHR failed: %d\n", result);
        return false;
    }

    // Destroy old swapchain after new one is created
    if (oldSwapchain != VK_NULL_HANDLE) {
        // Destroy old image views
        if (state->imageViews) {
            for (uint32_t i = 0; i < state->imageCount; i++) {
                vkDestroyImageView(ctx->device, state->imageViews[i], NULL);
            }
            free(state->imageViews);
        }
        free(state->images);

        // Destroy old depth resources
        if (state->depthImageView != VK_NULL_HANDLE)
            vkDestroyImageView(ctx->device, state->depthImageView, NULL);
        if (state->depthImage != VK_NULL_HANDLE)
            vkDestroyImage(ctx->device, state->depthImage, NULL);
        if (state->depthMemory != VK_NULL_HANDLE)
            vkFreeMemory(ctx->device, state->depthMemory, NULL);

        vkDestroySwapchainKHR(ctx->device, oldSwapchain, NULL);
    }

    state->imageFormat = surfaceFormat.format;
    state->extent = extent;

    // Retrieve swapchain images
    vkGetSwapchainImagesKHR(ctx->device, state->swapchain, &state->imageCount, NULL);
    state->images = malloc(state->imageCount * sizeof(VkImage));
    vkGetSwapchainImagesKHR(ctx->device, state->swapchain, &state->imageCount, state->images);

    // Create image views
    state->imageViews = malloc(state->imageCount * sizeof(VkImageView));
    for (uint32_t i = 0; i < state->imageCount; i++) {
        VkImageViewCreateInfo viewCI = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = state->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = state->imageFormat,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };

        if (vkCreateImageView(ctx->device, &viewCI, NULL, &state->imageViews[i]) != VK_SUCCESS) {
            fprintf(stderr, "[vk_swapchain] Failed to create image view %u.\n", i);
            return false;
        }
    }

    // Create depth buffer
    state->depthImage     = VK_NULL_HANDLE;
    state->depthMemory    = VK_NULL_HANDLE;
    state->depthImageView = VK_NULL_HANDLE;
    if (!create_depth_resources(ctx, state)) {
        return false;
    }

    fprintf(stderr, "[vk_swapchain] Created %ux%u swapchain (%u images)\n",
            extent.width, extent.height, state->imageCount);

    return true;
}

void vk_swapchain_destroy(const VkContext *ctx, VkSwapchainState *state)
{
    if (ctx->device == VK_NULL_HANDLE) return;

    if (state->depthImageView != VK_NULL_HANDLE)
        vkDestroyImageView(ctx->device, state->depthImageView, NULL);
    if (state->depthImage != VK_NULL_HANDLE)
        vkDestroyImage(ctx->device, state->depthImage, NULL);
    if (state->depthMemory != VK_NULL_HANDLE)
        vkFreeMemory(ctx->device, state->depthMemory, NULL);

    if (state->imageViews) {
        for (uint32_t i = 0; i < state->imageCount; i++) {
            vkDestroyImageView(ctx->device, state->imageViews[i], NULL);
        }
        free(state->imageViews);
    }

    free(state->images);

    if (state->swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(ctx->device, state->swapchain, NULL);

    memset(state, 0, sizeof(*state));
}
