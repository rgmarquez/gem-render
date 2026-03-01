/**
 * gem-geometry.js
 *
 * Generates a 57-facet round brilliant-cut diamond as a THREE.BufferGeometry.
 * Each facet is a separate flat-shaded polygon (non-indexed, per-face normals)
 * so that individual facets catch and reflect light independently — essential
 * for realistic diamond rendering.
 *
 * Facet layout (standard round brilliant):
 *   Crown (top):  1 table + 8 star + 8 bezel (kite) + 16 upper girdle = 33
 *   Pavilion (bottom):  8 main + 16 lower girdle = 24
 *   Total: 57 facets
 *
 * Coordinate system: Y-up. Table faces +Y, culet points -Y.
 * Girdle sits at Y = 0 (crown above, pavilion below).
 */

import * as THREE from "three";

const DEG2RAD = Math.PI / 180;

/**
 * Load the parametric config from JSON and build the geometry.
 * @param {string} jsonPath - URL to brilliant-cut.json
 * @returns {Promise<THREE.BufferGeometry>}
 */
export async function createBrilliantGeometry(jsonPath) {
    const resp = await fetch(jsonPath);
    const config = await resp.json();
    return buildGeometry(config.parameters);
}

/**
 * Build the geometry directly from parameter values (no fetch).
 * @param {object} params
 * @returns {THREE.BufferGeometry}
 */
export function buildGeometry(params) {
    const {
        girdleRadius = 1.0,
        tableRatio = 0.57,
        crownAngleDeg = 34.5,
        pavilionAngleDeg = 40.75,
        girdleThickness = 0.025,
        symmetryFold = 8,
        starFacetLengthRatio = 0.50,
        lowerGirdleFacetLengthRatio = 0.75,
        culetSizeRatio = 0.0,
    } = params;

    const N = symmetryFold; // 8-fold rotational symmetry
    const R = girdleRadius;
    const tableR = R * tableRatio;

    // Derived heights
    const crownAngle = crownAngleDeg * DEG2RAD;
    const pavilionAngle = pavilionAngleDeg * DEG2RAD;
    const crownHeight = (R - tableR) * Math.tan(crownAngle);
    const pavilionDepth = R * Math.tan(pavilionAngle);
    const halfGirdle = girdleThickness / 2;

    // Y coordinates
    const yTable = crownHeight + halfGirdle;
    const yGirdleTop = halfGirdle;
    const yGirdleBot = -halfGirdle;
    const yCulet = -(pavilionDepth + halfGirdle);

    // ---------------------------------------------------------------
    // Helper: point on circle at angle theta, radius r, at height y
    // ---------------------------------------------------------------
    function pt(r, theta, y) {
        return new THREE.Vector3(r * Math.cos(theta), y, r * Math.sin(theta));
    }

    // ---------------------------------------------------------------
    // Key angular positions (2*N evenly spaced around the gem)
    // Main facet vertices sit at i * (2PI / 2N) = i * PI/N
    // ---------------------------------------------------------------
    const step = (2 * Math.PI) / (2 * N); // PI/8 for N=8

    // Girdle vertices: 2N points on the girdle circle (top and bottom edge)
    const girdleTop = [];
    const girdleBot = [];
    for (let i = 0; i < 2 * N; i++) {
        const angle = i * step;
        girdleTop.push(pt(R, angle, yGirdleTop));
        girdleBot.push(pt(R, angle, yGirdleBot));
    }

    // Table vertices: N points on the table circle
    // Table vertices align with even-indexed girdle vertices (main directions)
    const tableVerts = [];
    for (let i = 0; i < N; i++) {
        const angle = i * 2 * step; // same as i * (2PI/N)
        tableVerts.push(pt(tableR, angle, yTable));
    }

    // Star facet intermediate points on the crown (between table and girdle)
    // These sit at odd multiples of step (halfway between main girdle vertices)
    // at a radius interpolated between table and girdle, at a height on the
    // crown slope.
    const starR = tableR + (R - tableR) * starFacetLengthRatio;
    const starY = yTable - (starR - tableR) * Math.tan(crownAngle);
    const starVerts = [];
    for (let i = 0; i < N; i++) {
        const angle = (2 * i + 1) * step;
        starVerts.push(pt(starR, angle, starY));
    }

    // Pavilion main facet lower points (toward culet)
    // For culetSizeRatio = 0 they all converge to the culet point.
    const culetR = R * culetSizeRatio;
    const culetVerts = [];
    if (culetR < 0.001) {
        // Single culet point
        culetVerts.push(new THREE.Vector3(0, yCulet, 0));
    } else {
        for (let i = 0; i < N; i++) {
            const angle = i * 2 * step;
            culetVerts.push(pt(culetR, angle, yCulet));
        }
    }

    // Lower girdle intermediate points on the pavilion
    // Sit at odd multiples of step, at a radius interpolated between girdle and culet
    const lowerR = R * (1 - lowerGirdleFacetLengthRatio);
    const lowerY = yGirdleBot - (R - lowerR) * Math.tan(pavilionAngle);
    const lowerVerts = [];
    for (let i = 0; i < N; i++) {
        const angle = (2 * i + 1) * step;
        lowerVerts.push(pt(lowerR, angle, lowerY));
    }

    // ---------------------------------------------------------------
    // Collect all triangles (non-indexed, for flat per-face normals)
    // ---------------------------------------------------------------
    const triangles = [];

    function addTri(a, b, c) {
        triangles.push(a.clone(), b.clone(), c.clone());
    }

    function addQuad(a, b, c, d) {
        // Two triangles: (a, b, c) and (a, c, d)
        addTri(a, b, c);
        addTri(a, c, d);
    }

    // ---------- TABLE (1 octagonal facet → 8 triangles) ----------
    // Table center
    const tableCenter = new THREE.Vector3(0, yTable, 0);
    for (let i = 0; i < N; i++) {
        const next = (i + 1) % N;
        addTri(tableCenter, tableVerts[i], tableVerts[next]);
    }

    // ---------- STAR FACETS (8 triangles) ----------
    // Each star facet is a triangle: tableVerts[i], starVerts[i], tableVerts[next]
    for (let i = 0; i < N; i++) {
        const next = (i + 1) % N;
        addTri(tableVerts[next], tableVerts[i], starVerts[i]);
    }

    // ---------- BEZEL / KITE FACETS (8 quadrilaterals → 16 triangles) ----------
    // Each bezel kite connects: starVerts[prev], girdleTop[2*i], starVerts[i], tableVerts[i]
    // Kite shape: top = tableVerts[i], left = starVerts[prev], bottom = girdleTop[2*i], right = starVerts[i]
    for (let i = 0; i < N; i++) {
        const prev = (i - 1 + N) % N;
        addQuad(
            tableVerts[i],
            starVerts[prev],
            girdleTop[2 * i],
            starVerts[i]
        );
    }

    // ---------- UPPER GIRDLE FACETS (16 triangles) ----------
    // Two triangles per main direction sector:
    //   Left:  starVerts[prev], girdleTop[2*i - 1], girdleTop[2*i]
    //   Right: starVerts[i],    girdleTop[2*i],     girdleTop[2*i + 1]
    for (let i = 0; i < N; i++) {
        const gi = 2 * i;
        const giPrev = (gi - 1 + 2 * N) % (2 * N);
        const giNext = (gi + 1) % (2 * N);
        const prev = (i - 1 + N) % N;

        addTri(starVerts[prev], girdleTop[giPrev], girdleTop[gi]);
        addTri(starVerts[i], girdleTop[gi], girdleTop[giNext]);
    }

    // ---------- GIRDLE (thin band — 2N quads → 4N triangles) ----------
    // Connect girdleTop[i] to girdleBot[i]
    for (let i = 0; i < 2 * N; i++) {
        const next = (i + 1) % (2 * N);
        addQuad(
            girdleTop[i],
            girdleBot[i],
            girdleBot[next],
            girdleTop[next]
        );
    }

    // ---------- PAVILION MAIN FACETS (8 quadrilaterals → 16 triangles) ----------
    // Each main pavilion facet is a kite from girdleBot[2*i] down to culet,
    // flanked by lowerVerts.
    // For culetSizeRatio = 0, culet is a single point → these become triangles.
    if (culetR < 0.001) {
        const culet = culetVerts[0];
        for (let i = 0; i < N; i++) {
            const prev = (i - 1 + N) % N;
            addQuad(
                girdleBot[2 * i],
                lowerVerts[prev],
                culet,
                lowerVerts[i]
            );
        }
    } else {
        for (let i = 0; i < N; i++) {
            const prev = (i - 1 + N) % N;
            addQuad(
                girdleBot[2 * i],
                lowerVerts[prev],
                culetVerts[i],
                lowerVerts[i]
            );
        }
    }

    // ---------- LOWER GIRDLE FACETS (16 triangles) ----------
    for (let i = 0; i < N; i++) {
        const gi = 2 * i;
        const giPrev = (gi - 1 + 2 * N) % (2 * N);
        const giNext = (gi + 1) % (2 * N);
        const prev = (i - 1 + N) % N;

        addTri(lowerVerts[prev], girdleBot[giPrev], girdleBot[gi]);
        addTri(lowerVerts[i], girdleBot[gi], girdleBot[giNext]);
    }

    // ---------------------------------------------------------------
    // Build BufferGeometry
    // ---------------------------------------------------------------
    const positions = new Float32Array(triangles.length * 3);
    for (let i = 0; i < triangles.length; i++) {
        positions[i * 3] = triangles[i].x;
        positions[i * 3 + 1] = triangles[i].y;
        positions[i * 3 + 2] = triangles[i].z;
    }

    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
    geometry.computeVertexNormals();

    // Override with flat per-face normals:
    // For each triangle, compute the face normal and assign it to all 3 vertices.
    const normals = geometry.attributes.normal.array;
    const vA = new THREE.Vector3();
    const vB = new THREE.Vector3();
    const vC = new THREE.Vector3();
    const ab = new THREE.Vector3();
    const ac = new THREE.Vector3();
    const faceNormal = new THREE.Vector3();

    for (let i = 0; i < triangles.length; i += 3) {
        vA.set(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
        vB.set(positions[(i + 1) * 3], positions[(i + 1) * 3 + 1], positions[(i + 1) * 3 + 2]);
        vC.set(positions[(i + 2) * 3], positions[(i + 2) * 3 + 1], positions[(i + 2) * 3 + 2]);

        ab.subVectors(vB, vA);
        ac.subVectors(vC, vA);
        faceNormal.crossVectors(ab, ac).normalize();

        for (let j = 0; j < 3; j++) {
            normals[(i + j) * 3] = faceNormal.x;
            normals[(i + j) * 3 + 1] = faceNormal.y;
            normals[(i + j) * 3 + 2] = faceNormal.z;
        }
    }

    geometry.attributes.normal.needsUpdate = true;
    geometry.computeBoundingSphere();

    return geometry;
}
