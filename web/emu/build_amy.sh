#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# X0X_AMY in the browser: web/emu/build.sh's emulator with AMY (github.com/shorepine/amy) making the
# sound and ui_amy.c on the screen (the Juno-60 patches on the knobs).
#   AMY_DIR=/path/to/amy [X0X_AMY_PATCH=0] [X0X_AMY_VOLUME=2] web/emu/build_amy.sh  ->  build/emu_amy/
# AMY builds as it does for the host simulator, not as AMY's own web build: with __EMSCRIPTEN__
# undefined, so it runs at 44.1 kHz (AMY's web build is 48 kHz; the page's AudioContext and every
# X0X timing are 44.1 kHz) and needs none of AMY's JavaScript (Web MIDI, file transfer: EM_JS),
# which this standalone wasm has no glue for.
set -e
cd "$(dirname "$0")/../.."
[ -n "${AMY_DIR:-}" ] && [ -f "$AMY_DIR/src/amy.h" ] || { echo "build_amy.sh: set AMY_DIR to an amy checkout"; exit 1; }
O=build/emu_amy/obj
mkdir -p build/gen "$O"
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
[ -f build/gen/x0x_drum_samples.h ] || python3 tools/gen_drum_samples.py build/gen/x0x_drum_samples.h >/dev/null
python3 tools/gen_words.py firmware/words.tsv web/words build/gen/ann_words.h >/dev/null
sh tools/gen_builtin_break.sh build/gen >/dev/null
python3 tools/gen_amy_juno.py "$AMY_DIR/src/patches.h" build/gen/amy_juno.h
AMY_SRC="algorithms amy envelope examples parse filters oscillators pcm interp_partials custom delay
         log2_exp2 patches transfer sequencer instrument amy_midi api midi_mappings cv_trigger note_output"
for f in $AMY_SRC; do
    emcc -O2 -ffp-contract=off -w -U__EMSCRIPTEN__ -DAMY_NO_MINIAUDIO -DAMY_WAVETABLE -I"$AMY_DIR/src" \
        -c "$AMY_DIR/src/$f.c" -o "$O/$f.o"
done
D="-DX0X_AMY -DX0X_AMY_PATCH=${X0X_AMY_PATCH:-0} -DX0X_AMY_VOLUME=${X0X_AMY_VOLUME:-2}"
# shellcheck disable=SC2086
emcc -O2 -ffp-contract=off -std=gnu99 -Wall -Wno-unused-parameter -U__EMSCRIPTEN__ -DAMY_NO_MINIAUDIO \
    -DX0X_HOST -DX0X_WEB $D -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen -I"$AMY_DIR/src" \
    -c firmware/src/app/engine_amy.c -o "$O/engine_amy.o"
# engine_amy.c in place of engine.c and the DSP units (see engine.h)
U="firmware/src/seq/sequencer.c firmware/src/seq/tb3po.c firmware/src/seq/pattern.c firmware/src/seq/motion.c"
# shellcheck disable=SC2086
emcc -O2 -ffp-contract=off -std=gnu99 -Wall -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
    -DX0X_HOST -DX0X_WEB $D "-DX0X_VERSION=\"AMY\"" -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/emu_amy/x0x.wasm web/emu/x0x_web.c $U "$O"/*.o
cp web/emu/index.html web/emu/worklet.js web/emu/words.js build/emu_amy/
rm -rf build/emu_amy/words && cp -R web/words build/emu_amy/words   # the screen reader's words (gen_words.py)
echo "emu: build/emu_amy ($(wc -c < build/emu_amy/x0x.wasm) B wasm, AMY $(git -C "$AMY_DIR" describe --tags --always 2>/dev/null || echo '?'))"
