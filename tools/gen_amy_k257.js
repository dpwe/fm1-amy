#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
// X0X_AMY: the Juno-60 presets as the AMYboard editor loads them, into tools/amy_k257.json.
//
//   node tools/gen_amy_k257.js TULIPCC_DIR [OUT.json]     write the snapshot (default tools/amy_k257.json)
//   node tools/gen_amy_k257.js TULIPCC_DIR --check        exit 1 if the snapshot is out of date
//
// The editor (amyboard.com/editor, tulipcc's tulip/amyboardweb) doesn't load a Juno patch as AMY's
// own 6-osc patch: its Load Preset loads AMYboard's 4-osc default, patch 257, and then sends wire
// messages that set it up like the Juno patch (spss.js get_wire_commands_for_juno_patch). This runs
// that code itself, unchanged, under Node: AMY's generated amy_api.generated.js (AMY, AMY_KW_MAP) and
// patches.generated.js (the patch strings), and spss.js's parse_wire_code,
// events_from_wire_code_message and get_wire_commands_for_juno_patch, cut out of it by name (the
// whole of spss.js needs a browser). The snapshot is committed, so building needs neither Node nor
// tulipcc; regenerate it when the editor's translation or AMY's patches change.
"use strict";
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const NJUNO = 128;
const BASE_PATCH = 257;                       // the editor's AMYboard default (4 oscs per voice)
const SYNTH = 1, VOICES = 6;                  // the FM-1's synth; handlePresetLoad's num_voices

function cut(src, name, file) {               // a top-level function's text, by brace matching
  const start = src.search(new RegExp("^function " + name + "\\(", "m"));
  if (start < 0) throw new Error(`${file}: no function ${name}`);
  let depth = 0, i = src.indexOf("{", start);
  for (; i < src.length; i++) {
    if (src[i] === "{") depth++;
    else if (src[i] === "}" && --depth === 0) return src.slice(start, i + 1);
  }
  throw new Error(`${file}: ${name} does not end`);
}

function generate(tulipcc) {
  const amy = path.join(tulipcc, "amy", "src");
  const spssFile = path.join(tulipcc, "tulip", "amyboardweb", "static", "spss.js");
  const quiet = { log() {}, warn() {}, error: console.error };   // amy_send warns that no AMY is loaded
  const ctx = { console: quiet };
  ctx.window = ctx;                           // the browser's window is its global object
  vm.createContext(ctx);
  for (const f of ["amy_api.generated.js", "patches.generated.js"])
    vm.runInContext(fs.readFileSync(path.join(amy, f), "utf8"), ctx, { filename: f });
  const spss = fs.readFileSync(spssFile, "utf8");
  for (const fn of ["parse_wire_code", "events_from_wire_code_message", "get_wire_commands_for_juno_patch"])
    vm.runInContext(cut(spss, fn, spssFile), ctx, { filename: "spss.js:" + fn });
  const presets = [];
  for (let n = 0; n < NJUNO; n++) {
    const name = String(ctx.amy_patches[n]);
    if (!/^Juno /.test(name)) throw new Error(`patch ${n} is not a Juno patch: ${name}`);
    presets.push({ name: name.replace(/^Juno /, ""),   // each then sent as "i<synth>" + message
                   messages: Array.from(ctx.get_wire_commands_for_juno_patch(n)) });
  }
  // handlePresetLoad's first message: the base patch on SYNTH (amy_send formats it; nothing sends it here)
  const base = String(ctx.amy_send({ synth: SYNTH, num_voices: VOICES, patch: BASE_PATCH }, true)).replace(/Z$/, "");
  return { generated_by: "tools/gen_amy_k257.js", base_patch: BASE_PATCH, base_message: base, synth: SYNTH,
           presets: presets };
}

const args = process.argv.slice(2);
if (args.length < 1) {
  console.error("usage: node tools/gen_amy_k257.js TULIPCC_DIR [OUT.json | --check]");
  process.exit(2);
}
const out = path.join(__dirname, "amy_k257.json");
const text = JSON.stringify(generate(args[0]), null, 1) + "\n";
if (args[1] === "--check") {
  const same = fs.existsSync(out) && fs.readFileSync(out, "utf8") === text;
  console.log(same ? "amy_k257.json: up to date" : "amy_k257.json: out of date (run without --check)");
  process.exit(same ? 0 : 1);
}
fs.writeFileSync(args[1] || out, text);
console.log(`amy_k257: ${NJUNO} presets -> ${args[1] || out}`);
