/**
 * main.c
 *
 * Gemstone Viewer — Vulkan application entry point.
 *
 * Phase 1-3: spinning gem with basic Blinn-Phong shading on macOS (MoltenVK).
 *
 * Lifecycle:
 *   1. Create GLFW window
 *   2. Bootstrap Vulkan (vk_context_create)
 *   3. Create swapchain + render pass + framebuffers
 *   4. Create command pool + sync primitives
 *   5. Load gem geometry + upload vertex buffer
 *   6. Create uniform buffers (frame + material)
 *   7. Create graphics pipeline (descriptor sets)
 *   8. Enter frame loop
 *   9. Clean up in reverse order
 */

#include "renderer/vk_init.h"
#include "renderer/vk_swapchain.h"
#include "renderer/vk_renderpass.h"
#include "renderer/vk_command.h"
#include "renderer/vk_buffer.h"
#include "renderer/vk_pipeline.h"
#include "renderer/vk_environment.h"
#include "renderer/vk_background.h"
#include "scene/camera.h"
#include "scene/gem_geometry.h"
#include "scene/gem_material.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Per-frame UBO (matches gem.vert binding 0)
// ---------------------------------------------------------------------------
typedef struct FrameUBO {
    mat4 view;
    mat4 projection;
    float cameraPos[4];  // vec3 + padding
} FrameUBO;

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------
typedef struct App {
    GLFWwindow         *window;

    VkContext           vkCtx;
    VkSwapchainState    swapchain;
    VkRenderPassState   renderPassState;
    VkFrameSync         frameSync;
    VkPipelineState     pipeline;

    GvBuffer            vertexBuffer;
    GvBuffer            frameUBOs[GV_MAX_FRAMES_IN_FLIGHT];
    GvBuffer            matUBOs[GV_MAX_FRAMES_IN_FLIGHT];

    GvEnvMap            envMap;
    GvBackground        background;    // off-screen background for screen-space transmission

    Camera              camera;
    GemPresetKey        currentPreset;
    GemMaterial         currentMaterial;

    float               gemRotationY;   // accumulated idle rotation (radians)
    bool                framebufferResized;
    bool                running;
} App;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
static bool app_init(App *app);
static void app_run(App *app);
static void app_cleanup(App *app);
static bool app_recreate_swapchain(App *app);
static void app_switch_gem(App *app, GemPresetKey key);
static void app_draw_frame(App *app, double dt);
static void app_record_command_buffer(App *app, VkCommandBuffer cmd, uint32_t imageIndex);

// ---------------------------------------------------------------------------
// GLFW callback wrappers
// ---------------------------------------------------------------------------
static App *g_app = NULL;

static void cb_framebuffer_resize(GLFWwindow *wn, int w, int h)
{
    (void)wn; (void)w; (void)h;
    if (g_app) g_app->framebufferResized = true;
}

static void cb_mouse_button(GLFWwindow *wn, int button, int action, int mods)
{
    (void)wn;
    if (g_app) camera_on_mouse_button(&g_app->camera, button, action, mods);
}

static void cb_cursor_pos(GLFWwindow *wn, double x, double y)
{
    (void)wn;
    if (g_app) camera_on_cursor_pos(&g_app->camera, x, y);
}

static void cb_scroll(GLFWwindow *wn, double x, double y)
{
    (void)wn;
    if (g_app) camera_on_scroll(&g_app->camera, x, y);
}

static void cb_key(GLFWwindow *wn, int key, int scancode, int action, int mods)
{
    (void)wn; (void)scancode; (void)mods;
    if (!g_app) return;
    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_ESCAPE) {
        g_app->running = false;
    } else if (key == GLFW_KEY_LEFT) {
        app_switch_gem(g_app, gem_preset_prev(g_app->currentPreset));
    } else if (key == GLFW_KEY_RIGHT) {
        app_switch_gem(g_app, gem_preset_next(g_app->currentPreset));
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(void)
{
    App app;
    memset(&app, 0, sizeof(app));
    g_app = &app;

    if (!app_init(&app)) {
        fprintf(stderr, "Application init failed.\n");
        app_cleanup(&app);
        return 1;
    }

    app_run(&app);
    app_cleanup(&app);
    return 0;
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

static bool app_init(App *app)
{
    // -- GLFW setup --------------------------------------------------------
    if (!glfwInit()) {
        fprintf(stderr, "glfwInit failed.\n");
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // Vulkan — no OpenGL
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    app->window = glfwCreateWindow(1280, 720, "Gemstone Viewer", NULL, NULL);
    if (!app->window) {
        fprintf(stderr, "glfwCreateWindow failed.\n");
        return false;
    }

    glfwSetWindowUserPointer(app->window, app);
    glfwSetFramebufferSizeCallback(app->window, cb_framebuffer_resize);
    glfwSetMouseButtonCallback(app->window, cb_mouse_button);
    glfwSetCursorPosCallback(app->window, cb_cursor_pos);
    glfwSetScrollCallback(app->window, cb_scroll);
    glfwSetKeyCallback(app->window, cb_key);

    // -- Vulkan bootstrap -------------------------------------------------
    if (!vk_context_create(app->window, &app->vkCtx)) return false;

    // -- Swapchain --------------------------------------------------------
    if (!vk_swapchain_create(&app->vkCtx, app->window, &app->swapchain)) return false;

    // -- Render pass + framebuffers ---------------------------------------
    if (!vk_renderpass_create(&app->vkCtx, &app->swapchain, &app->renderPassState)) return false;

    // -- Command pool + sync ----------------------------------------------
    if (!vk_framesync_create(&app->vkCtx, app->swapchain.imageCount,
                             &app->frameSync)) return false;

    // -- Procedural environment cube map ----------------------------------
    if (!gv_environment_create(&app->vkCtx, app->frameSync.commandPool,
                                &app->envMap)) return false;

    // -- Background render target (screen-space transmission) -------------
    if (!gv_background_create(&app->vkCtx, &app->swapchain,
                               &app->background)) return false;

    // -- Gem geometry (load + build + upload) -----------------------------
    GemCutParams cutParams = gem_cut_params_from_json(GV_DATA_DIR "/brilliant-cut.json");
    GemGeometry geo;
    if (!gem_geometry_build(&cutParams, 1.2f, &geo)) return false;

    bool vertexOk = gv_buffer_create_vertex(
        &app->vkCtx, app->frameSync.commandPool,
        geo.vertices, geo.vertexCount * sizeof(GemVertex),
        &app->vertexBuffer
    );
    gem_geometry_destroy(&geo);
    if (!vertexOk) return false;

    // -- Uniform buffers --------------------------------------------------
    for (uint32_t i = 0; i < GV_MAX_FRAMES_IN_FLIGHT; i++) {
        if (!gv_buffer_create_uniform(&app->vkCtx, sizeof(FrameUBO), &app->frameUBOs[i]))
            return false;
        if (!gv_buffer_create_uniform(&app->vkCtx, sizeof(GemMaterialUBO), &app->matUBOs[i]))
            return false;
    }

    // -- Graphics pipeline ------------------------------------------------
    if (!vk_pipeline_create(&app->vkCtx,
                              app->renderPassState.renderPass,
                              app->swapchain.extent,
                              app->frameUBOs,
                              app->matUBOs,
                              &app->envMap,
                              &app->background,
                              &app->pipeline)) return false;

    // -- Camera -----------------------------------------------------------
    camera_init(&app->camera);

    // -- Initial gem preset -----------------------------------------------
    app->currentPreset = GEM_DIAMOND;
    app->currentMaterial = gem_preset_material(GEM_DIAMOND);

    // Upload initial material UBO to all frames
    GemMaterialUBO matUBO = gem_material_to_ubo(&app->currentMaterial);
    for (uint32_t i = 0; i < GV_MAX_FRAMES_IN_FLIGHT; i++) {
        gv_buffer_update(&app->matUBOs[i], &matUBO, sizeof(matUBO));
    }

    app->running = true;
    fprintf(stderr, "Initialized: %s\n", gem_preset_label(app->currentPreset));
    return true;
}

// ---------------------------------------------------------------------------
// Frame loop
// ---------------------------------------------------------------------------

static void app_run(App *app)
{
    double prevTime = glfwGetTime();

    while (app->running && !glfwWindowShouldClose(app->window)) {
        double now = glfwGetTime();
        double dt  = now - prevTime;
        prevTime   = now;

        glfwPollEvents();
        app_draw_frame(app, dt);
    }

    // Wait for all in-flight frames before cleanup
    vk_device_wait_idle(&app->vkCtx);
}

// ---------------------------------------------------------------------------
// Gem switching
// ---------------------------------------------------------------------------

static void app_switch_gem(App *app, GemPresetKey key)
{
    app->currentPreset   = key;
    app->currentMaterial = gem_preset_material(key);
    fprintf(stderr, "Gem: %s\n", gem_preset_label(key));

    // Update all in-flight material UBOs
    GemMaterialUBO ubo = gem_material_to_ubo(&app->currentMaterial);
    for (uint32_t i = 0; i < GV_MAX_FRAMES_IN_FLIGHT; i++) {
        gv_buffer_update(&app->matUBOs[i], &ubo, sizeof(ubo));
    }
}

// ---------------------------------------------------------------------------
// Swapchain recreation (on resize or VK_ERROR_OUT_OF_DATE_KHR)
// ---------------------------------------------------------------------------

static bool app_recreate_swapchain(App *app)
{
    // Handle minimized window: wait until non-zero size
    int width = 0, height = 0;
    glfwGetFramebufferSize(app->window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(app->window, &width, &height);
        glfwWaitEvents();
    }

    vk_device_wait_idle(&app->vkCtx);

    vk_renderpass_destroy(&app->vkCtx, &app->renderPassState);
    vk_swapchain_create(&app->vkCtx, app->window, &app->swapchain);
    vk_renderpass_create(&app->vkCtx, &app->swapchain, &app->renderPassState);

    // Resize background targets and re-point descriptor binding 3.
    gv_background_resize(&app->vkCtx, &app->swapchain, &app->background);
    vk_pipeline_update_background(&app->vkCtx, &app->pipeline, &app->background);

    // Reset image-in-flight tracking (all images are idle after device wait)
    for (uint32_t i = 0; i < GV_MAX_SWAPCHAIN_IMAGES; i++) {
        app->frameSync.imagesInFlight[i] = VK_NULL_HANDLE;
    }

    // Recreate renderFinished semaphores if swapchain image count changed
    uint32_t newCount = app->swapchain.imageCount;
    if (newCount != app->frameSync.renderFinishedCount) {
        VkSemaphoreCreateInfo semCI = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };
        // Destroy old
        for (uint32_t i = 0; i < app->frameSync.renderFinishedCount; i++) {
            if (app->frameSync.renderFinished[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(app->vkCtx.device, app->frameSync.renderFinished[i], NULL);
            app->frameSync.renderFinished[i] = VK_NULL_HANDLE;
        }
        // Create new
        for (uint32_t i = 0; i < newCount; i++) {
            vkCreateSemaphore(app->vkCtx.device, &semCI, NULL,
                              &app->frameSync.renderFinished[i]);
        }
        app->frameSync.renderFinishedCount = newCount;
    }

    app->framebufferResized = false;
    return true;
}

// ---------------------------------------------------------------------------
// Per-frame draw
// ---------------------------------------------------------------------------

static void app_draw_frame(App *app, double dt)
{
    uint32_t frameIdx = app->frameSync.currentFrame;

    // Wait for this frame's fence (GPU finished with frame N-MAX)
    vkWaitForFences(app->vkCtx.device, 1,
                    &app->frameSync.inFlightFences[frameIdx],
                    VK_TRUE, UINT64_MAX);

    // Acquire next swapchain image
    uint32_t imageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(
        app->vkCtx.device,
        app->swapchain.swapchain,
        UINT64_MAX,
        app->frameSync.imageAvailable[frameIdx],
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        app_recreate_swapchain(app);
        return;
    } else if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        fprintf(stderr, "[main] vkAcquireNextImageKHR failed: %d\n", acquireResult);
        return;
    }

    // If this swapchain image is still in flight from a previous frame,
    // wait for that submission to complete before reusing.
    if (app->frameSync.imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(app->vkCtx.device, 1,
                        &app->frameSync.imagesInFlight[imageIndex],
                        VK_TRUE, UINT64_MAX);
    }
    // Associate this image with the current frame's fence
    app->frameSync.imagesInFlight[imageIndex] = app->frameSync.inFlightFences[frameIdx];

    // Reset fence for this frame
    vkResetFences(app->vkCtx.device, 1, &app->frameSync.inFlightFences[frameIdx]);

    // -- Update camera + UBOs -----------------------------------------------
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(app->window, &fbWidth, &fbHeight);
    float aspect = (fbHeight > 0) ? (float)fbWidth / (float)fbHeight : 1.0f;

    camera_update(&app->camera, (float)dt, aspect);

    // Idle rotation of the gem itself
    app->gemRotationY += 0.003f;  // rad/frame ~@ 60fps

    // Build frame UBO
    FrameUBO frameUBO;
    glm_mat4_copy(app->camera.view, frameUBO.view);
    glm_mat4_copy(app->camera.projection, frameUBO.projection);
    frameUBO.cameraPos[0] = app->camera.position[0];
    frameUBO.cameraPos[1] = app->camera.position[1];
    frameUBO.cameraPos[2] = app->camera.position[2];
    frameUBO.cameraPos[3] = 0.0f;

    gv_buffer_update(&app->frameUBOs[frameIdx], &frameUBO, sizeof(frameUBO));

    // -- Record command buffer ----------------------------------------------
    VkCommandBuffer cmd = app->frameSync.commandBuffers[frameIdx];
    vkResetCommandBuffer(cmd, 0);
    app_record_command_buffer(app, cmd, imageIndex);

    // -- Submit ------------------------------------------------------------
    VkSemaphore          waitSems[]   = { app->frameSync.imageAvailable[frameIdx] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore          signalSems[] = { app->frameSync.renderFinished[imageIndex] };

    VkSubmitInfo submitInfo = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = waitSems,
        .pWaitDstStageMask    = waitStages,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = signalSems,
    };

    if (vkQueueSubmit(app->vkCtx.graphicsQueue, 1, &submitInfo,
                       app->frameSync.inFlightFences[frameIdx]) != VK_SUCCESS) {
        fprintf(stderr, "[main] vkQueueSubmit failed.\n");
        return;
    }

    // -- Present -----------------------------------------------------------
    VkPresentInfoKHR presentInfo = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = signalSems,
        .swapchainCount     = 1,
        .pSwapchains        = &app->swapchain.swapchain,
        .pImageIndices      = &imageIndex,
    };

    VkResult presentResult = vkQueuePresentKHR(app->vkCtx.presentQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR ||
        app->framebufferResized) {
        app_recreate_swapchain(app);
    } else if (presentResult != VK_SUCCESS) {
        fprintf(stderr, "[main] vkQueuePresentKHR failed: %d\n", presentResult);
    }

    app->frameSync.currentFrame = (frameIdx + 1) % GV_MAX_FRAMES_IN_FLIGHT;
}

// ---------------------------------------------------------------------------
// Command buffer recording
// ---------------------------------------------------------------------------

static void app_record_command_buffer(App *app, VkCommandBuffer cmd, uint32_t imageIndex)
{
    uint32_t frameIdx = app->frameSync.currentFrame;

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    vkBeginCommandBuffer(cmd, &beginInfo);

    // -----------------------------------------------------------------------
    // Background pass: capture the scene behind the gem.
    //
    // Clears to transparent black (alpha = 0).  When scene objects are added
    // they are rendered here with alpha = 1 so the gem shader knows to use
    // the screen-space sample rather than falling back to env-based refraction.
    //
    // The background render pass's subpass dependency handles the memory
    // ordering between this colour write and the gem shader's texture read —
    // no explicit vkCmdPipelineBarrier is required.
    // -----------------------------------------------------------------------
    VkClearValue bgClear = { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } } };

    VkRenderPassBeginInfo bgBegin = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass      = app->background.renderPass,
        .framebuffer     = app->background.framebuffers[frameIdx],
        .renderArea      = { .offset = {0, 0}, .extent = app->swapchain.extent },
        .clearValueCount = 1,
        .pClearValues    = &bgClear,
    };

    vkCmdBeginRenderPass(cmd, &bgBegin, VK_SUBPASS_CONTENTS_INLINE);
    // TODO: render skybox / scene objects here when present.
    vkCmdEndRenderPass(cmd);

    // -----------------------------------------------------------------------
    // Gem render pass
    // -----------------------------------------------------------------------
    VkClearValue clearValues[2] = {
        { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } },  // black background
        { .depthStencil = { .depth = 1.0f, .stencil = 0 } },
    };

    VkRenderPassBeginInfo rpBegin = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass      = app->renderPassState.renderPass,
        .framebuffer     = app->renderPassState.framebuffers[imageIndex],
        .renderArea      = { .offset = {0, 0}, .extent = app->swapchain.extent },
        .clearValueCount = 2,
        .pClearValues    = clearValues,
    };

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // -- Dynamic state (viewport/scissor shared by both passes) -----------
    VkViewport viewport = {
        .x        = 0.0f,
        .y        = 0.0f,
        .width    = (float)app->swapchain.extent.width,
        .height   = (float)app->swapchain.extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = app->swapchain.extent,
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // -- Shared draw data -------------------------------------------------
    VkDeviceSize offsets[] = { 0 };
    uint32_t vertexCount = (uint32_t)(app->vertexBuffer.size / sizeof(GemVertex));

    mat4 model;
    glm_mat4_identity(model);
    glm_rotate(model, app->gemRotationY, (vec3){ 0.0f, 1.0f, 0.0f });
    glm_translate(model, (vec3){ 0.0f, 0.3f, 0.0f });  // lift gem to match web version

    // -- Pass 1: back faces (gem interior, semi-transparent body color) ---
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->pipeline.pipelineBackFace);

    vkCmdBindVertexBuffers(cmd, 0, 1, &app->vertexBuffer.buffer, offsets);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                             app->pipeline.pipelineLayout,
                             0, 1, &app->pipeline.descriptorSets[frameIdx],
                             0, NULL);

    vkCmdPushConstants(cmd, app->pipeline.pipelineLayout,
                        VK_SHADER_STAGE_VERTEX_BIT,
                        0, sizeof(mat4), model);

    vkCmdDraw(cmd, vertexCount, 1, 0, 0);

    // -- Pass 2: front faces (gem surface with Fresnel transparency) ------
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->pipeline.pipelineFrontFace);
    // Vertex buffer, descriptor sets, and push constants are inherited from pass 1.
    vkCmdDraw(cmd, vertexCount, 1, 0, 0);

    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        fprintf(stderr, "[main] vkEndCommandBuffer failed.\n");
    }
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

static void app_cleanup(App *app)
{
    if (app->vkCtx.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(app->vkCtx.device);

        vk_pipeline_destroy(&app->vkCtx, &app->pipeline);

        gv_background_destroy(&app->vkCtx, &app->background);
        gv_environment_destroy(&app->vkCtx, &app->envMap);

        for (uint32_t i = 0; i < GV_MAX_FRAMES_IN_FLIGHT; i++) {
            gv_buffer_destroy(&app->vkCtx, &app->matUBOs[i]);
            gv_buffer_destroy(&app->vkCtx, &app->frameUBOs[i]);
        }

        gv_buffer_destroy(&app->vkCtx, &app->vertexBuffer);
        vk_framesync_destroy(&app->vkCtx, &app->frameSync);
        vk_renderpass_destroy(&app->vkCtx, &app->renderPassState);
        vk_swapchain_destroy(&app->vkCtx, &app->swapchain);
    }

    vk_context_destroy(&app->vkCtx);

    if (app->window) {
        glfwDestroyWindow(app->window);
    }

    glfwTerminate();

    fprintf(stderr, "Cleaned up.\n");
}
