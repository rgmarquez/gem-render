#version 450

// ---------------------------------------------------------------------------
// gem.vert — Vertex shader for gemstone rendering
//
// MVP transform; passes world-space position and normal to fragment.
// ---------------------------------------------------------------------------

// Vertex input (matches GemVertex layout: position vec3 + normal vec3)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

// Per-frame uniforms (binding 0, set 0)
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
} frame;

// Push constants for per-object data
layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

// Outputs to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragWorldNormal;

void main()
{
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    // Transform normal by the inverse-transpose of model matrix.
    // For uniform scaling this is equivalent to (model * vec4(normal, 0)).xyz
    mat3 normalMatrix = transpose(inverse(mat3(push.model)));
    fragWorldNormal = normalize(normalMatrix * inNormal);

    gl_Position = frame.projection * frame.view * worldPos;
}
