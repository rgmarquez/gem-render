/**
 * vk_pipeline.c
 *
 * Graphics pipeline for gem rendering.
 *
 * - Loads SPIR-V shader modules
 * - Creates descriptor set layout (frame UBO + material UBO)
 * - Creates pipeline layout with push constants (model matrix)
 * - Builds the graphics pipeline (vertex input, rasterizer, depth, blend)
 * - Allocates descriptor pool + sets
 */

#include "vk_pipeline.h"
#include "vk_environment.h"
#include "vk_background.h"
#include "util/file_io.h"
#include "scene/gem_geometry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Shader loading
// ---------------------------------------------------------------------------
static VkShaderModule create_shader_module(VkDevice device,
                                            const uint8_t *code,
                                            size_t codeSize)
{
    VkShaderModuleCreateInfo createInfo = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = codeSize,
        .pCode    = (const uint32_t *)code,
    };

    VkShaderModule module;
    if (vkCreateShaderModule(device, &createInfo, NULL, &module) != VK_SUCCESS) {
        fprintf(stderr, "[vk_pipeline] Failed to create shader module.\n");
        return VK_NULL_HANDLE;
    }

    return module;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool vk_pipeline_create(const VkContext *ctx,
                          VkRenderPass renderPass,
                          VkExtent2D extent,
                          const GvBuffer frameUBOs[GV_MAX_FRAMES_IN_FLIGHT],
                          const GvBuffer matUBOs[GV_MAX_FRAMES_IN_FLIGHT],
                          const GvEnvMap *envMap,
                          const GvBackground *background,
                          VkPipelineState *state)
{
    memset(state, 0, sizeof(*state));

    // -- Load shader SPIR-V -----------------------------------------------
    size_t vertSize = 0, fragSize = 0;
    uint8_t *vertCode = file_io_read_binary(GV_SHADER_DIR "/gem.vert.spv", &vertSize);
    uint8_t *fragCode = file_io_read_binary(GV_SHADER_DIR "/gem.frag.spv", &fragSize);

    if (!vertCode || !fragCode) {
        fprintf(stderr, "[vk_pipeline] Failed to load shader SPIR-V files.\n");
        free(vertCode);
        free(fragCode);
        return false;
    }

    state->vertModule = create_shader_module(ctx->device, vertCode, vertSize);
    state->fragModule = create_shader_module(ctx->device, fragCode, fragSize);
    free(vertCode);
    free(fragCode);

    if (state->vertModule == VK_NULL_HANDLE || state->fragModule == VK_NULL_HANDLE) {
        return false;
    }

    // -- Shader stages ----------------------------------------------------
    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = state->vertModule,
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = state->fragModule,
            .pName  = "main",
        },
    };

    // -- Vertex input (GemVertex: position vec3 + normal vec3) ------------
    VkVertexInputBindingDescription bindingDesc = {
        .binding   = 0,
        .stride    = sizeof(GemVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attrDescs[2] = {
        {
            .binding  = 0,
            .location = 0,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = offsetof(GemVertex, position),
        },
        {
            .binding  = 0,
            .location = 1,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = offsetof(GemVertex, normal),
        },
    };

    VkPipelineVertexInputStateCreateInfo vertexInputCI = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &bindingDesc,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions    = attrDescs,
    };

    // -- Input assembly ---------------------------------------------------
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    // -- Dynamic viewport / scissor (set during command buffer recording) --
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamicStateCI = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates    = dynamicStates,
    };

    VkPipelineViewportStateCreateInfo viewportCI = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1,
    };

    // -- Rasterizer (common base) ----------------------------------------
    // Two instances differing only in cullMode:
    //   rasterBackFace  — cull front faces → renders gem interior (back faces)
    //   rasterFrontFace — cull back  faces → renders gem surface (front faces)
    VkPipelineRasterizationStateCreateInfo rasterBase = {
        .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable        = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode             = VK_POLYGON_MODE_FILL,
        .lineWidth               = 1.0f,
        .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable         = VK_FALSE,
    };

    VkPipelineRasterizationStateCreateInfo rasterBackFace  = rasterBase;
    rasterBackFace.cullMode  = VK_CULL_MODE_FRONT_BIT;   // renders back faces of gem

    VkPipelineRasterizationStateCreateInfo rasterFrontFace = rasterBase;
    rasterFrontFace.cullMode = VK_CULL_MODE_BACK_BIT;    // renders front faces of gem

    // -- Multisampling (no MSAA for now) ----------------------------------
    VkPipelineMultisampleStateCreateInfo msCI = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable  = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    // -- Depth / stencil --------------------------------------------------
    // Back-face pass: depth TEST on (reject back faces hidden behind opaque geometry)
    //                 depth WRITE off — back-face depth must NOT occlude front faces.
    //                 A brilliant cut is non-convex: pavilion back faces are closer
    //                 to the camera than crown front faces, so writing here would
    //                 cause front-face crown/pavilion-tip fragments to fail LESS.
    VkPipelineDepthStencilStateCreateInfo depthCIBackFace = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = VK_TRUE,
        .depthWriteEnable      = VK_FALSE,
        .depthCompareOp        = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
    };

    // Front-face pass: full depth test + write — writes the authoritative surface depth.
    VkPipelineDepthStencilStateCreateInfo depthCIFrontFace = {
        .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable       = VK_TRUE,
        .depthWriteEnable      = VK_TRUE,
        .depthCompareOp        = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
    };

    // -- Color blending (alpha blending for transparent gem) --------------
    // Standard SRC_ALPHA / ONE_MINUS_SRC_ALPHA blending.
    // Both passes (back-face and front-face) use the same blend state.
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable         = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
    };

    VkPipelineColorBlendStateCreateInfo colorBlendCI = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable   = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments    = &colorBlendAttachment,
    };

    // -- Descriptor set layout (set 0) ------------------------------------
    // Binding 0: FrameUBO      (vertex + fragment)
    // Binding 1: MaterialUBO   (fragment)
    // Binding 2: envMap        (fragment, combined image sampler — cube map)
    // Binding 3: backgroundMap (fragment, combined image sampler — 2D)
    VkDescriptorSetLayoutBinding bindings[4] = {
        {
            .binding            = 0,
            .descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding            = 1,
            .descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding            = 2,
            .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
        {
            .binding            = 3,
            .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount    = 1,
            .stageFlags         = VK_SHADER_STAGE_FRAGMENT_BIT,
        },
    };

    VkDescriptorSetLayoutCreateInfo descLayoutCI = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 4,
        .pBindings    = bindings,
    };

    if (vkCreateDescriptorSetLayout(ctx->device, &descLayoutCI, NULL,
                                     &state->descriptorSetLayout) != VK_SUCCESS) {
        fprintf(stderr, "[vk_pipeline] Failed to create descriptor set layout.\n");
        return false;
    }

    // -- Push constant range (model matrix = mat4 = 64 bytes) -------------
    VkPushConstantRange pushConstant = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset     = 0,
        .size       = sizeof(float) * 16,  // mat4
    };

    // -- Pipeline layout --------------------------------------------------
    VkPipelineLayoutCreateInfo layoutCI = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &state->descriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &pushConstant,
    };

    if (vkCreatePipelineLayout(ctx->device, &layoutCI, NULL,
                                &state->pipelineLayout) != VK_SUCCESS) {
        fprintf(stderr, "[vk_pipeline] Failed to create pipeline layout.\n");
        return false;
    }

    // -- Graphics pipeline (two passes: back faces, then front faces) ------
    // Both share all state except the rasterizer cull mode.
    VkGraphicsPipelineCreateInfo pipelineCIs[2];

    VkGraphicsPipelineCreateInfo pipelineBase = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = 2,
        .pStages             = shaderStages,
        .pVertexInputState   = &vertexInputCI,
        .pInputAssemblyState = &inputAssemblyCI,
        .pViewportState      = &viewportCI,
        .pMultisampleState   = &msCI,
        .pDepthStencilState  = NULL,   // overridden per-pipeline below
        .pColorBlendState    = &colorBlendCI,
        .pDynamicState       = &dynamicStateCI,
        .layout              = state->pipelineLayout,
        .renderPass          = renderPass,
        .subpass             = 0,
    };

    // Pass 1: back-face pipeline (cull front faces, draws interior)
    pipelineCIs[0]                    = pipelineBase;
    pipelineCIs[0].pRasterizationState = &rasterBackFace;
    pipelineCIs[0].pDepthStencilState = &depthCIBackFace;   // no depth writes

    // Pass 2: front-face pipeline (cull back faces, draws surface)
    pipelineCIs[1]                    = pipelineBase;
    pipelineCIs[1].pRasterizationState = &rasterFrontFace;
    pipelineCIs[1].pDepthStencilState = &depthCIFrontFace;  // writes surface depth

    VkPipeline pipelines[2];
    if (vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 2,
                                   pipelineCIs, NULL, pipelines) != VK_SUCCESS) {
        fprintf(stderr, "[vk_pipeline] Failed to create graphics pipelines.\n");
        return false;
    }
    state->pipelineBackFace  = pipelines[0];
    state->pipelineFrontFace = pipelines[1];

    // -- Descriptor pool --------------------------------------------------
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         GV_MAX_FRAMES_IN_FLIGHT * 2 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, GV_MAX_FRAMES_IN_FLIGHT * 2 }, // envMap + backgroundMap
    };

    VkDescriptorPoolCreateInfo poolCI = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = 2,
        .pPoolSizes    = poolSizes,
        .maxSets       = GV_MAX_FRAMES_IN_FLIGHT,
    };

    if (vkCreateDescriptorPool(ctx->device, &poolCI, NULL,
                                &state->descriptorPool) != VK_SUCCESS) {
        fprintf(stderr, "[vk_pipeline] Failed to create descriptor pool.\n");
        return false;
    }

    // -- Descriptor sets (allocate + write) --------------------------------
    VkDescriptorSetLayout layouts[GV_MAX_FRAMES_IN_FLIGHT];
    for (uint32_t i = 0; i < GV_MAX_FRAMES_IN_FLIGHT; i++) {
        layouts[i] = state->descriptorSetLayout;
    }

    VkDescriptorSetAllocateInfo descAllocInfo = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = state->descriptorPool,
        .descriptorSetCount = GV_MAX_FRAMES_IN_FLIGHT,
        .pSetLayouts        = layouts,
    };

    if (vkAllocateDescriptorSets(ctx->device, &descAllocInfo,
                                  state->descriptorSets) != VK_SUCCESS) {
        fprintf(stderr, "[vk_pipeline] Failed to allocate descriptor sets.\n");
        return false;
    }

    // Write UBO + env map + background bindings to each descriptor set
    for (uint32_t i = 0; i < GV_MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo frameBufferInfo = {
            .buffer = frameUBOs[i].buffer,
            .offset = 0,
            .range  = frameUBOs[i].size,
        };

        VkDescriptorBufferInfo matBufferInfo = {
            .buffer = matUBOs[i].buffer,
            .offset = 0,
            .range  = matUBOs[i].size,
        };

        VkDescriptorImageInfo envImageInfo = {
            .sampler     = envMap->sampler,
            .imageView   = envMap->view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        VkDescriptorImageInfo bgImageInfo = {
            .sampler     = background->sampler,
            .imageView   = background->views[i],
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        VkWriteDescriptorSet writes[4] = {
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = state->descriptorSets[i],
                .dstBinding      = 0,
                .dstArrayElement = 0,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .pBufferInfo     = &frameBufferInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = state->descriptorSets[i],
                .dstBinding      = 1,
                .dstArrayElement = 0,
                .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .pBufferInfo     = &matBufferInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = state->descriptorSets[i],
                .dstBinding      = 2,
                .dstArrayElement = 0,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .pImageInfo      = &envImageInfo,
            },
            {
                .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet          = state->descriptorSets[i],
                .dstBinding      = 3,
                .dstArrayElement = 0,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .pImageInfo      = &bgImageInfo,
            },
        };

        vkUpdateDescriptorSets(ctx->device, 4, writes, 0, NULL);
    }

    fprintf(stderr, "[vk_pipeline] Graphics pipeline created successfully.\n");
    return true;
}

void vk_pipeline_update_background(const VkContext    *ctx,
                                    VkPipelineState    *state,
                                    const GvBackground *background)
{
    for (uint32_t i = 0; i < GV_MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo bgInfo = {
            .sampler     = background->sampler,
            .imageView   = background->views[i],
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = state->descriptorSets[i],
            .dstBinding      = 3,
            .dstArrayElement = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .pImageInfo      = &bgInfo,
        };
        vkUpdateDescriptorSets(ctx->device, 1, &write, 0, NULL);
    }
}

void vk_pipeline_destroy(const VkContext *ctx, VkPipelineState *state)
{
    if (ctx->device == VK_NULL_HANDLE) return;

    if (state->pipelineFrontFace != VK_NULL_HANDLE)
        vkDestroyPipeline(ctx->device, state->pipelineFrontFace, NULL);
    if (state->pipelineBackFace != VK_NULL_HANDLE)
        vkDestroyPipeline(ctx->device, state->pipelineBackFace, NULL);
    if (state->pipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(ctx->device, state->pipelineLayout, NULL);
    if (state->descriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(ctx->device, state->descriptorPool, NULL);
    if (state->descriptorSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(ctx->device, state->descriptorSetLayout, NULL);
    if (state->vertModule != VK_NULL_HANDLE)
        vkDestroyShaderModule(ctx->device, state->vertModule, NULL);
    if (state->fragModule != VK_NULL_HANDLE)
        vkDestroyShaderModule(ctx->device, state->fragModule, NULL);

    memset(state, 0, sizeof(*state));
}
