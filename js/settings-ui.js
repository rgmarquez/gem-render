/**
 * settings-ui.js
 *
 * Data-driven settings panel.  Reads slider definitions from the DOM
 * (`data-target`, `data-prop`, `data-default`) and binds them to the
 * live Three.js objects passed in via `initSettingsUI()`.
 *
 * Zero knowledge of Three.js internals — it just sets numeric
 * properties on the objects it receives.
 */

// ---------------------------------------------------------------
// Public API
// ---------------------------------------------------------------

/** @type {Map<string, {slider: HTMLInputElement, display: HTMLSpanElement}>} */
let sliderMap = new Map();

/** @type {Object|null} */
let boundTargets = null;

/**
 * Initialise the settings panel.
 *
 * @param {Object} targets - Mutable objects keyed by the names used
 *     in each slider's `data-target` attribute.
 *     Example: { bloomPass, renderer, material }
 */
export function initSettingsUI(targets) {
    boundTargets = targets;
    const panel   = document.getElementById("settings-panel");
    const toggle  = document.getElementById("settings-toggle");
    const sliders = panel.querySelectorAll('input[type="range"]');

    // -- Toggle panel open/closed ------------------------------------
    function setPanelOpen(open) {
        panel.classList.toggle("open", open);
        toggle.classList.toggle("active", open);
        toggle.setAttribute("aria-expanded", open);
    }

    toggle.addEventListener("click", () => {
        setPanelOpen(!panel.classList.contains("open"));
    });

    // Keyboard shortcut: 'S' when not focused on an input
    window.addEventListener("keydown", (e) => {
        if (e.target.tagName === "INPUT" || e.target.tagName === "TEXTAREA") return;
        if (e.key === "s" || e.key === "S") {
            e.preventDefault();
            setPanelOpen(!panel.classList.contains("open"));
        }
    });

    // -- Bind each slider to its target property ---------------------
    for (const slider of sliders) {
        const targetName  = slider.dataset.target;   // e.g. "bloomPass"
        const prop        = slider.dataset.prop;      // e.g. "strength"
        const defaultVal  = parseFloat(slider.dataset.default);
        const display     = slider.parentElement.querySelector(".slider-value");
        const targetObj   = targets[targetName];

        // Register in the lookup map for external sync
        const key = `${targetName}.${prop}`;
        sliderMap.set(key, { slider, display });

        if (!targetObj) {
            console.warn(`[settings-ui] Unknown target "${targetName}"`);
            continue;
        }

        // Set initial slider position from the live value (or the default)
        const initial = targetObj[prop] ?? defaultVal;
        slider.value = initial;
        if (display) display.textContent = formatValue(initial, slider.step);

        // Live update on drag
        slider.addEventListener("input", () => {
            const val = parseFloat(slider.value);
            targetObj[prop] = val;
            if (display) display.textContent = formatValue(val, slider.step);
        });
    }

    // -- Reset buttons per group -------------------------------------
    const resetButtons = panel.querySelectorAll(".group-reset");
    for (const btn of resetButtons) {
        btn.addEventListener("click", () => {
            const group = btn.closest(".settings-group");
            if (!group) return;

            for (const slider of group.querySelectorAll('input[type="range"]')) {
                const defaultVal = parseFloat(slider.dataset.default);
                const targetObj  = targets[slider.dataset.target];
                const prop       = slider.dataset.prop;
                const display    = slider.parentElement.querySelector(".slider-value");

                slider.value = defaultVal;
                if (targetObj) targetObj[prop] = defaultVal;
                if (display) display.textContent = formatValue(defaultVal, slider.step);
            }
        });
    }
}

// ---------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------

/**
 * Format a numeric value for display, matching the slider's step
 * precision so we don't show excess decimals.
 */
function formatValue(val, step) {
    const decimals = stepDecimals(parseFloat(step));
    return val.toFixed(decimals);
}

/** Count the decimal places in a step value (e.g. 0.001 → 3). */
function stepDecimals(step) {
    const s = step.toString();
    const dot = s.indexOf(".");
    return dot === -1 ? 0 : s.length - dot - 1;
}

// ---------------------------------------------------------------
// External sync — call after changing material properties from
// outside the panel (e.g. gemstone preset switch).
// ---------------------------------------------------------------

/**
 * Re-read the live values from the bound target objects and update
 * every slider + display to match.
 */
export function syncSliders() {
    if (!boundTargets) return;
    for (const [key, { slider, display }] of sliderMap) {
        const [targetName, prop] = key.split(".");
        const obj = boundTargets[targetName];
        if (!obj) continue;
        const val = obj[prop];
        if (val == null) continue;
        slider.value = val;
        if (display) display.textContent = formatValue(val, slider.step);
    }
}
