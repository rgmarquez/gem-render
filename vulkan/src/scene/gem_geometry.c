/**
 * gem_geometry.c
 *
 * Port of the JavaScript 57-facet brilliant-cut geometry builder.
 *
 * Facet layout (standard round brilliant):
 *   Crown (top):  1 table (8 tri) + 8 star + 8 bezel kite (16 tri) + 16 upper girdle = 33 facets
 *   Girdle:       16 quads (32 tri band)
 *   Pavilion:     8 main (16 tri) + 16 lower girdle = 24 facets
 *   Total: 57 facets, 112 triangles, 336 vertices
 *
 * Coordinate system: Y-up. Table faces +Y, culet points -Y.
 * Girdle sits at Y = 0.
 */

#include "gem_geometry.h"
#include "util/file_io.h"

#include <cjson/cJSON.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEG2RAD (3.14159265358979323846f / 180.0f)
#define PI      3.14159265358979323846f

// Max triangles: table(8) + star(8) + bezel(16) + upper_girdle(16)
//              + girdle(32) + pavilion_main(16) + lower_girdle(16) = 112
#define MAX_TRIANGLES 112
#define MAX_VERTICES  (MAX_TRIANGLES * 3)

// ---------------------------------------------------------------------------
// Internal: 3D point helper
// ---------------------------------------------------------------------------
typedef struct Vec3 {
    float x, y, z;
} Vec3;

static Vec3 vec3_make(float x, float y, float z)
{
    return (Vec3){ x, y, z };
}

// Point on circle at angle theta, radius r, height y
static Vec3 pt(float r, float theta, float y)
{
    return vec3_make(r * cosf(theta), y, r * sinf(theta));
}

static Vec3 vec3_sub(Vec3 a, Vec3 b)
{
    return vec3_make(a.x - b.x, a.y - b.y, a.z - b.z);
}

static Vec3 vec3_cross(Vec3 a, Vec3 b)
{
    return vec3_make(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

static Vec3 vec3_normalize(Vec3 v)
{
    float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 1e-8f) {
        float inv = 1.0f / len;
        v.x *= inv;
        v.y *= inv;
        v.z *= inv;
    }
    return v;
}

static Vec3 vec3_scale(Vec3 v, float s)
{
    return vec3_make(v.x * s, v.y * s, v.z * s);
}

// ---------------------------------------------------------------------------
// Triangle accumulator
// ---------------------------------------------------------------------------
typedef struct TriAccum {
    Vec3     verts[MAX_VERTICES];
    uint32_t count;  // number of vertices (always multiple of 3)
} TriAccum;

static void accum_tri(TriAccum *acc, Vec3 a, Vec3 b, Vec3 c)
{
    if (acc->count + 3 > MAX_VERTICES) return;
    acc->verts[acc->count++] = a;
    acc->verts[acc->count++] = b;
    acc->verts[acc->count++] = c;
}

static void accum_quad(TriAccum *acc, Vec3 a, Vec3 b, Vec3 c, Vec3 d)
{
    accum_tri(acc, a, b, c);
    accum_tri(acc, a, c, d);
}

// ---------------------------------------------------------------------------
// Default parameters
// ---------------------------------------------------------------------------

GemCutParams gem_cut_params_default(void)
{
    return (GemCutParams){
        .girdleRadius              = 1.0f,
        .tableRatio                = 0.57f,
        .crownAngleDeg             = 34.5f,
        .pavilionAngleDeg          = 40.75f,
        .girdleThickness           = 0.025f,
        .symmetryFold              = 8,
        .starFacetLengthRatio      = 0.50f,
        .lowerGirdleFacetLengthRatio = 0.75f,
        .culetSizeRatio            = 0.0f,
    };
}

// ---------------------------------------------------------------------------
// JSON loading
// ---------------------------------------------------------------------------

GemCutParams gem_cut_params_from_json(const char *path)
{
    GemCutParams defaults = gem_cut_params_default();

    char *json = file_io_read_text(path);
    if (!json) {
        fprintf(stderr, "[gem_geometry] Failed to read %s, using defaults.\n", path);
        return defaults;
    }

    cJSON *root = cJSON_Parse(json);
    free(json);

    if (!root) {
        fprintf(stderr, "[gem_geometry] JSON parse error, using defaults.\n");
        return defaults;
    }

    cJSON *params = cJSON_GetObjectItem(root, "parameters");
    if (!params) {
        cJSON_Delete(root);
        return defaults;
    }

    GemCutParams p = defaults;

    cJSON *item;
    if ((item = cJSON_GetObjectItem(params, "girdleRadius")))
        p.girdleRadius = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(params, "tableRatio")))
        p.tableRatio = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(params, "crownAngleDeg")))
        p.crownAngleDeg = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(params, "pavilionAngleDeg")))
        p.pavilionAngleDeg = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(params, "girdleThickness")))
        p.girdleThickness = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(params, "symmetryFold")))
        p.symmetryFold = item->valueint;
    if ((item = cJSON_GetObjectItem(params, "starFacetLengthRatio")))
        p.starFacetLengthRatio = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(params, "lowerGirdleFacetLengthRatio")))
        p.lowerGirdleFacetLengthRatio = (float)item->valuedouble;
    if ((item = cJSON_GetObjectItem(params, "culetSizeRatio")))
        p.culetSizeRatio = (float)item->valuedouble;

    cJSON_Delete(root);
    return p;
}

// ---------------------------------------------------------------------------
// Geometry builder
// ---------------------------------------------------------------------------

bool gem_geometry_build(const GemCutParams *params, float scale,
                         GemGeometry *out)
{
    memset(out, 0, sizeof(*out));

    const int   N            = params->symmetryFold;  // 8
    const float R            = params->girdleRadius;
    const float tableR       = R * params->tableRatio;
    const float crownAngle   = params->crownAngleDeg * DEG2RAD;
    const float pavilionAngle = params->pavilionAngleDeg * DEG2RAD;
    const float halfGirdle   = params->girdleThickness / 2.0f;

    // Derived heights
    const float crownHeight  = (R - tableR) * tanf(crownAngle);
    const float pavilionDepth = R * tanf(pavilionAngle);

    // Y coordinates
    const float yTable     = crownHeight + halfGirdle;
    const float yGirdleTop = halfGirdle;
    const float yGirdleBot = -halfGirdle;
    const float yCulet     = -(pavilionDepth + halfGirdle);

    // Angular step: 2*N points around the girdle
    const float step = (2.0f * PI) / (2.0f * (float)N);  // PI / N

    // --- Girdle vertices (2N top + 2N bottom) ---
    Vec3 girdleTop[32], girdleBot[32];  // max 2*16
    for (int i = 0; i < 2 * N; i++) {
        float angle = (float)i * step;
        girdleTop[i] = pt(R, angle, yGirdleTop);
        girdleBot[i] = pt(R, angle, yGirdleBot);
    }

    // --- Table vertices (N points) ---
    Vec3 tableVerts[16];
    for (int i = 0; i < N; i++) {
        float angle = (float)i * 2.0f * step;
        tableVerts[i] = pt(tableR, angle, yTable);
    }

    // --- Star vertices (N points, at odd multiples of step) ---
    const float starR = tableR + (R - tableR) * params->starFacetLengthRatio;
    const float starY = yTable - (starR - tableR) * tanf(crownAngle);
    Vec3 starVerts[16];
    for (int i = 0; i < N; i++) {
        float angle = (float)(2 * i + 1) * step;
        starVerts[i] = pt(starR, angle, starY);
    }

    // --- Culet vertices ---
    const float culetR = R * params->culetSizeRatio;
    Vec3 culetVerts[16];
    int culetCount = 0;
    if (culetR < 0.001f) {
        culetVerts[0] = vec3_make(0.0f, yCulet, 0.0f);
        culetCount = 1;
    } else {
        for (int i = 0; i < N; i++) {
            float angle = (float)i * 2.0f * step;
            culetVerts[i] = pt(culetR, angle, yCulet);
        }
        culetCount = N;
    }

    // --- Lower girdle intermediate vertices ---
    const float lowerR = R * (1.0f - params->lowerGirdleFacetLengthRatio);
    const float lowerY = yGirdleBot - (R - lowerR) * tanf(pavilionAngle);
    Vec3 lowerVerts[16];
    for (int i = 0; i < N; i++) {
        float angle = (float)(2 * i + 1) * step;
        lowerVerts[i] = pt(lowerR, angle, lowerY);
    }

    // --- Accumulate triangles ---
    TriAccum acc = { .count = 0 };

    // TABLE (1 octagonal facet -> N triangles)
    Vec3 tableCenter = vec3_make(0.0f, yTable, 0.0f);
    for (int i = 0; i < N; i++) {
        int next = (i + 1) % N;
        accum_tri(&acc, tableCenter, tableVerts[i], tableVerts[next]);
    }

    // STAR FACETS (N triangles)
    for (int i = 0; i < N; i++) {
        int next = (i + 1) % N;
        accum_tri(&acc, tableVerts[next], tableVerts[i], starVerts[i]);
    }

    // BEZEL / KITE FACETS (N quads -> 2N triangles)
    for (int i = 0; i < N; i++) {
        int prev = (i - 1 + N) % N;
        accum_quad(&acc,
                   tableVerts[i],
                   starVerts[prev],
                   girdleTop[2 * i],
                   starVerts[i]);
    }

    // UPPER GIRDLE FACETS (2N triangles)
    for (int i = 0; i < N; i++) {
        int gi     = 2 * i;
        int giPrev = (gi - 1 + 2 * N) % (2 * N);
        int giNext = (gi + 1) % (2 * N);
        int prev   = (i - 1 + N) % N;

        accum_tri(&acc, starVerts[prev], girdleTop[giPrev], girdleTop[gi]);
        accum_tri(&acc, starVerts[i], girdleTop[gi], girdleTop[giNext]);
    }

    // GIRDLE BAND (2N quads -> 4N triangles)
    for (int i = 0; i < 2 * N; i++) {
        int next = (i + 1) % (2 * N);
        accum_quad(&acc,
                   girdleTop[i],
                   girdleBot[i],
                   girdleBot[next],
                   girdleTop[next]);
    }

    // PAVILION MAIN FACETS (N quads -> 2N triangles)
    if (culetR < 0.001f) {
        Vec3 culet = culetVerts[0];
        for (int i = 0; i < N; i++) {
            int prev = (i - 1 + N) % N;
            accum_quad(&acc,
                       girdleBot[2 * i],
                       lowerVerts[prev],
                       culet,
                       lowerVerts[i]);
        }
    } else {
        for (int i = 0; i < N; i++) {
            int prev = (i - 1 + N) % N;
            accum_quad(&acc,
                       girdleBot[2 * i],
                       lowerVerts[prev],
                       culetVerts[i],
                       lowerVerts[i]);
        }
    }

    // LOWER GIRDLE FACETS (2N triangles)
    for (int i = 0; i < N; i++) {
        int gi     = 2 * i;
        int giPrev = (gi - 1 + 2 * N) % (2 * N);
        int giNext = (gi + 1) % (2 * N);
        int prev   = (i - 1 + N) % N;

        accum_tri(&acc, lowerVerts[prev], girdleBot[giPrev], girdleBot[gi]);
        accum_tri(&acc, lowerVerts[i], girdleBot[gi], girdleBot[giNext]);
    }

    // --- Build vertex array with flat per-face normals ---
    uint32_t triCount = acc.count / 3;
    out->vertexCount = acc.count;
    out->vertices = malloc(out->vertexCount * sizeof(GemVertex));
    if (!out->vertices) {
        fprintf(stderr, "[gem_geometry] Failed to allocate %u vertices.\n", out->vertexCount);
        return false;
    }

    for (uint32_t t = 0; t < triCount; t++) {
        Vec3 a = acc.verts[t * 3 + 0];
        Vec3 b = acc.verts[t * 3 + 1];
        Vec3 c = acc.verts[t * 3 + 2];

        // Apply uniform scale
        a = vec3_scale(a, scale);
        b = vec3_scale(b, scale);
        c = vec3_scale(c, scale);

        // Flat face normal from winding order
        Vec3 ab = vec3_sub(b, a);
        Vec3 ac = vec3_sub(c, a);
        Vec3 n  = vec3_normalize(vec3_cross(ab, ac));

        // Outward-normal correction: the normal must point away from the gem
        // center (origin).  Use the face centroid as the reference direction.
        // If n points inward (dot < 0), reverse the winding (swap b and c)
        // so Vulkan's CCW front-face rule sees the correct orientation, and
        // negate n to match.
        Vec3 centroid = vec3_make(
            (a.x + b.x + c.x) / 3.0f,
            (a.y + b.y + c.y) / 3.0f,
            (a.z + b.z + c.z) / 3.0f
        );
        float outwardness = n.x * centroid.x + n.y * centroid.y + n.z * centroid.z;
        if (outwardness < 0.0f) {
            // Swap b and c to flip CCW winding, then negate normal
            Vec3 tmp = b;
            b = c;
            c = tmp;
            n.x = -n.x;
            n.y = -n.y;
            n.z = -n.z;
        }

        for (int v = 0; v < 3; v++) {
            Vec3 *p;
            if (v == 0) p = &a;
            else if (v == 1) p = &b;
            else p = &c;

            GemVertex *vert = &out->vertices[t * 3 + v];
            vert->position[0] = p->x;
            vert->position[1] = p->y;
            vert->position[2] = p->z;
            vert->normal[0]   = n.x;
            vert->normal[1]   = n.y;
            vert->normal[2]   = n.z;
        }
    }

    fprintf(stderr, "[gem_geometry] Built %u triangles (%u vertices)\n",
            triCount, out->vertexCount);

    return true;
}

void gem_geometry_destroy(GemGeometry *geo)
{
    free(geo->vertices);
    memset(geo, 0, sizeof(*geo));
}
