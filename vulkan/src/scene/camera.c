/**
 * camera.c
 *
 * Orbit camera implementation matching Three.js OrbitControls behavior.
 *
 * Spherical coordinates:
 *   theta = azimuth around Y axis
 *   phi   = elevation from XZ plane
 *   radius = distance to target
 *
 * Camera position = target + spherical_to_cartesian(theta, phi, radius)
 */

#include "camera.h"

#include <math.h>

#define DEG2RAD (GLM_PIf / 180.0f)

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void camera_init(Camera *cam)
{
    // Match JS: camera.position.set(0, 1.5, 4.5), target (0, 0.3, 0)
    // Compute initial spherical coords from cartesian offset (0, 1.2, 4.5)
    float dx = 0.0f;
    float dy = 1.5f - 0.3f;  // 1.2
    float dz = 4.5f;

    cam->radius = sqrtf(dx * dx + dy * dy + dz * dz);
    cam->theta  = atan2f(dx, dz);          // ~0
    cam->phi    = asinf(dy / cam->radius);  // ~0.26 rad (~15 deg)

    cam->minRadius = 2.0f;
    cam->maxRadius = 12.0f;
    cam->minPhi    = -89.0f * DEG2RAD;
    cam->maxPhi    = 89.0f * DEG2RAD;

    glm_vec3_copy((vec3){ 0.0f, 0.3f, 0.0f }, cam->target);

    cam->dampingFactor   = 0.06f;
    cam->thetaVelocity   = 0.0f;
    cam->phiVelocity     = 0.0f;
    cam->radiusVelocity  = 0.0f;

    cam->autoRotate      = true;
    cam->autoRotateSpeed = 1.5f * DEG2RAD;  // convert deg/s to rad/s

    cam->fovDeg    = 40.0f;
    cam->nearPlane = 0.1f;
    cam->farPlane  = 100.0f;

    cam->isDragging       = false;
    cam->lastMouseX       = 0.0f;
    cam->lastMouseY       = 0.0f;
    cam->mouseSensitivity = 0.005f;
    cam->scrollSensitivity = 0.5f;

    glm_mat4_identity(cam->view);
    glm_mat4_identity(cam->projection);
    glm_vec3_zero(cam->position);
}

void camera_update(Camera *cam, float dt, float aspect)
{
    // Auto-rotate when not dragging
    if (cam->autoRotate && !cam->isDragging) {
        cam->thetaVelocity += cam->autoRotateSpeed * dt;
    }

    // Apply damping (exponential decay)
    float damping = 1.0f - cam->dampingFactor;
    cam->theta  += cam->thetaVelocity;
    cam->phi    += cam->phiVelocity;
    cam->radius += cam->radiusVelocity;

    cam->thetaVelocity  *= damping;
    cam->phiVelocity    *= damping;
    cam->radiusVelocity *= damping;

    // Kill tiny velocities to avoid drift
    if (fabsf(cam->thetaVelocity)  < 1e-6f) cam->thetaVelocity  = 0.0f;
    if (fabsf(cam->phiVelocity)    < 1e-6f) cam->phiVelocity    = 0.0f;
    if (fabsf(cam->radiusVelocity) < 1e-6f) cam->radiusVelocity = 0.0f;

    // Clamp
    if (cam->phi < cam->minPhi) cam->phi = cam->minPhi;
    if (cam->phi > cam->maxPhi) cam->phi = cam->maxPhi;
    if (cam->radius < cam->minRadius) cam->radius = cam->minRadius;
    if (cam->radius > cam->maxRadius) cam->radius = cam->maxRadius;

    // Spherical to Cartesian
    float cosPhi = cosf(cam->phi);
    cam->position[0] = cam->target[0] + cam->radius * cosPhi * sinf(cam->theta);
    cam->position[1] = cam->target[1] + cam->radius * sinf(cam->phi);
    cam->position[2] = cam->target[2] + cam->radius * cosPhi * cosf(cam->theta);

    // View matrix: look at target from camera position
    glm_lookat(cam->position, cam->target, (vec3){ 0.0f, 1.0f, 0.0f }, cam->view);

    // Projection matrix
    glm_perspective(cam->fovDeg * DEG2RAD, aspect, cam->nearPlane, cam->farPlane,
                    cam->projection);

    // Vulkan clip space: Y is flipped compared to OpenGL
    cam->projection[1][1] *= -1.0f;
}

// ---------------------------------------------------------------------------
// Input handlers
// ---------------------------------------------------------------------------

void camera_on_mouse_button(Camera *cam, int button, int action, int mods)
{
    (void)mods;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        cam->isDragging = (action == GLFW_PRESS);
    }
}

void camera_on_cursor_pos(Camera *cam, double xpos, double ypos)
{
    float x = (float)xpos;
    float y = (float)ypos;

    if (cam->isDragging) {
        float dx = x - cam->lastMouseX;
        float dy = y - cam->lastMouseY;

        cam->thetaVelocity -= dx * cam->mouseSensitivity;
        cam->phiVelocity   += dy * cam->mouseSensitivity;
    }

    cam->lastMouseX = x;
    cam->lastMouseY = y;
}

void camera_on_scroll(Camera *cam, double xoffset, double yoffset)
{
    (void)xoffset;
    cam->radiusVelocity -= (float)yoffset * cam->scrollSensitivity;
}
