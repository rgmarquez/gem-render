# Gemstone Viewer

A real-time physically-based gemstone renderer built with Three.js. Generates a parametric 57-facet round brilliant-cut gem and renders it with realistic refraction, dispersion, specular fire, and bloom — all running in the browser with zero build tools. Cycle between four gemstones (diamond, sapphire, emerald, citrine), each with physically accurate optical properties.

![Three.js](https://img.shields.io/badge/Three.js-r170-blue) ![Build](https://img.shields.io/badge/build-none-brightgreen) ![License](https://img.shields.io/badge/license-MIT-lightgrey)

## Setup

No build step, package manager, or install required. The project uses ES modules with an [import map](https://developer.mozilla.org/en-US/docs/Web/HTML/Element/script/type/importmap) that loads Three.js r170 directly from a CDN.

**Prerequisites:** A modern browser (Chrome 89+, Firefox 108+, Safari 16.4+, Edge 89+) with WebGL 2 support.

```
prototypes/
    index.html              Entry point
    data/
        brilliant-cut.json  Parametric gem proportions (Tolkowsky ideal cut)
    js/
        main.js             Renderer, scene, camera, lights, post-processing, gem cycling
        gem-geometry.js     Procedural 57-facet brilliant-cut geometry builder
        gem-material.js     Gemstone preset system and MeshPhysicalMaterial factory
        settings-ui.js      Data-driven slider panel with external sync support
```

## Running

Serve the project root over HTTP. Any static file server works:

```bash
# Python (built-in)
python3 -m http.server 8000

# Node (npx, no install)
npx serve .

# VS Code Live Server extension
# Right-click index.html → "Open with Live Server"
```

Then open `http://localhost:8000` (or whichever port your server reports).

> **Note:** Opening `index.html` directly via `file://` will fail because ES module imports and `fetch()` require an HTTP origin.

## Interaction

| Action | Input |
|--------|-------|
| Orbit | Click and drag |
| Zoom | Scroll wheel / pinch |
| Cycle gemstone | **◀ / ▶** buttons (top center) or **← →** arrow keys |
| Open settings panel | Click the **gear icon** (top-right) or press **S** |
| Close settings panel | Click the gear icon again or press **S** |
| Adjust a parameter | Drag any slider in the panel |
| Reset a group | Click the **Reset** button in the group header |

The gem auto-rotates slowly. Dragging to orbit overrides auto-rotation until the camera settles. Switching gemstones instantly updates the material and syncs all sliders to the new stone's properties.

### Gemstone presets

| Stone | IOR | Dispersion | Body Color |
|-------|-----|-----------|------------|
| Diamond | 2.42 | 0.044 | Colorless (white) |
| Blue Sapphire | 1.77 | 0.018 | Deep blue |
| Green Emerald | 1.57 | 0.014 | Green |
| Amber Citrine | 1.55 | 0.013 | Warm amber |

### Settings panel parameters

| Group | Parameter | What it controls |
|-------|-----------|-----------------|
| **Bloom** | Strength | Intensity of the glow on bright specular highlights |
| | Radius | How far the bloom bleeds outward |
| | Threshold | Minimum brightness required to trigger bloom |
| **Exposure** | Exposure | Overall scene brightness (tone mapping exposure) |
| **Reflection** | Env Intensity | Strength of environment map reflections |
| | Specular | Intensity of specular highlights on facets |
| | Clearcoat | Strength of the polished surface sheen layer |
| **Refraction** | IOR | Index of refraction (varies per gemstone) |
| | Dispersion | Chromatic split — controls rainbow "fire" intensity |
| | Thickness | Internal optical path length for light absorption |

---

## Technical Discussion

### Geometry: Parametric Brilliant Cut

The gem is generated procedurally from nine parameters defined in `data/brilliant-cut.json`, based on Marcel Tolkowsky's 1919 ideal cut proportions. The geometry builder (`gem-geometry.js`) constructs all 57 facets of a standard round brilliant:

- **Crown (33 facets):** 1 octagonal table, 8 star facets, 8 bezel (kite) facets, and 16 upper girdle triangles.
- **Pavilion (24 facets):** 8 main pavilion facets and 16 lower girdle triangles.
- **Girdle:** A thin band of 16 quadrilaterals connecting crown and pavilion.

Vertices are computed on circles at analytically derived heights: crown height = `(R - tableR) * tan(crownAngle)`, pavilion depth = `R * tan(pavilionAngle)`. The geometry uses 8-fold rotational symmetry (`symmetryFold = 8`), generating vertices at evenly spaced angular positions around the Y axis.

The mesh is **non-indexed** (flat-shaded) by design. After initial vertex generation, the builder overwrites the computed smooth normals with per-face normals so that each facet reflects light independently. This is essential for diamond realism — smooth-shaded normals would destroy the sharp facet edges that produce a diamond's characteristic pattern of light and dark reflections.

### Material: Physically-Based Gemstone Optics

The material (`gem-material.js`) uses `THREE.MeshPhysicalMaterial` — Three.js's most advanced PBR material — with a **preset system** that defines per-stone optical properties. All four gemstones share the same rendering flags (transmission, clearcoat, double-sided) but differ in the properties that create each stone's unique appearance:

- **Transmission (`1.0`):** The body is fully transparent. Light passes through rather than being absorbed, as in a real gemstone.
- **Index of Refraction:** Varies per stone (diamond 2.42, sapphire 1.77, emerald 1.57, citrine 1.55). Higher IOR produces stronger internal reflections and more brilliance.
- **Dispersion:** Varies per stone (diamond 0.044 down to citrine 0.013). Available since Three.js r166, this splits refraction into per-wavelength IOR offsets, simulating chromatic aberration — the rainbow "fire" visible at facet edges.
- **Attenuation color and distance:** Controls the body color of each stone. Diamond uses white attenuation for a colorless appearance; sapphire uses deep blue, emerald uses green, and citrine uses warm amber. The attenuation distance controls how quickly the color saturates over optical path length.
- **Clearcoat (`1.0`):** Adds a second specular layer on top of the transmissive body, simulating the polished surface sheen of a real cut stone. Combined with `clearcoatRoughness: 0`, this creates sharp, bright surface reflections.
- **Roughness (`0.0`) / Metalness (`0.0`):** Perfectly smooth dielectric surface, as expected for a polished gemstone.
- **Double-sided rendering:** `side: DoubleSide` is required for transmission to work correctly — the renderer must see both front and back faces to compute refraction paths through the gem body.

The `applyGemPreset()` function switches the material in-place without recreating the mesh, updating only the stone-specific properties and leaving shared flags untouched.

### Environment & Lighting

The scene uses a **procedural environment map** generated by Three.js's `RoomEnvironment`, which creates a neutral studio-like box. This is processed through a `PMREMGenerator` (Prefiltered Mipmapped Radiance Environment Map) to produce the glossy-to-rough mip chain that `MeshPhysicalMaterial` samples at different roughness levels. The environment map is critical: without it, the transmissive/reflective material would have nothing to reflect or refract, and the gem would appear empty.

Four **point lights** at asymmetric positions create visible specular "fire spots" on the facets — the bright flashes that move as the gem rotates. A dim ambient light provides minimal fill without washing out the specular contrast.

### Post-Processing: Bloom

The render pipeline uses Three.js's `EffectComposer` with three passes:

1. **RenderPass** — Renders the scene normally.
2. **UnrealBloomPass** — Applies a selective bloom/glow effect. With a high threshold (`0.85`), only the brightest specular highlights (the "fire" flashes) produce visible bloom, simulating the scintillation effect seen in real diamonds under point lighting. The strength and radius are kept subtle to avoid an over-processed look.
3. **OutputPass** — Handles final color space conversion (linear working space to sRGB output).

**Tone mapping** uses ACES Filmic at a low exposure of 0.1, compressing the high-dynamic-range specular peaks into a displayable range while preserving contrast in the midtones.

### Settings UI Architecture

The settings panel (`settings-ui.js`) is fully **data-driven** and decoupled from Three.js. Sliders in the HTML carry `data-target`, `data-prop`, and `data-default` attributes; the JS module reads these at init time and builds a binding map to the live objects it receives via dependency injection (`{ bloomPass, renderer, material }`). Adding a new slider requires only HTML markup — no JavaScript changes. The module has zero imports from Three.js and no knowledge of the rendering pipeline; it simply sets numeric properties on the objects it is given.

The module also exports a `syncSliders()` function that re-reads live values from the bound target objects and updates all slider positions and displays. This is called after each gemstone switch so the panel always reflects the current material state.
