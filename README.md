# fm1-amy: a Juno-60 synth for the M-VAVE FM-1

fm1-amy turns the M-VAVE FM-1 into a six-voice polyphonic analog-style synthesizer. It replaces
the FM-1's firmware with [AMY](https://github.com/shorepine/amy), a portable synthesizer library,
playing AMY's emulations of the 128 factory patches of the Roland Juno-60. Pick a patch with
the PRESETS knob, then shape it with the four knobs: filter, envelope, oscillator mix and LFO.

<!-- Photo: put it at docs/img/fm1-amy.jpg and replace this comment with:
![fm1-amy on the M-VAVE FM-1](docs/img/fm1-amy.jpg)
-->

![The fm1-amy screen: the VCF page, the ENV page, another patch, and the OSC page](docs/img/amy-screens.png)

> **fm1-amy is experimental.** It has been built, installed and played on one FM-1. Installing
> it replaces the FM-1's own firmware and is at your own risk. An FM-1 that keeps crashing at
> start-up drops into a safe mode from which other firmware can be installed, and M-VAVE's
> firmware can be put back (see [If something goes wrong](#if-something-goes-wrong)).

fm1-amy is built on [X0X](https://github.com/charlesvestal/fm1-x0x), Charles Vestal's groovebox
firmware for the FM-1, itself a fork of [Felucca](https://github.com/hugelton/Felucca) by Leo
Kuroshita (Hügelton Instruments). It keeps their foundations (the hardware layer, USB, the update
loader and installer, the safe mode and the build system) and replaces the instrument entirely:
none of X0X's groovebox is used.

## Playing it

| Control | What it does |
|---|---|
| Keys | Play the patch: 27 keys from F3 to G5, up to six notes at once |
| OCT- / OCT+ | Shift the keys down or up an octave (-2 to +2; shown at the top right of the screen) |
| PRESETS | Choose the patch, A11 to B88 (all 128 of the Juno-60's factory patches) |
| SELECT | Choose the page of knobs: VCF, ENV, OSC or LFO |
| KNOB1 to KNOB4 | Change the page's four settings (shown as K1 to K4 on the screen) |
| MASTER | Volume |

Choosing a patch puts every knob where that patch has it, so you start from the factory sound
and change it from there. Each knob has 128 steps across its range; frequencies and times move
logarithmically.

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| VCF | Cutoff (20 Hz to 20 kHz) | Resonance | Envelope amount (-12 to +12; negative closes the filter) | LFO amount |
| ENV | Attack (to 5 s) | Decay (to 150 s) | Sustain | Release (to 30 s) |
| OSC | Pulse level | Saw level | Sub-oscillator level | Noise level |
| LFO | LFO rate (0.1 to 30 Hz) | LFO pitch depth (vibrato) | Pulse width | Chorus level |

The envelope shapes the filter (by the envelope amount) and, in most patches, the volume, as on
the Juno.

What it doesn't do (yet):

- **Nothing is saved.** Your knob changes last until you choose another patch or switch off.
- **No MIDI in.** The FM-1 still shows up as a USB MIDI device (that's how firmware is installed),
  but notes and controllers sent to it aren't played.
- **No velocity.** Every note plays at full velocity.
- **Only the Juno patches.** AMY also has 128 DX7 patches, but they are left out to fit the
  FM-1's flash (see [How it works](#how-it-works)).

## Installing

There is no ready-made firmware file yet: [build it](#building) first, which gives
`build/amy_fw/x0x-amy.fwsc`. Then, with the FM-1 plugged in by USB and switched on:

```bash
pip3 install mido python-rtmidi
python3 tools/fm1_install.py --info                       # check the FM-1 is found, and what it runs
python3 tools/fm1_install.py build/amy_fw/x0x-amy.fwsc    # install
```

The installer reads the package and starts the FM-1's update loader, which writes the firmware,
restarts the FM-1 and checks what it runs afterwards. It works from M-VAVE's own firmware
(it was first installed over M-VAVE's V15, which identifies as `FM-1_015`) and from X0X.
Don't unplug the FM-1 or switch it off while it writes. Afterwards the FM-1
reports `FM-1_900`, the identity of a development build, and its USB MIDI port is named
"X0X FM-1".

To go back to X0X, install its firmware from [X0X's web installer](https://charlesvestal.github.io/fm1-x0x/install/),
which can also put M-VAVE's firmware back.

## If something goes wrong

These come from X0X and Felucca, and still work in fm1-amy:

- **Crash:** the screen turns red, shows where the crash happened, and the FM-1 restarts by itself.
  If you report a crash, include that address and what you were doing.
- **Safe mode:** if the firmware crashes twice in a row while starting, the FM-1 starts in safe
  mode instead: no sound, but USB works, so the installer (or X0X's web installer) can reach it.
- **Update mode:** hold OCT- and OCT+ together for five seconds. fm1-amy doesn't show X0X's
  countdown on its screen, but the FM-1 still enters update mode, where an install can finish.
- **Rescue:** if even safe mode fails twice, the chip drops into its own update mode, which only
  `tools/fm1_rescue.sh` can reach (from a Mac). It puts M-VAVE's V15 firmware back; see "Recovering
  an FM-1 that won't start" in [X0X's manual](docs/MANUAL.md).

## Building

fm1-amy builds with X0X's build system and JieLi's toolchain for the FM-1's chip (an AC791N with
a pi32v2 CPU). [BUILDING.md](BUILDING.md) has the details; in short, on a Mac you need:

- **Python 3** with Pillow: `pip3 install Pillow`
- **Docker**, to run JieLi's toolchain, which exists only for x86-64 Linux. Docker Desktop works,
  or Colima: `brew install colima docker && colima start --vm-type vz --vz-rosetta` (Rosetta runs
  the x86-64 toolchain on Apple silicon). On x86-64 Linux the toolchain runs without Docker.
- **JieLi's toolchain**: `tools/get_toolchain.sh` installs it to `~/.jieli/toolchain`.
- **JieLi's AC79 SDK** (Apache-2.0), from which the package takes three files. Clone it to
  `~/fw-AC79_AIoT_SDK`:
  ```bash
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```
  If gitee.com can't be reached from your network, a GitHub mirror of the same tag works: the
  build checks the three files against their SHA-256 hashes.
- **AMY**: a checkout of [shorepine/amy](https://github.com/shorepine/amy), unmodified. fm1-amy
  was developed against AMY 1.2.200 (commit `c4433d4f`).
  ```bash
  git clone https://github.com/shorepine/amy.git ~/amy
  ```

Then build:

```bash
X0X_AMY=1 AMY_DIR=~/amy ./build.sh        # build/amy_fw/x0x-amy.fwsc
```

`X0X_AMY_PATCH=N` chooses the patch at power-on (0 to 127, default 0 = A11 Brass Set 1).
The build checks the result as X0X's does and fails if anything is out of bounds:

```
  ok    image 304208 B; RAM .data+.bss 77968 B of 98304; pool 233936 B of 344064
```

Without `X0X_AMY=1`, `./build.sh` still builds X0X's groovebox (into `build/`), since this tree
carries X0X's sources unchanged: the AMY build swaps its engine and screen in at build time.

## Trying it without an FM-1

The same code also builds for a computer, which is how most of fm1-amy was developed:

```bash
AMY_DIR=~/amy sh host/build_host_amy.sh                          # build/host/x0x_amy_host
build/host/x0x_amy_host host/amy_juno.x0x OUTDIR                 # runs a script, writes WAVs and screenshots
```

The simulator runs the whole firmware against a simulated FM-1 driven by a script of key presses
and knob turns (`host/amy_*.x0x`; the commands are listed at the top of `host/x0x_host.c`), and
writes the audio and the screen to files. At the end it prints how many instructions per sample
the synth took, a guide to whether a change still fits the FM-1's CPU.

In a browser:

```bash
AMY_DIR=~/amy sh web/emu/build_amy.sh                            # needs Emscripten (brew install emscripten)
python3 -m http.server 8000 --directory build/emu_amy            # then open http://localhost:8000/
```

That's X0X's browser emulator running fm1-amy: click the knobs and keys on the picture of the
FM-1, use the mouse wheel over a knob to turn it, or play the white keys from the computer
keyboard (`q` to `i`, `a` to `k`). The page's text still describes X0X.

## How it works

**AMY in place of X0X's engines.** X0X renders its parts in the FM-1's audio interrupt, 256
stereo frames at 44.1 kHz at a time. fm1-amy hands that same call to AMY
(`firmware/src/app/engine_amy.c`), which renders one 256-sample block of synth 1: a Juno patch
with six voices. Everything that touches AMY runs in that interrupt: the keys are read there and
become note messages, and the screen's knob changes reach it through a queue, so the main loop
(the screen, USB) never races the synth. The AMY build leaves out X0X's `engine.c` and its 909,
808, 303 and break engines; `engine_amy.c` stands in for them.

**The screen** (`firmware/src/app/ui_amy.c`) replaces X0X's. It turns knob changes into AMY wire
messages for synth 1. A Juno voice in AMY is six oscillators (the filter and amplifier, the LFO,
then pulse, saw, sub and noise), and a message to the synth naming an oscillator changes that
oscillator in every voice. Each patch's starting values come from a table generated at build
time from AMY's own patch definitions (`tools/gen_amy_juno.py`).

**Fitting the FM-1.** The firmware has 568 KB of flash, 96 KB of RAM for the program's data and
a 336 KB pool for large buffers.

| | fm1-amy | X0X, for comparison |
|---|---|---|
| Firmware image | 304 KB | 480 KB |
| Program data (RAM) | 78 KB | 98 KB |
| Pool (fm1-amy: AMY's 160 KB heap, the screen's 69 KB canvas, backup buffers) | 234 KB | 336 KB |

To fit, `tools/prep_amy_src.py` copies AMY's sources into the build tree with only the 128 Juno
patches (no DX7 patches) and without AMY's built-in drum samples. AMY itself is not modified.
AMY needs about 128 KB of heap with six Juno voices and chorus.

**The C library.** JieLi's toolchain comes with a reduced C library that has neither `malloc`
nor `printf`. `firmware/src/app/amy_libc.c` provides them for the FM-1: a simple allocator over
the heap in the pool, and an integer-only `printf` family (AMY prints floating-point numbers only
in diagnostics, which the FM-1 doesn't show). The FM-1's FPU is single precision, and X0X's build
fails if any double-precision arithmetic is linked; the fm1-amy build allows only AMY's float to
double conversions, which happen in diagnostics and when a patch loads, never per sample.

**CPU.** Measured in the simulator (instructions per sample on a Mac: a comparison, not an FM-1
figure), six Juno voices take about 980, against about 1,150 for X0X playing its demo pattern,
which X0X splits across the FM-1's two cores; fm1-amy renders on one. The load on the FM-1
itself hasn't been measured yet.

## Next steps

- Save edited patches, and recall them.
- Play notes and controllers arriving over USB MIDI, and send the knobs out as MIDI.
- Show the audio load, and use the FM-1's second core.
- Bring back the DX7 patches, if they can be made to fit.
- In AMY: build options for leaving out patch banks and samples, and single-precision constants
  throughout, so neither the trimming step nor the build-check exception is needed.

## Credits

- [AMY](https://github.com/shorepine/amy), the synthesizer, by Brian Whitman and Dan Ellis
  (shorepine), including its emulation of the Juno-60 and its factory patches.
- [X0X](https://github.com/charlesvestal/fm1-x0x) by Charles Vestal, which fm1-amy is built on:
  its hardware support, simulator, browser emulator, installer and build checks are all here.
- [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments), the
  FM-1 firmware X0X is a fork of: the hardware layer, USB, the update loader and installer, and
  the storage.
- [FM-1-RE](https://github.com/AL-255/FM-1-RE), which documents the FM-1's chip, firmware and
  update protocol.
- X0X's use of the FM-1's second core follows keremimo's [Melodee](https://github.com/keremimo/melodee)
  and zednaked's [Jangada](https://github.com/zednaked/jangada).
- The screen font is Barlow Semi Condensed (The Barlow Project Authors, SIL OFL 1.1).

## Licence

fm1-amy is free software under the GNU General Public License, version 3 only
(`GPL-3.0-only`, full text in [LICENSE](LICENSE)), the licence of X0X and Felucca. If you
distribute the firmware, you must give your recipients its complete source under the same
licence. AMY is under the MIT licence and is compiled into the firmware from your AMY checkout;
it isn't part of this tree. [LICENSING.md](LICENSING.md) lists where each part of the code comes
from. The tree also still carries X0X's groovebox sources and its other assets, under their own
terms as listed there.

M-VAVE and FM-1 are trademarks of their owners; Juno-60 is a trademark of Roland Corporation,
used here only to describe the patches AMY emulates. fm1-amy isn't affiliated with or endorsed
by M-VAVE, Roland, Hügelton Instruments or the authors of X0X.
