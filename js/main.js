/**
 * main.js
 *
 * Entry point for the diamond renderer.
 *
 * Sets up: renderer, camera, scene, environment, lights, gem mesh,
 * OrbitControls, and post-processing (UnrealBloom for sparkle).
 */

import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { EffectComposer } from "three/addons/postprocessing/EffectComposer.js";
import { RenderPass } from "three/addons/postprocessing/RenderPass.js";
import { UnrealBloomPass } from "three/addons/postprocessing/UnrealBloomPass.js";
import { OutputPass } from "three/addons/postprocessing/OutputPass.js";
import { RoomEnvironment } from "three/addons/environments/RoomEnvironment.js";

import { createBrilliantGeometry } from "./gem-geometry.js";
import { createGemMaterial, applyGemPreset, GEM_PRESETS, GEM_ORDER } from "./gem-material.js";
import { initSettingsUI, syncSliders } from "./settings-ui.js";

// ---------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------
const renderer = new THREE.WebGLRenderer({
    antialias: true,
    powerPreference: "high-performance",
});
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setClearColor(0x000000, 1);
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 0.10;
renderer.outputColorSpace = THREE.SRGBColorSpace;
document.body.appendChild(renderer.domElement);

// ---------------------------------------------------------------
// Scene & Camera
// ---------------------------------------------------------------
const scene = new THREE.Scene();

const camera = new THREE.PerspectiveCamera(
    40,
    window.innerWidth / window.innerHeight,
    0.1,
    100
);
camera.position.set(0, 1.5, 4.5);

// ---------------------------------------------------------------
// Environment map (procedural — no external HDR files)
//
// RoomEnvironment generates a neutral studio box that gives
// the diamond something meaningful to reflect and refract.
// The scene background stays black; only the env map is used.
// ---------------------------------------------------------------
const pmremGenerator = new THREE.PMREMGenerator(renderer);
pmremGenerator.compileEquirectangularShader();

const roomEnv = new RoomEnvironment(renderer);
const envTexture = pmremGenerator.fromScene(roomEnv, 0.04).texture;

scene.environment = envTexture;
// scene.background left unset → renderer clear color (black) is used.

roomEnv.dispose();

// ---------------------------------------------------------------
// Lights
//
// Point lights create visible specular "fire" spots on the facets.
// The environment map handles ambient/reflected illumination.
// ---------------------------------------------------------------
const lights = [
    { color: 0xffffff, intensity: 80, pos: [3, 5, 3] },
    { color: 0xffffff, intensity: 60, pos: [-4, 3, -2] },
    { color: 0xccddff, intensity: 40, pos: [0, -3, 4] },
    { color: 0xffffee, intensity: 30, pos: [2, -1, -5] },
];

for (const l of lights) {
    const pl = new THREE.PointLight(l.color, l.intensity, 30, 2);
    pl.position.set(...l.pos);
    scene.add(pl);
}

// Soft fill
const ambient = new THREE.AmbientLight(0xffffff, 0.15);
scene.add(ambient);

// ---------------------------------------------------------------
// Gem
// ---------------------------------------------------------------
const geometry = await createBrilliantGeometry("data/brilliant-cut.json");

// Scale the gem up a bit so it's nicely visible
geometry.scale(1.2, 1.2, 1.2);

const material = createGemMaterial(envTexture, "diamond");
const gem = new THREE.Mesh(geometry, material);
gem.position.set(0, 0.3, 0);
scene.add(gem);

// ---------------------------------------------------------------
// Controls
// ---------------------------------------------------------------
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.06;
controls.minDistance = 2;
controls.maxDistance = 12;
controls.target.copy(gem.position);
controls.enablePan = false;

// Slow auto-rotate so the gem is never static — user orbit overrides
controls.autoRotate = true;
controls.autoRotateSpeed = 1.5;

// ---------------------------------------------------------------
// Post-processing: Bloom (specular sparkle / scintillation)
// ---------------------------------------------------------------
const composer = new EffectComposer(renderer);
composer.addPass(new RenderPass(scene, camera));

const bloomPass = new UnrealBloomPass(
    new THREE.Vector2(window.innerWidth, window.innerHeight),
    0.30,  // strength — subtle glow on bright highlights
    0.4,   // radius
    0.85   // threshold — only the brightest specular hits bloom
);
composer.addPass(bloomPass);

composer.addPass(new OutputPass());

// ---------------------------------------------------------------
// Resize handling
// ---------------------------------------------------------------
window.addEventListener("resize", () => {
    const w = window.innerWidth;
    const h = window.innerHeight;
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    renderer.setSize(w, h);
    composer.setSize(w, h);
});

// ---------------------------------------------------------------
// Gemstone cycling
// ---------------------------------------------------------------
let currentGemIndex = 0;

function switchGem(index) {
    currentGemIndex = ((index % GEM_ORDER.length) + GEM_ORDER.length) % GEM_ORDER.length;
    const key = GEM_ORDER[currentGemIndex];
    applyGemPreset(material, key);

    // Update the name display
    const nameEl = document.getElementById("gem-name");
    if (nameEl) nameEl.textContent = GEM_PRESETS[key].label;

    // Sync sliders to reflect new material values
    syncSliders();
}

document.getElementById("gem-prev")?.addEventListener("click", () => {
    switchGem(currentGemIndex - 1);
});
document.getElementById("gem-next")?.addEventListener("click", () => {
    switchGem(currentGemIndex + 1);
});

// Arrow-key cycling (when not focused on an input)
window.addEventListener("keydown", (e) => {
    if (e.target.tagName === "INPUT" || e.target.tagName === "TEXTAREA") return;
    if (e.key === "ArrowLeft") {
        e.preventDefault();
        switchGem(currentGemIndex - 1);
    } else if (e.key === "ArrowRight") {
        e.preventDefault();
        switchGem(currentGemIndex + 1);
    }
});

// ---------------------------------------------------------------
// Settings panel
// ---------------------------------------------------------------
initSettingsUI({ bloomPass, renderer, material });

// ---------------------------------------------------------------
// Animation loop
// ---------------------------------------------------------------
function animate() {
    requestAnimationFrame(animate);

    // Gentle idle spin on the gem itself (Y axis)
    gem.rotation.y += 0.003;

    controls.update();
    composer.render();
}

animate();
