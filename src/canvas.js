/*
 * TinyPro5 "are you sure?" screens for Reload, Save and Revert.
 *
 * Each of the three controls is a `canvas` push button in module.json (canvas_script "canvas.js#confirm_save" etc.): a click opens
 * one of these full-screen dialogs, a knob turn does nothing, Back (or Shift+jog) cancels. The jog wheel picks No / Yes, the jog
 * click confirms. Only a click on Yes writes the parameter (the module fires on a write of 1). No is selected when the dialog opens.
 *
 * When there is nothing to do (nothing edited, nothing saved) the dialog says so and the only choice is OK.
 * The module answers `can_reload` / `can_save` / `can_revert` with "1" or "0" and `preset_name` with the program's label.
 */
(function () {
    "use strict";

    var CC_JOG = 14, CC_CLICK = 3;

    function fit(text, n) {
        text = String(text == null ? "" : text);
        return text.length > n ? text.slice(0, n - 1) + "~" : text;
    }

    function make(spec) {
        return {
            onOpen: function (ctx) {
                var st = ctx.state;
                st.sel = 0;                                           /* 0 = No, 1 = Yes: the safe answer is the one the dialog opens on */
                st.name = String(ctx.getParam("preset_name") || "");
                st.avail = String(ctx.getParam(spec.can)) !== "0";
                st.open = true;
            },

            draw: function (ctx) {
                var st = ctx.state, i, x, w = 40, y = 44;
                ctx.clear();
                ctx.print(2, 1, spec.title, 1);
                ctx.fillRect(0, 10, ctx.width, 1, 1);
                if (st.avail === false) {
                    ctx.print(2, 14, spec.none[0], 1);
                    ctx.print(2, 23, spec.none[1], 1);
                    ctx.fillRect(44, y, 40, 11, 1);
                    ctx.print(58, y + 2, "OK", 0);
                    ctx.print(2, 57, "Click or Back: close", 1);
                    return;
                }
                ctx.print(2, 14, spec.lines[0], 1);
                ctx.print(2, 23, spec.lines[1], 1);
                ctx.print(2, 33, fit(st.name, 21), 1);
                for (i = 0; i < 2; i++) {
                    x = i === 0 ? 14 : 74;
                    if (st.sel === i) {
                        ctx.fillRect(x, y, w, 11, 1);
                        ctx.print(x + (i === 0 ? 14 : 12), y + 2, i === 0 ? "No" : "Yes", 0);
                    } else {
                        ctx.drawRect(x, y, w, 11, 1);
                        ctx.print(x + (i === 0 ? 14 : 12), y + 2, i === 0 ? "No" : "Yes", 1);
                    }
                }
                ctx.print(2, 57, "Jog: pick  Click: OK", 1);
            },

            onMidi: function (ctx, msg) {
                var d = msg && msg.data, st = ctx.state;
                if (!d || d.length < 3 || (d[0] & 0xF0) !== 0xB0) return false;
                if (d[1] === CC_JOG && d[2] !== 0) {
                    if (st.avail !== false) st.sel = d[2] < 64 ? 1 : 0;       /* clockwise = Yes (right), counter-clockwise = No (left) */
                    return true;
                }
                if (d[1] === CC_CLICK && d[2] > 0) {
                    if (st.avail !== false && st.sel === 1) ctx.setParam(spec.key, "1");
                    ctx.close();
                    return true;
                }
                return false;
            }
        };
    }

    globalThis.canvas_overlays = {
        confirm_reload: make({
            key: "load_preset", can: "can_reload", title: "RELOAD PROGRAM?",
            lines: ["Discard knob changes", "and reload the sound."],
            none: ["Nothing to reload:", "no unsaved changes."]
        }),
        confirm_save: make({
            key: "save_preset", can: "can_save", title: "SAVE OVER PROGRAM?",
            lines: ["Overwrite the saved", "sound of this program."],
            none: ["Nothing to save:", "no unsaved changes."]
        }),
        confirm_revert: make({
            key: "revert_preset", can: "can_revert", title: "REVERT PROGRAM?",
            lines: ["Erase your saved edit,", "restore the original."],
            none: ["Nothing to revert:", "no saved edit."]
        })
    };
})();
