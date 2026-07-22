# TX81Z VST3 Emulator

A VST3 plugin that emulates the Yamaha TX81Z FM tone generator (1987) by
running its **real firmware** on a from-scratch, standalone port of the
hardware it originally shipped on - not a reimplementation of the synthesis
algorithm, an emulation of the actual machine.

## How it works

Unlike a typical "clean-room" synth emulator, this project ports the exact
hardware MAME's own `ymtx81z.cpp` driver describes:

- **CPU**: a from-scratch HD6303X (6800/6801/HD63701 family) instruction-set
  interpreter, including the on-chip timer, ports, and the SCI/UART used for
  real MIDI I/O - ported by reading MAME's `src/devices/cpu/m6800/*.cpp` as
  the behavioral reference (opcode tables cross-checked by automated diff
  against the original, not just eyeballed).
- **Sound chip**: the real YM2414 (OPZ) FM chip, via
  [ymfm](https://github.com/aaronsgiles/ymfm) (Aaron Giles' standalone,
  BSD-3-Clause chip-emulation library - the same code MAME itself uses).
- **LCD**: a from-scratch port of MAME's HD44780 controller logic.
- **Memory map**: banked ROM, battery-backed NVRAM, and the chip/LCD address
  decoding all match the real board (`src/mame/yamaha/ymtx81z.cpp` in MAME).

Booting real firmware against this engine reproduces the actual TX81Z boot
banner ("YAMAHA TX81Z / Good morning!!") character-for-character, and real
MIDI Note On/Off messages are bit-banged into the emulated UART exactly like
a physical MIDI cable would.

## You need your own ROM + NVRAM dump

**No firmware or memory dumps are included in this repository** - they're
copyrighted Yamaha firmware / your own device's data. To use this, you need:

1. A TX81Z firmware ROM dump (64KB, any of versions 1.0-1.6).
2. An NVRAM dump (8KB) from a real, factory-reset unit - a blank/all-zero
   NVRAM boots into a cold-start "Utility Mode" screen and won't actually
   play notes until it's been through the real unit's own memory-format
   procedure, so a real dump is the practical way to get a working
   instrument bank.

Drop both files into `C:\Program Files\Common Files\VST3\TX81Z Data\` (the
plugin finds them by file size, not name) before loading the plugin.

## Project layout

- `engine/` - the standalone C++ engine (CPU core, memory map, LCD, ymfm
  integration), with its own CMake build and test/diagnostic console tools
  (`tools/boot_rom.cpp`, `tools/render_note.cpp`, `tools/direct_opz_test.cpp`).
- `plugin/` - the JUCE-based VST3 plugin wrapping the engine, with a custom
  front-panel GUI matching the real hardware.

## Building

```
cmake -G "Visual Studio 17 2022" -A x64 -S plugin -B plugin/build
cmake --build plugin/build --config Release --target Tx81zEmulator_VST3
```

JUCE is fetched automatically via CMake `FetchContent`.
