// Drives src/canvas.js (the Reload / Save / Revert "are you sure?" screens) with a fake host context.
// Run: node tests/test_canvas.js   (scripts/test.sh runs it when node is installed)
"use strict";
const fs = require("fs");
const vm = require("vm");

let checks = 0, failed = 0;
function check(cond, msg) { checks++; if (!cond) { failed++; console.log("    FAIL " + msg); } }

function load() {
    const sandbox = { globalThis: null };
    sandbox.globalThis = sandbox;
    vm.createContext(sandbox);
    vm.runInContext(fs.readFileSync("src/canvas.js", "utf8"), sandbox, { filename: "canvas.js" });
    return sandbox.canvas_overlays;
}

function fakeCtx(params) {
    const writes = [];
    const drawn = [];
    const ctx = {
        width: 128, height: 64, state: {}, closed: false,
        clear() { drawn.length = 0; },
        print(x, y, t, c) { drawn.push({ x, y, t: String(t), c }); },
        fillRect() {}, drawRect() {},
        getParam(k) { return Object.prototype.hasOwnProperty.call(params, k) ? params[k] : null; },
        setParam(k, v) { writes.push([k, String(v)]); return true; },
        close() { ctx.closed = true; return true; },
    };
    return { ctx, writes, drawn };
}

const cc = (n, v) => ({ source: "internal", data: [0xB0, n, v] });
const JOG = 14, CLICK = 3;
const overlays = load();

check(overlays && ["confirm_reload", "confirm_save", "confirm_revert"].every((n) => overlays[n]), "three overlays are exported");

const cases = [
    ["confirm_reload", "load_preset", "can_reload"],
    ["confirm_save", "save_preset", "can_save"],
    ["confirm_revert", "revert_preset", "can_revert"],
];
for (const [name, key, can] of cases) {
    const ov = overlays[name];

    // 1. opens on No: a click straight away does nothing but close
    {
        const f = fakeCtx({ preset_name: "1-1-1 Test", [can]: "1" });
        ov.onOpen(f.ctx, {});
        ov.draw(f.ctx);
        check(f.drawn.some((d) => d.t === "No") && f.drawn.some((d) => d.t === "Yes"), name + ": shows No and Yes");
        check(f.drawn.some((d) => d.t.indexOf("1-1-1 Test") === 0), name + ": shows the program name");
        ov.onMidi(f.ctx, cc(CLICK, 127));
        check(f.writes.length === 0 && f.ctx.closed, name + ": click on the default (No) closes without writing");
    }
    // 2. jog clockwise selects Yes, click writes 1 and closes
    {
        const f = fakeCtx({ preset_name: "x", [can]: "1" });
        ov.onOpen(f.ctx, {});
        ov.onMidi(f.ctx, cc(JOG, 1));
        ov.onMidi(f.ctx, cc(CLICK, 127));
        check(f.writes.length === 1 && f.writes[0][0] === key && f.writes[0][1] === "1" && f.ctx.closed, name + ": jog right + click = Yes writes " + key + "=1");
    }
    // 3. jog back to No cancels again
    {
        const f = fakeCtx({ preset_name: "x", [can]: "1" });
        ov.onOpen(f.ctx, {});
        ov.onMidi(f.ctx, cc(JOG, 1));
        ov.onMidi(f.ctx, cc(JOG, 127));
        ov.onMidi(f.ctx, cc(CLICK, 127));
        check(f.writes.length === 0, name + ": jog right then left ends on No");
    }
    // 4. the jog alone, a button release, other CCs and note messages never act
    {
        const f = fakeCtx({ preset_name: "x", [can]: "1" });
        ov.onOpen(f.ctx, {});
        ov.onMidi(f.ctx, cc(JOG, 1));
        ov.onMidi(f.ctx, cc(CLICK, 0));
        ov.onMidi(f.ctx, cc(71, 127));
        ov.onMidi(f.ctx, { source: "internal", data: [0x90, 60, 100] });
        ov.onMidi(f.ctx, {});
        ov.onMidi(f.ctx, null);
        check(f.writes.length === 0 && !f.ctx.closed, name + ": only a click acts (jog, release, knobs and notes do nothing)");
    }
    // 5. nothing to do: the screen says so and a click only closes, even after turning the jog
    {
        const f = fakeCtx({ preset_name: "x", [can]: "0" });
        ov.onOpen(f.ctx, {});
        ov.draw(f.ctx);
        check(f.drawn.some((d) => d.t === "OK") && !f.drawn.some((d) => d.t === "Yes"), name + ": unavailable shows OK, not Yes");
        ov.onMidi(f.ctx, cc(JOG, 1));
        ov.onMidi(f.ctx, cc(CLICK, 127));
        check(f.writes.length === 0 && f.ctx.closed, name + ": unavailable never writes");
    }
    // 6. long names are cut to the screen width; a failed read does not throw
    {
        const f = fakeCtx({ preset_name: "A very long program name that does not fit", [can]: "1" });
        ov.onOpen(f.ctx, {});
        ov.draw(f.ctx);
        check(f.drawn.every((d) => d.t.length <= 22), name + ": text fits the 128 px screen");
        const g = fakeCtx({});
        ov.onOpen(g.ctx, {});
        ov.draw(g.ctx);
        check(true, name + ": missing parameters do not throw");
    }
}

console.log("    " + checks + " checks, " + failed + " failed");
console.log("    RESULT: " + (failed ? "FAIL" : "PASS"));
process.exit(failed ? 1 : 0);
