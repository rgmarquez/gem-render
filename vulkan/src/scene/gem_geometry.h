/**
 * gem_geometry.h
 *
 * Procedural 57-facet round brilliant-cut gemstone geometry builder.
 * Port of the JavaScript gem-geometry.js to C.
 *
 * Generates a flat-shaded vertex array (position + normal per vertex).
 * 112 triangles = 336 vertices.
 */

#ifndef GEM_GEOMETRY_H
#define GEM_GEOMETRY_H

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Vertex layout: interleaved position (vec3) + normal (vec3)
// ---------------------------------------------------------------------------
typedef struct GemVertex {
    float position[3];
    float normal[3];
} GemVertex;

// ---------------------------------------------------------------------------
// Gem geometry data (CPU-side)
// ---------------------------------------------------------------------------
typedef struct GemGeometry {
    GemVertex *vertices;
    uint32_t   vertexCount;
} GemGeometry;

// ---------------------------------------------------------------------------
// Cut parameters (from brilliant-cut.json)
// ---------------------------------------------------------------------------
typedef struct GemCutParams {
    float girdleRadius;
    float tableRatio;
    float crownAngleDeg;
    float pavilionAngleDeg;
    float girdleThickness;
    int   symmetryFold;
    float starFacetLengthRatio;
    float lowerGirdleFacetLengthRatio;
    float culetSizeRatio;
} GemCutParams;

/**
 * Default Tolkowsky ideal cut parameters.
 */
GemCutParams gem_cut_params_default(void);

/**
 * Load cut parameters from a JSON file.
 * Falls back to defaults on parse error.
 */
GemCutParams gem_cut_params_from_json(const char *path);

/**
 * Build the gem geometry from cut parameters.
 * Caller must call gem_geometry_destroy() when done.
 *
 * @param params Cut parameters
 * @param scale  Uniform scale factor (e.g., 1.2 to match the web version)
 * @param out    Output geometry
 * @return true on success.
 */
bool gem_geometry_build(const GemCutParams *params, float scale,
                         GemGeometry *out);

/**
 * Free the vertex array.
 */
void gem_geometry_destroy(GemGeometry *geo);

#endif /* GEM_GEOMETRY_H */
