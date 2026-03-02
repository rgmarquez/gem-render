/**
 * camera.h
 *
 * Orbit camera with damping, auto-rotate, and mouse/scroll control.
 * Port of Three.js OrbitControls behavior.
 */

#ifndef CAMERA_H
#define CAMERA_H

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// Camera state
// ---------------------------------------------------------------------------
typedef struct Camera {
    // Spherical coordinates (centered on target)
    float theta;            // azimuth angle (radians, around Y)
    float phi;              // elevation angle (radians, from XZ plane)
    float radius;           // distance from target

    // Orbit constraints
    float minRadius;
    float maxRadius;
    float minPhi;           // radians (-89 deg)
    float maxPhi;           // radians (+89 deg)

    // Target point (gem center)
    vec3  target;

    // Damping
    float dampingFactor;    // 0 = no damping, 1 = instant
    float thetaVelocity;
    float phiVelocity;
    float radiusVelocity;

    // Auto-rotate
    bool  autoRotate;
    float autoRotateSpeed;  // radians per second

    // Projection
    float fovDeg;
    float nearPlane;
    float farPlane;

    // Output matrices (updated each frame)
    mat4  view;
    mat4  projection;
    vec3  position;         // world-space camera position

    // Input tracking
    bool  isDragging;
    float lastMouseX;
    float lastMouseY;
    float mouseSensitivity;
    float scrollSensitivity;
} Camera;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/**
 * Initialise camera with defaults matching the web version.
 * Target = (0, 0.3, 0), FOV 40, auto-rotate 1.5 deg/s.
 */
void camera_init(Camera *cam);

/**
 * Update camera: apply damping/auto-rotate, recompute view + projection.
 *
 * @param cam    Camera state
 * @param dt     Delta time in seconds
 * @param aspect Viewport aspect ratio (width / height)
 */
void camera_update(Camera *cam, float dt, float aspect);

// ---------------------------------------------------------------------------
// GLFW input callbacks (set these as GLFW callbacks)
// ---------------------------------------------------------------------------

void camera_on_mouse_button(Camera *cam, int button, int action, int mods);
void camera_on_cursor_pos(Camera *cam, double xpos, double ypos);
void camera_on_scroll(Camera *cam, double xoffset, double yoffset);

#endif /* CAMERA_H */
