/**
 * gem_material.c
 *
 * Gemstone preset definitions matching the web version exactly.
 */

#include "gem_material.h"

#include <string.h>

// ---------------------------------------------------------------------------
// Preset data (matching gem-material.js values)
// ---------------------------------------------------------------------------

static const char *PRESET_LABELS[GEM_PRESET_COUNT] = {
    "Diamond",
    "Blue Sapphire",
    "Green Emerald",
    "Amber Citrine",
};

static const GemMaterial PRESETS[GEM_PRESET_COUNT] = {
    [GEM_DIAMOND] = {
        .ior                = 2.42f,
        .dispersion         = 0.044f,
        .thickness          = 2.0f,
        .attenuationColor   = { 1.0f, 1.0f, 1.0f },
        .attenuationDistance = 5.0f,
        .specularIntensity  = 1.0f,
        .envMapIntensity    = 2.5f,
        .clearcoat          = 1.0f,
        .clearcoatRoughness = 0.0f,
        .roughness          = 0.0f,
        .metalness          = 0.0f,
    },
    [GEM_SAPPHIRE] = {
        .ior                = 1.77f,
        .dispersion         = 0.018f,
        .thickness          = 2.5f,
        .attenuationColor   = { 0.15f, 0.18f, 0.85f },
        .attenuationDistance = 2.0f,
        .specularIntensity  = 0.9f,
        .envMapIntensity    = 2.0f,
        .clearcoat          = 1.0f,
        .clearcoatRoughness = 0.0f,
        .roughness          = 0.0f,
        .metalness          = 0.0f,
    },
    [GEM_EMERALD] = {
        .ior                = 1.57f,
        .dispersion         = 0.014f,
        .thickness          = 2.2f,
        .attenuationColor   = { 0.12f, 0.72f, 0.22f },
        .attenuationDistance = 1.8f,
        .specularIntensity  = 0.85f,
        .envMapIntensity    = 1.8f,
        .clearcoat          = 1.0f,
        .clearcoatRoughness = 0.0f,
        .roughness          = 0.0f,
        .metalness          = 0.0f,
    },
    [GEM_CITRINE] = {
        .ior                = 1.55f,
        .dispersion         = 0.013f,
        .thickness          = 2.0f,
        .attenuationColor   = { 0.92f, 0.62f, 0.10f },
        .attenuationDistance = 2.5f,
        .specularIntensity  = 0.85f,
        .envMapIntensity    = 2.0f,
        .clearcoat          = 1.0f,
        .clearcoatRoughness = 0.0f,
        .roughness          = 0.0f,
        .metalness          = 0.0f,
    },
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

const char *gem_preset_label(GemPresetKey key)
{
    if (key < 0 || key >= GEM_PRESET_COUNT) return "Unknown";
    return PRESET_LABELS[key];
}

GemMaterial gem_preset_material(GemPresetKey key)
{
    if (key < 0 || key >= GEM_PRESET_COUNT) key = GEM_DIAMOND;
    return PRESETS[key];
}

GemPresetKey gem_preset_next(GemPresetKey current)
{
    return (GemPresetKey)(((int)current + 1) % GEM_PRESET_COUNT);
}

GemPresetKey gem_preset_prev(GemPresetKey current)
{
    return (GemPresetKey)(((int)current - 1 + GEM_PRESET_COUNT) % GEM_PRESET_COUNT);
}

GemMaterialUBO gem_material_to_ubo(const GemMaterial *mat)
{
    GemMaterialUBO ubo;
    memset(&ubo, 0, sizeof(ubo));

    ubo.ior                = mat->ior;
    ubo.dispersion         = mat->dispersion;
    ubo.thickness          = mat->thickness;
    ubo.attenuationDistance = mat->attenuationDistance;

    ubo.attenuationColor[0] = mat->attenuationColor[0];
    ubo.attenuationColor[1] = mat->attenuationColor[1];
    ubo.attenuationColor[2] = mat->attenuationColor[2];
    ubo.attenuationColor[3] = 1.0f;

    ubo.specularIntensity  = mat->specularIntensity;
    ubo.envMapIntensity    = mat->envMapIntensity;
    ubo.clearcoat          = mat->clearcoat;
    ubo.clearcoatRoughness = mat->clearcoatRoughness;

    ubo.roughness = mat->roughness;
    ubo.metalness = mat->metalness;

    return ubo;
}
