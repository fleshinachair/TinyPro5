/*
 * TinyPro5 - Move OLED UI (128x64, 1-bit). Four pages of eight encoders.
 *
 *   Left/Right (CC 62/63) or Track buttons (CC 43..40) switch pages.
 *   Encoders 1-8 = CC 71-78 (relative). Page 4 (Voice+Sys) holds the voice and preset controls: turning Preset (E5) or the
 *   jog wheel pops up a scrolling list of names and loads the highlighted program as you go. The list is in bank
 *   order ("group-program name", four groups of eight) or sorted by name (the same labels, so the address stays visible); a jog
 *   click toggles the order and keeps the current program selected. Reload (E6) reloads it, discarding edits;
 *   Save (E7) saves your edit over it. (Page 4 has no spare encoder, so the sort toggle lives on the jog click.)
 *
 * Self-contained: uses only the documented host globals (clear_screen, print,
 * fill_rect, host_module_get_param, host_module_set_param) and no imports, so it
 * does not depend on where the host places the shared helper modules.
 *
 * Labels are all < 8 characters. Each entry: [key, label, min, max, kind]
 *   kind: "pct" 0..1 shown 0-99 | "bi" -1..1 shown -99..99 | "int" | "semi" | "bool" | "enum:<a|b|..>"
 */
const PAGES = [
    { name: "Filter", knobs: [
        ["cutoff", "Cutoff", 0, 1, "pct"], ["resonance", "Reso", 0, 1, "pct"],
        ["env_amt", "EnvAmt", -1, 1, "bi"], ["kbd_track", "KbdTrk", 0, 1, "pct"],
        ["f_attack", "FAtk", 0, 1, "pct"], ["f_decay", "FDecay", 0, 1, "pct"],
        ["f_sustain", "FSus", 0, 1, "pct"], ["f_release", "FRel", 0, 1, "pct"] ] },
    { name: "Osc", knobs: [
        ["osca_wave", "AWave", 0, 1, "pct"], ["osca_pw", "APW", 0, 1, "pct"],
        ["oscb_wave", "BWave", 0, 1, "pct"], ["oscb_pw", "BPW", 0, 1, "pct"],
        ["oscb_pitch", "BPitch", -48, 60, "semi"], ["osc_sync", "Sync", 0, 1, "bool"],
        ["pm_filenv", "PM-Fenv", 0, 1, "pct"], ["pm_oscb", "PM-OscB", 0, 1, "pct"] ] },
    { name: "Amp+Mix", knobs: [
        ["attack", "AAtk", 0, 1, "pct"], ["decay", "ADecay", 0, 1, "pct"],
        ["sustain", "ASus", 0, 1, "pct"], ["release", "ARel", 0, 1, "pct"],
        ["osca_vol", "AVol", 0, 1, "pct"], ["oscb_vol", "BVol", 0, 1, "pct"],
        ["noise", "Noise", 0, 1, "pct"], ["master_vol", "Volume", 0, 1, "pct"] ] },
    // Page 4 is dials plus the preset scroller: turning Preset (E5) or the jog wheel pops up the name list.
    { name: "Voice+Sys", presetKnob: 4, knobs: [
        ["voice_mode", "Mode", 0, 3, "enum:Poly|Unison|Mono|Legato"],
        ["voice_count", "Voices", 1, 5, "int"],
        ["detune", "Detune", 0, 1, "pct"], ["lfo_amount", "LFOAmt", 0, 1, "pct"],
        ["preset", "Preset", 0, 199, "int"], ["load_preset", "Reload", 0, 1, "action"],
        ["save_preset", "Save", 0, 1, "action"], ["preset_sort", "Order", 0, 1, "enum:Bank|A-Z"] ] },
];

const CC_KNOB_FIRST = 71;      // knobs 1-8 = CC 71..78
const CC_JOG = 14, CC_JOG_CLICK = 3, CC_LEFT = 62, CC_RIGHT = 63;
const CC_TRACK = [43, 42, 41, 40];   // Track 1..4 -> pages 1..4

let page = 0;
let dirty = true;
let ticks = 0;
let toast = "";
let toastTicks = 0;
const cache = {};
const accum = {};

function num(s, fallback) {
    const v = parseFloat(s);
    return Number.isFinite(v) ? v : fallback;
}

function readParam(key) {
    try {
        return num(host_module_get_param(key), cache[key] === undefined ? 0 : cache[key]);
    } catch (e) {
        return cache[key] === undefined ? 0 : cache[key];
    }
}

function writeParam(key, v) {
    cache[key] = v;
    try { host_module_set_param(key, String(v)); } catch (e) { /* host not ready */ }
}

function syncPage() {
    for (const k of PAGES[page].knobs) if (k) cache[k[0]] = readParam(k[0]);
    dirty = true;
}

function showToast(text) {
    toast = text;
    toastTicks = 50;
    dirty = true;
}

function fmt(k) {
    const v = cache[k[0]] === undefined ? 0 : cache[k[0]];
    const kind = k[4];
    if (kind === "pct") return String(Math.round(Math.min(0.99, Math.max(0, (v - k[2]) / (k[3] - k[2]))) * 100));
    if (kind === "bi") return String(Math.round(v * 99));
    if (kind === "semi") return (v > 0 ? "+" : "") + v.toFixed(0);
    if (kind === "bool") return v > 0.5 ? "On" : "Off";
    if (kind === "action") return "->";
    if (kind === "bank") return bankName.slice(0, 5);                       // the active bank (the list names them in full)
    if (kind === "int") {
        if (k[0] !== "preset") return String(Math.round(v));
        const i = Math.round(v);
        return String(i + 1);                                         // position in the list (the overlay shows the names)
    }
    if (kind.indexOf("enum:") === 0) {
        const names = kind.slice(5).split("|");
        return names[Math.max(0, Math.min(names.length - 1, Math.round(v)))];
    }
    return "";
}

function turn(idx, delta) {
    const k = PAGES[page].knobs[idx];
    if (!k || delta === 0) return;
    if (idx === PAGES[page].presetKnob) {    // the Preset encoder scrolls the name list
        accum.list = (accum.list || 0) + delta * (Math.abs(delta) > 3 ? 4 : 1) / 2;   // fast turns skip through the programs
        const steps = Math.trunc(accum.list);
        accum.list -= steps;
        scrollPresets(steps);
        return;
    }
    const key = k[0], lo = k[2], hi = k[3], kind = k[4];
    let v = cache[key] === undefined ? readParam(key) : cache[key];

    if (kind === "bank") {                   // the Bank encoder opens the bank list; a jog click loads the highlighted bank
        accum.bank = (accum.bank || 0) + delta / 3;
        const steps = Math.trunc(accum.bank);
        accum.bank -= steps;
        scrollBanks(steps);
        return;
    }

    if (kind === "action") {                 // Load / Save fire on a clockwise nudge
        if (delta > 0) {
            writeParam(key, 1);
            showToast((key === "load_preset" ? "Reloaded " : "Saved ") + presetName());
            syncPage();
        }
        return;
    }
    if (kind === "bool") {
        v = delta > 0 ? 1 : 0;
    } else if (kind === "int" || kind.indexOf("enum:") === 0) {
        accum[key] = (accum[key] || 0) + delta / 3;       // ~3 detents per step
        const steps = Math.trunc(accum[key]);
        if (steps === 0) return;
        accum[key] -= steps;
        v = Math.max(lo, Math.min(hi, Math.round(v) + steps));
    } else {
        const accel = Math.abs(delta) > 3 ? 3 : 1;
        v = Math.max(lo, Math.min(hi, v + delta * accel * (hi - lo) / 160));
    }
    writeParam(key, v);
    dirty = true;
}

function presetName() {
    try { return String(host_module_get_param("preset_name") || ""); } catch (e) { return ""; }
}

// ---- scrolling preset list (page 4) ----------------------------------------------------------------
let labels = [];                         // list entry per position in the current order, fetched from the DSP
let sortMode = 0;                        // 0 = bank order, 1 = alphabetical (the DSP's preset_sort)
let bankCount = 1;                       // 1 factory bank + any custom banks the DSP found in banks/
let bankName = "Factory";
let presetCount = 32;
let presetSel = 0;
let listTicks = 0;                       // the name list stays on screen this many ticks after the last scroll

function presetLabel(i) {
    if (labels[i] === undefined) {
        let s = "";
        try { s = String(host_module_get_param("preset_name:" + i) || ""); } catch (e) { s = ""; }
        labels[i] = s || ("Preset " + (i + 1));
    }
    return labels[i];
}

function loadPresetList() {
    const sm = Math.round(readParam("preset_sort"));
    if (sm !== sortMode) { sortMode = sm; labels = []; }
    bankCount = Math.max(1, Math.round(readParam("bank_count")));
    try { bankName = String(host_module_get_param("bank_name") || "Factory"); } catch (e) { bankName = "Factory"; }
    const n = Math.round(readParam("preset_count"));
    if (n > 0) presetCount = n;
    presetSel = Math.max(0, Math.min(presetCount - 1, Math.round(readParam("preset"))));
}

function scrollPresets(steps) {
    if (!steps) return;
    const next = Math.max(0, Math.min(presetCount - 1, presetSel + steps));
    if (next === presetSel) return;
    presetSel = next;
    writeParam("preset", presetSel);      // the DSP loads the program as soon as it is selected
    listTicks = 90;                       // ~1.5 s at the 60 Hz tick
    dirty = true;
}

function drawPresetList() {
    const rows = 5, top = Math.max(0, Math.min(presetCount - rows, presetSel - 2));
    const count = String(presetSel + 1) + "/" + presetCount;
    drawText(0, 0, bankName.slice(0, 14) + (sortMode ? " A-Z" : " order"));
    drawText(128 - 6 * count.length, 0, count);
    fill_rect(0, 9, 128, 1, 1);
    for (let r = 0; r < rows && top + r < presetCount; r++) {
        const y = 12 + r * 9, i = top + r;
        const text = presetLabel(i).slice(0, 21);
        if (i === presetSel) { fill_rect(0, y - 1, 128, 9, 1); print(2, y, text, 0); }
        else print(2, y, text, 1);
    }
    fill_rect(0, 55, 128, 1, 1);
    drawText(0, 57, toastTicks > 0 ? toast : "E6 reload  jog:sort");
}

// ---- bank list (Bank encoder): pick from the banks the DSP found in banks/, click the jog wheel to load ---------------
let bankScreenTicks = 0;                 // the bank list stays up this long after the last turn
let bankSel = 0;
const bankInfo = [];                     // [name, size] per bank, fetched when the list opens

function openBankList() {
    bankInfo.length = 0;
    loadPresetList();                    // refreshes bankCount and the active bank's name
    for (let i = 0; i < bankCount; i++) {
        let nm = "", sz = 0;
        try { nm = String(host_module_get_param("bank_name:" + i) || ""); sz = Math.round(num(host_module_get_param("bank_size:" + i), 0)); } catch (e) { /* host not ready */ }
        bankInfo.push([nm || ("Bank " + (i + 1)), sz]);
    }
}

function scrollBanks(steps) {
    if (bankScreenTicks === 0) { openBankList(); bankSel = Math.max(0, Math.min(bankCount - 1, Math.round(readParam("bank")))); }
    bankScreenTicks = 300;               // ~5 s
    listTicks = 0;
    bankSel = Math.max(0, Math.min(bankCount - 1, bankSel + steps));
    dirty = true;
}

function confirmBank() {
    const target = bankSel;
    bankScreenTicks = 0;
    writeParam("bank", target);          // the DSP loads the bank's first program
    bankChanged();
}

function drawBankList() {
    const rows = 5, top = Math.max(0, Math.min(Math.max(0, bankCount - rows), bankSel - 2));
    const active = Math.round(readParam("bank"));
    const count = String(bankSel + 1) + "/" + bankCount;
    drawText(0, 0, "TP5 Banks");
    drawText(128 - 6 * count.length, 0, count);
    fill_rect(0, 9, 128, 1, 1);
    for (let r = 0; r < rows && top + r < bankCount; r++) {
        const y = 12 + r * 9, i = top + r, info = bankInfo[i] || ["", 0];
        const size = String(info[1]);
        const text = (i === active ? "*" : " ") + (i + 1) + " " + info[0].slice(0, 21 - 4 - size.length - String(i + 1).length);
        if (i === bankSel) { fill_rect(0, y - 1, 128, 9, 1); print(2, y, text, 0); print(126 - 6 * size.length, y, size, 0); }
        else { print(2, y, text, 1); print(126 - 6 * size.length, y, size, 1); }
    }
    fill_rect(0, 55, 128, 1, 1);
    drawText(0, 57, bankSel === active ? "* loaded   click:load" : "click: load this bank");
}

function bankChanged() {                  // the DSP loads the new bank's first program and resizes the list
    labels = [];
    loadPresetList();
    listTicks = 90;
    showToast("Bank: " + bankName.slice(0, 14));
}

function toggleSort() {
    writeParam("preset_sort", sortMode ? 0 : 1);   // the DSP keeps the current program selected and re-numbers the position
    labels = [];
    loadPresetList();
    listTicks = 90;
    showToast(sortMode ? "Order: A-Z" : "Order: bank");
}

function setPage(p) {
    page = (p + PAGES.length) % PAGES.length;
    bankScreenTicks = 0;
    if (PAGES[page].presetKnob !== undefined) loadPresetList();
    listTicks = 0;
    syncPage();
}

function drawText(x, y, s) { print(x, y, s, 1); }

function draw() {
    const pg = PAGES[page];
    clear_screen();
    if (bankScreenTicks > 0) { drawBankList(); return; }
    if (listTicks > 0) { drawPresetList(); return; }
    drawText(0, 0, "TP5 " + (page + 1) + "/" + PAGES.length + " " + pg.name);
    fill_rect(0, 9, 128, 1, 1);
    for (let row = 0; row < 4; row++) {
        const y = 13 + row * 11;
        const left = pg.knobs[row], right = pg.knobs[row + 4];
        if (left) { drawText(0, y, left[1]); const t = fmt(left); drawText(68 - 6 * t.length, y, t); }      // values right-aligned
        if (right) { drawText(72, y, right[1]); const t = fmt(right); drawText(128 - 6 * t.length, y, t); }
    }
    fill_rect(0, 55, 128, 1, 1);
    if (toastTicks > 0) drawText(0, 57, toast);
    else drawText(0, 57, (pg.presetKnob !== undefined ? ">" : "") + presetName());
}

globalThis.init = function () {
    page = 0;
    loadPresetList();
    syncPage();
};

globalThis.tick = function () {
    ticks++;
    if (toastTicks > 0 && --toastTicks === 0) dirty = true;
    if (listTicks > 0 && --listTicks === 0) dirty = true;
    if (bankScreenTicks > 0 && --bankScreenTicks === 0) dirty = true;      // no click: the list closes and nothing was loaded
    if (ticks % 30 === 0) {                 // follow preset loads / state restores
        if (PAGES[page].presetKnob !== undefined && listTicks === 0) loadPresetList();
        syncPage();
    }
    if (dirty) { draw(); dirty = false; }
};

globalThis.onMidiMessageInternal = function (data) {
    const status = data[0] & 0xF0, d1 = data[1], d2 = data[2];
    if (status === 0xB0) {
        if (d1 >= CC_KNOB_FIRST && d1 < CC_KNOB_FIRST + 8) {
            turn(d1 - CC_KNOB_FIRST, d2 < 64 ? d2 : d2 - 128);
        } else if (d2 > 0 && d1 === CC_LEFT) {
            setPage(page - 1);
        } else if (d2 > 0 && d1 === CC_RIGHT) {
            setPage(page + 1);
        } else if (d2 > 0 && CC_TRACK.indexOf(d1) >= 0) {
            setPage(CC_TRACK.indexOf(d1));
        } else if (d1 === CC_JOG && bankScreenTicks > 0) {
            scrollBanks(d2 < 64 ? 1 : -1);
        } else if (d1 === CC_JOG && PAGES[page].presetKnob !== undefined) {
            const delta = d2 < 64 ? d2 : d2 - 128;
            scrollPresets((delta > 0 ? 1 : -1) * (Math.abs(delta) > 3 ? 8 : 1));   // fast spin jumps a bank
        } else if (d1 === CC_JOG_CLICK && d2 > 0 && bankScreenTicks > 0) {
            confirmBank();
        } else if (d1 === CC_JOG_CLICK && d2 > 0 && PAGES[page].presetKnob !== undefined) {
            toggleSort();
        }
    }
};

globalThis.onMidiMessageExternal = function () { /* notes go straight to the DSP */ };
