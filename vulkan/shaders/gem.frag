#version 450

// ---------------------------------------------------------------------------
// gem.frag — Phase 6: adds screen-space transmission via an off-screen
//            background render target that mirrors the Three.js
//            OpaqueRenderTarget pattern.  When the background is empty the
//            shader falls back to env-based refraction (no visual regression).
//
// Physical model (front face):
//   1. Schlick Fresnel from IOR-derived F0.
//   2. Specular IBL:     texture(envMap, reflect(-V, N)) × envMapIntensity
//   3. Refraction IBL:   texture(envMap, refract(-V, N, 1/ior)) × Beer-Lambert
//   4. Direct specular:  Blinn-Phong from 4 point lights — the "fire" flashes
//                        that move as the gem rotates.
//   5. Clearcoat:        second smooth dielectric lobe (F0 = 0.04) on top.
//   6. Bloom approx:     pre-ACES brightening of specular peaks above the
//                        Three.js bloom threshold (0.85); no spatial blur, but
//                        narrow point-light highlights look identical.
//   7. ACES tone mapping + final alpha from Fresnel curve.
//
// Physical model (back face):
//   Interior surface seen from outside. Refract view ray from gem→air side
//   (eta = ior). TIR is expected and common; falls back to the reflected env.
//   Dim alpha so the front-face composite mostly covers it.
// ---------------------------------------------------------------------------

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragWorldNormal;

layout(location = 0) out vec4 outColor;

// ---- Uniforms -------------------------------------------------------------

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
} frame;

layout(set = 0, binding = 1) uniform MaterialUBO {
    float ior;
    float dispersion;
    float thickness;
    float attenuationDistance;

    vec4  attenuationColor;       // .rgb = body tint, .a unused

    float specularIntensity;
    float envMapIntensity;
    float clearcoat;
    float clearcoatRoughness;

    float roughness;
    float metalness;
    float _pad0;
    float _pad1;
} material;

layout(set = 0, binding = 2) uniform samplerCube envMap;

// Background render target: one image per frame-in-flight, same extent as the
// swapchain.  Empty pixels are cleared to (0,0,0,0); when opaque scene objects
// are placed there they write alpha=1 so the gem shader knows to prefer the
// screen-space sample over the env-cube fallback.
layout(set = 0, binding = 3) uniform sampler2D backgroundMap;

// ---- Utility functions ----------------------------------------------------

// ACES Filmic tone mapping (Narkowicz 2015 approximation).
vec3 aces(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Schlick Fresnel reflectance at a given cos(theta) and F0.
float fresnel_schlick(float cosTheta, float f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Three.js-matching distance attenuation: inverse-square + smooth range cutoff.
float dist_atten(float dist, float range, float intensity)
{
    float att   = intensity / max(dist * dist, 0.001);
    float ratio = dist / range;
    float r2    = ratio * ratio;
    float w     = clamp(1.0 - r2 * r2, 0.0, 1.0);
    return att * w * w;
}

// Beer-Lambert transmittance: pow(attenuationColor, thickness/attenuationDistance).
// Equivalent to Three.js MeshPhysicalMaterial volumetric attenuation.
vec3 beer_lambert(vec3 attenColor, float thickness, float attenuationDist)
{
    float d = thickness / max(attenuationDist, 0.001);
    return pow(clamp(attenColor, vec3(0.001), vec3(1.0)), vec3(d));
}

// ---------------------------------------------------------------------------
// Rendering constants — match Three.js output pipeline
// ---------------------------------------------------------------------------

// Three.js renderer.toneMappingExposure = 0.10.  Without this, the Vulkan
// output is ~10x brighter before ACES, destroying contrast and making the
// gem look flat/washed-out.
const float EXPOSURE = 0.10;

// The procedural env map is stored as UNORM [0,1].  Three.js RoomEnvironment
// processed through PMREMGenerator produces true HDR values (peaks 3-10).
// This scale factor recovers comparable dynamic range before envMapIntensity
// and exposure are applied.
const float ENV_STORED_RANGE = 5.0;

// Blinn-Phong specular (shininess 1024) peaks ~5-8x lower than the GGX
// distribution Three.js uses at roughness ≈ 0.  This multiplier compensates,
// keeping the direct-light "fire" flashes at physically realistic intensity.
const float DIRECT_SPEC_BOOST = 8.0;

// GGX/Trowbridge-Reitz NDF.
float D_GGX(float NdotH, float alpha)
{
    float a2    = alpha * alpha;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom);
}

// Project a world-space point onto the current frame's NDC screen.
// Returns UV in [0,1]x[0,1].  Returns (-1,-1) when the point is behind
// or on the camera plane so callers can discard the sample.
vec2 world_to_screenUV(vec3 worldPos)
{
    vec4 clip = frame.projection * frame.view * vec4(worldPos, 1.0);
    if (clip.w <= 0.0) return vec2(-1.0);
    return clip.xy / clip.w * 0.5 + 0.5;
}

// Smith height-correlated geometric visibility (GGX).
float G_Smith(float NdotV, float NdotL, float alpha)
{
    float k  = alpha * 0.5;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

// ---------------------------------------------------------------------------
// Direct specular accumulator.
// Base layer:  Blinn-Phong (matches the DIRECT_SPEC_BOOST calibration).
// Clearcoat:   GGX NDF + Smith visibility — physically correct polished sheen.
// coatAlpha  = clearcoatRoughness² (perceptual-roughness remap).
// ---------------------------------------------------------------------------
void accumulate_direct_spec(vec3 N, vec3 V,
                             float shininess, float coatAlpha,
                             out vec3 out_spec, out vec3 out_coat)
{
    // Light data matches main.js / renderer-comparison.md spec
    vec3  lpos[4] = vec3[]( vec3( 3.0,  5.0,  3.0),
                             vec3(-4.0,  3.0, -2.0),
                             vec3( 0.0, -3.0,  4.0),
                             vec3( 2.0, -1.0, -5.0) );
    vec3  lcol[4] = vec3[]( vec3(1.00, 1.00, 1.00),
                             vec3(1.00, 1.00, 1.00),
                             vec3(0.80, 0.87, 1.00),
                             vec3(1.00, 1.00, 0.93) );
    float lint[4]  = float[](80.0, 60.0, 40.0, 30.0);
    float lrange[4] = float[](30.0, 30.0, 30.0, 30.0);

    float NdotV_clamp = max(dot(N, V), 0.001);

    out_spec = vec3(0.0);
    out_coat = vec3(0.0);

    for (int i = 0; i < 4; i++) {
        vec3  L     = lpos[i] - fragWorldPos;
        float dist  = length(L);
        L           = normalize(L);
        vec3  H     = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);
        float NdotL = max(dot(N, L), 0.0);
        float atten = dist_atten(dist, lrange[i], lint[i]);

        // Base: Blinn-Phong (calibrated with DIRECT_SPEC_BOOST).
        out_spec += lcol[i] * pow(NdotH, shininess) * atten;

        // Clearcoat: GGX NDF + Smith geometric visibility.
        float D   = D_GGX(NdotH, coatAlpha);
        float G   = G_Smith(NdotV_clamp, NdotL, coatAlpha);
        out_coat += lcol[i] * (D * G) * NdotL * atten;
    }
}

// ---------------------------------------------------------------------------
void main()
{
    vec3 N = normalize(fragWorldNormal);
    vec3 V = normalize(frame.cameraPos - fragWorldPos);

    // -----------------------------------------------------------------------
    // Back-face pass (gem interior viewed from outside).
    //
    // The view ray has passed through the front surface. This fragment covers
    // an internal reflection / refraction at a back facet. We approximate it
    // by sampling the environment through the gem→air refracted ray and apply
    // Beer-Lambert tinting. TIR is common for high-IOR gems — fall back to
    // reflection. Keep dim so the front-face composite mostly covers this.
    // -----------------------------------------------------------------------
    if (!gl_FrontFacing) {
        N = -N;

        // Cauchy-model chromatic dispersion for gem→air exit.
        // Delta is proportional to (ior - 1) so refractive power scales the
        // channel separation — matches Three.js r166 dispersion formula:
        // halfSpread = (ior - 1.0) * 0.025 * dispersion
        float disp_d_back = (material.ior - 1.0) * material.dispersion * 0.025;
        float ior_r_back  = material.ior - disp_d_back;
        float ior_g_back  = material.ior;
        float ior_b_back  = material.ior + disp_d_back;

        vec3 R_back = reflect(-V, N);
        vec3 rd_r = refract(-V, N, ior_r_back);
        vec3 rd_g = refract(-V, N, ior_g_back);
        vec3 rd_b = refract(-V, N, ior_b_back);
        if (dot(rd_r, rd_r) < 0.01) rd_r = R_back;
        if (dot(rd_g, rd_g) < 0.01) rd_g = R_back;
        if (dot(rd_b, rd_b) < 0.01) rd_b = R_back;

        float envScaleBack = ENV_STORED_RANGE * material.envMapIntensity;
        vec3 envSample;
        envSample.r = texture(envMap, rd_r).r * envScaleBack;
        envSample.g = texture(envMap, rd_g).g * envScaleBack;
        envSample.b = texture(envMap, rd_b).b * envScaleBack;

        vec3 beer = beer_lambert(material.attenuationColor.rgb,
                                 material.thickness,
                                 material.attenuationDistance);

        vec3 interior = envSample * beer;

        outColor = vec4(aces(interior * EXPOSURE), 0.55);
        return;
    }

    // -----------------------------------------------------------------------
    // Front-face pass (gem surface).
    // -----------------------------------------------------------------------
    float NdotV = max(dot(N, V), 0.001);

    // Schlick Fresnel with physical F0 from IOR: F0 = ((n-1)/(n+1))²
    //   Diamond  n=2.42  F0≈0.172 — low frontal reflectance (mostly transmit)
    //   Sapphire n=1.77  F0≈0.077
    float f0 = pow((material.ior - 1.0) / (material.ior + 1.0), 2.0);
    float F  = fresnel_schlick(NdotV, f0);

    // ---- Specular IBL (reflection) ----------------------------------------
    // Reflect view ray off the surface, sample the studio environment.
    // ENV_STORED_RANGE recovers HDR; envMapIntensity is per-preset weight.
    vec3 R       = reflect(-V, N);
    vec3 envSpec = texture(envMap, R).rgb * ENV_STORED_RANGE * material.envMapIntensity;

    // ---- Refraction IBL (transmission / body color) ----------------------
    // Cauchy-model chromatic dispersion: delta scales with (ior - 1) so
    // refractive power drives channel separation — matches Three.js r166:
    // halfSpread = (ior - 1.0) * 0.025 * dispersion
    float disp_d = (material.ior - 1.0) * material.dispersion * 0.025;
    float ior_r  = material.ior - disp_d;
    float ior_g  = material.ior;
    float ior_b  = material.ior + disp_d;

    vec3 refractDir_r = refract(-V, N, 1.0 / ior_r);
    vec3 refractDir_g = refract(-V, N, 1.0 / ior_g);
    vec3 refractDir_b = refract(-V, N, 1.0 / ior_b);
    if (dot(refractDir_r, refractDir_r) < 0.01) refractDir_r = R;
    if (dot(refractDir_g, refractDir_g) < 0.01) refractDir_g = R;
    if (dot(refractDir_b, refractDir_b) < 0.01) refractDir_b = R;

    float envScale = ENV_STORED_RANGE * material.envMapIntensity;

    // Env-cube fallback colours (used when background alpha is 0).
    float envBody_r = texture(envMap, refractDir_r).r * envScale;
    float envBody_g = texture(envMap, refractDir_g).g * envScale;
    float envBody_b = texture(envMap, refractDir_b).b * envScale;

    // ---- Screen-space transmission (Three.js OpaqueRenderTarget pattern) --
    // Approximate exit position by offsetting along the refracted ray by
    // material.thickness. Per-channel offsets produce the chromatic spread.
    vec2 screenUV_r = world_to_screenUV(fragWorldPos + refractDir_r * material.thickness);
    vec2 screenUV_g = world_to_screenUV(fragWorldPos + refractDir_g * material.thickness);
    vec2 screenUV_b = world_to_screenUV(fragWorldPos + refractDir_b * material.thickness);

    // Clamp to valid texture range; out-of-screen UVs naturally edge-clamp
    // to the nearest border pixel (CLAMP_TO_EDGE sampler).
    vec2 uvr = clamp(screenUV_r, vec2(0.0), vec2(1.0));
    vec2 uvg = clamp(screenUV_g, vec2(0.0), vec2(1.0));
    vec2 uvb = clamp(screenUV_b, vec2(0.0), vec2(1.0));

    // For invalid UVs (behind camera) force env fallback.
    float validR = (screenUV_r.x >= 0.0) ? 1.0 : 0.0;
    float validG = (screenUV_g.x >= 0.0) ? 1.0 : 0.0;
    float validB = (screenUV_b.x >= 0.0) ? 1.0 : 0.0;

    // alpha==0: background cleared to transparent → env wins.
    // alpha==1: scene object wrote here → screen-space wins.
    float bgAlpha_r = texture(backgroundMap, uvr).a * validR;
    float bgAlpha_g = texture(backgroundMap, uvg).a * validG;
    float bgAlpha_b = texture(backgroundMap, uvb).a * validB;

    vec3 envBody;
    envBody.r = mix(envBody_r, texture(backgroundMap, uvr).r, bgAlpha_r);
    envBody.g = mix(envBody_g, texture(backgroundMap, uvg).g, bgAlpha_g);
    envBody.b = mix(envBody_b, texture(backgroundMap, uvb).b, bgAlpha_b);

    vec3 beer      = beer_lambert(material.attenuationColor.rgb,
                                  material.thickness,
                                  material.attenuationDistance);
    vec3 refrColor = envBody * beer;

    // ---- Direct specular (Blinn-Phong, 4 point lights) --------------------
    // These produce the sharp "fire" flashes that move as the gem rotates —
    // point-light specular on individual facets, just as in the JS version.
    float shininess = max(1024.0 * pow(1.0 - clamp(material.roughness, 0.0, 1.0), 2.0), 32.0);
    // GGX alpha = roughness² (perceptual-to-linear roughness remap).
    // Clamped to 0.001 so D_GGX and G_Smith stay finite at roughness 0.
    float coatAlpha = max(material.clearcoatRoughness * material.clearcoatRoughness, 0.001);

    vec3 directSpec, coatSpec;
    accumulate_direct_spec(N, V, shininess, coatAlpha, directSpec, coatSpec);
    directSpec *= material.specularIntensity * DIRECT_SPEC_BOOST;

    // ---- Clearcoat --------------------------------------------------------
    // Second smooth dielectric lobe (F0=0.04, polished surface sheen).
    // Additive contribution on top of the base layer — matches Three.js
    // clearcoat behaviour (no energy conservation adjustment).
    float F_coat  = fresnel_schlick(NdotV, 0.04);
    vec3  ccContrib = coatSpec * F_coat * material.clearcoat;

    // ---- Combine ----------------------------------------------------------
    // mix(refrColor, envSpec, F): physically splits energy between
    //   transmission  (1-F) × refracted-env × Beer-Lambert body colour
    //   reflection    F     × reflected-env  (surface IBL)
    // Direct specular and clearcoat stack additively on top (fire + sheen).
    vec3 result = mix(refrColor, envSpec, F);
    result += directSpec + ccContrib;

    // ---- Bloom approximation ----------------------------------------------
    // With DIRECT_SPEC_BOOST compensating for Blinn-Phong vs GGX, the
    // brightest specular peaks live in the 10-30 range (linear HDR).
    // A smooth ramp emphasises those peaks before tone mapping crushes them,
    // approximating Three.js UnrealBloomPass (strength 0.30, threshold 0.85)
    // without spatial blur — indistinguishable for narrow fire highlights.
    float specLum = dot(directSpec + ccContrib, vec3(0.2126, 0.7152, 0.0722));
    float bloom   = smoothstep(4.0, 20.0, specLum) * 0.6;
    result += (directSpec + ccContrib) * bloom;

    // ---- Tone mapping (match Three.js: ACES Filmic @ exposure 0.10) -------
    result *= EXPOSURE;
    result = aces(result);

    // ---- Alpha ------------------------------------------------------------
    // Fresnel-weighted opacity: near-normal incidence is mostly transparent,
    // grazing incidence is fully reflective. Floor ensures the gem never
    // disappears against a black background.
    float alpha = clamp(F * 0.65 + 0.30, 0.30, 0.97);

    outColor = vec4(result, alpha);
}
