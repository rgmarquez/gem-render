/**
 * gem_material.h
 *
 * Gemstone material presets and uniform data for the PBR shader.
 * Port of gem-material.js preset system.
 *
 * Phase 1-3 uses a subset (for Blinn-Phong). Full PBR in Phase 4.
 */

#ifndef GEM_MATERIAL_H
#define GEM_MATERIAL_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Material parameters (matches PBR shader uniform layout)
// ---------------------------------------------------------------------------
typedef struct GemMaterial {
    // Optical
    float ior;
    float dispersion;
    float thickness;

    // Attenuation (Beer's law body color)
    float attenuationColor[3];   // linear RGB
    float attenuationDistance;

    // Specular / reflectance
    float specularIntensity;
    float envMapIntensity;

    // Clearcoat
    float clearcoat;
    float clearcoatRoughness;

    // Base PBR
    float roughness;
    float metalness;
} GemMaterial;

// ---------------------------------------------------------------------------
// Preset system
// ---------------------------------------------------------------------------

typedef enum GemPresetKey {
    GEM_DIAMOND  = 0,
    GEM_SAPPHIRE = 1,
    GEM_EMERALD  = 2,
    GEM_CITRINE  = 3,
    GEM_PRESET_COUNT
} GemPresetKey;

/**
 * Human-readable label for a preset.
 */
const char *gem_preset_label(GemPresetKey key);

/**
 * Get the material values for a preset.
 */
GemMaterial gem_preset_material(GemPresetKey key);

/**
 * Cycle forward or backward through presets with wrapping.
 */
GemPresetKey gem_preset_next(GemPresetKey current);
GemPresetKey gem_preset_prev(GemPresetKey current);

// ---------------------------------------------------------------------------
// GPU uniform layout (std140-compatible)
// Uploaded per-frame when material properties change.
// ---------------------------------------------------------------------------
typedef struct GemMaterialUBO {
    float ior;
    float dispersion;
    float thickness;
    float attenuationDistance;

    float attenuationColor[4];  // vec4 (w unused, for alignment)

    float specularIntensity;
    float envMapIntensity;
    float clearcoat;
    float clearcoatRoughness;

    float roughness;
    float metalness;
    float _pad[2];              // align to 16 bytes
} GemMaterialUBO;

/**
 * Pack a GemMaterial into the std140-aligned UBO struct.
 */
GemMaterialUBO gem_material_to_ubo(const GemMaterial *mat);

#endif /* GEM_MATERIAL_H */
