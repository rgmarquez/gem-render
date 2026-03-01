/**
 * gem-material.js
 *
 * Creates a physically-based gemstone material using THREE.MeshPhysicalMaterial
 * and provides presets for common gemstones.
 *
 * Each preset defines the optical properties that differentiate one stone
 * from another: IOR, dispersion, body color (attenuation), thickness, etc.
 * All presets share the same rendering flags (transmission, double-sided,
 * clearcoat) since the cut geometry is identical.
 */

import * as THREE from "three";

// ---------------------------------------------------------------
// Gemstone presets
//
// Each entry carries only the properties that vary per stone.
// Properties not listed here are shared defaults set in createGemMaterial().
// ---------------------------------------------------------------

/** @type {Object.<string, {label: string, props: Object}>} */
export const GEM_PRESETS = {
    diamond: {
        label: "Diamond",
        props: {
            ior: 2.42,
            dispersion: 0.044,
            thickness: 2.0,
            attenuationColor: new THREE.Color(0xffffff),
            attenuationDistance: 5.0,
            specularIntensity: 1.0,
            envMapIntensity: 2.5,
        },
    },
    sapphire: {
        label: "Blue Sapphire",
        props: {
            ior: 1.77,
            dispersion: 0.018,
            thickness: 2.5,
            attenuationColor: new THREE.Color(0.15, 0.18, 0.85),
            attenuationDistance: 2.0,
            specularIntensity: 0.9,
            envMapIntensity: 2.0,
        },
    },
    emerald: {
        label: "Green Emerald",
        props: {
            ior: 1.57,
            dispersion: 0.014,
            thickness: 2.2,
            attenuationColor: new THREE.Color(0.12, 0.72, 0.22),
            attenuationDistance: 1.8,
            specularIntensity: 0.85,
            envMapIntensity: 1.8,
        },
    },
    citrine: {
        label: "Amber Citrine",
        props: {
            ior: 1.55,
            dispersion: 0.013,
            thickness: 2.0,
            attenuationColor: new THREE.Color(0.92, 0.62, 0.10),
            attenuationDistance: 2.5,
            specularIntensity: 0.85,
            envMapIntensity: 2.0,
        },
    },
};

/** Ordered list of preset keys for cycling. */
export const GEM_ORDER = Object.keys(GEM_PRESETS);

// ---------------------------------------------------------------
// Material factory
// ---------------------------------------------------------------

/**
 * Create a gem material initialised to a given preset.
 *
 * @param {THREE.Texture|null} envMap
 * @param {string} [presetKey="diamond"]
 * @returns {THREE.MeshPhysicalMaterial}
 */
export function createGemMaterial(envMap = null, presetKey = "diamond") {
    const preset = GEM_PRESETS[presetKey] ?? GEM_PRESETS.diamond;

    const mat = new THREE.MeshPhysicalMaterial({
        // --- Shared across all gemstones ---
        transmission: 1.0,
        roughness: 0.0,
        metalness: 0.0,
        specularColor: new THREE.Color(0xffffff),
        clearcoat: 1.0,
        clearcoatRoughness: 0.0,
        envMap: envMap,
        side: THREE.DoubleSide,
        transparent: true,
        depthWrite: true,

        // --- Per-stone defaults ---
        ...preset.props,
    });

    return mat;
}

/**
 * Apply a preset to an existing material (in-place).
 * Only overwrites the properties defined in the preset — shared flags
 * (transmission, clearcoat, etc.) are left untouched.
 *
 * @param {THREE.MeshPhysicalMaterial} material
 * @param {string} presetKey
 */
export function applyGemPreset(material, presetKey) {
    const preset = GEM_PRESETS[presetKey] ?? GEM_PRESETS.diamond;
    for (const [key, value] of Object.entries(preset.props)) {
        if (material[key] instanceof THREE.Color) {
            material[key].copy(value);
        } else {
            material[key] = value;
        }
    }
    material.needsUpdate = true;
}
