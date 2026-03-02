/**
 * vk_pipeline.h
 *
 * Graphics pipeline: shader module loading, descriptor set layouts,
 * pipeline layout, and the pipeline itself.
 */

#ifndef VK_PIPELINE_H
#define VK_PIPELINE_H

#include "vk_init.h"
#include "vk_renderpass.h"
#include "vk_buffer.h"
#include "vk_command.h"
#include "vk_environment.h"
#include "vk_background.h"
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Pipeline state
// ---------------------------------------------------------------------------
typedef struct VkPipelineState {
    VkShaderModule        vertModule;
    VkShaderModule        fragModule;
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout      pipelineLayout;

    // Two-pass transparent rendering:
    //   pipelineBackFace  — cull FRONT faces  → draws interior (back faces first)
    //   pipelineFrontFace — cull BACK  faces  → draws surface  (front faces second)
    VkPipeline            pipelineBackFace;
    VkPipeline            pipelineFrontFace;

    // Descriptor pool + sets (one per frame in flight)
    VkDescriptorPool      descriptorPool;
    VkDescriptorSet       descriptorSets[GV_MAX_FRAMES_IN_FLIGHT];
} VkPipelineState;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * Create the graphics pipeline for gem rendering.
 *
 * @param ctx        Vulkan context
 * @param renderPass The render pass this pipeline will be used with
 * @param extent     Swapchain extent (for viewport/scissor defaults)
 * @param frameUBOs  Uniform buffers for per-frame data (one per frame in flight)
 * @param matUBOs    Uniform buffers for material data (one per frame in flight)
 * @param envMap     Procedural cube map for IBL and env-based refraction
 * @param background Off-screen background render target for screen-space transmission
 * @param state      Output pipeline state
 */
bool vk_pipeline_create(const VkContext *ctx,
                          VkRenderPass renderPass,
                          VkExtent2D extent,
                          const GvBuffer frameUBOs[GV_MAX_FRAMES_IN_FLIGHT],
                          const GvBuffer matUBOs[GV_MAX_FRAMES_IN_FLIGHT],
                          const GvEnvMap *envMap,
                          const GvBackground *background,
                          VkPipelineState *state);

/**
 * Update binding 3 (backgroundMap) in all descriptor sets.
 * Call after gv_background_resize() to re-point descriptors at the new views.
 */
void vk_pipeline_update_background(const VkContext    *ctx,
                                    VkPipelineState    *state,
                                    const GvBackground *background);

/**
 * Destroy the pipeline and all associated resources.
 */
void vk_pipeline_destroy(const VkContext *ctx, VkPipelineState *state);

#endif /* VK_PIPELINE_H */
