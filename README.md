# Munchi Tape

**CHOMPI TAPE 2.0 on Ableton Move.** A sampler you play like a keyboard, a tape
looper and a set of grubby effects, running the actual TAPE 2.0 firmware code.

Munchi Tape is a [Schwung](https://github.com/charlesvestal/schwung) standalone
tool. Launched from Schwung's Tools menu, it stops Move and runs the whole device
itself (pads, knobs, buttons, lights, screen, audio), the way the CHOMPI's
firmware runs its hardware. Press Back twice to hand Move back.
Its sibling is [Munchi Wave](https://github.com/charlesvestal/schwung-munchi-wave),
the CHOMPI's wavetable synth.

It is a port of the firmware CHOMPI Club released as open source
([CHOMPI-Club/CHOMPI](https://github.com/CHOMPI-Club/CHOMPI), MIT). It is not an
official CHOMPI Club release; see [Credits](#credits).

**→ [The manual](docs/MANUAL.md)**: a first session, every control, and how the
card works.

## At a glance

- **Keys mode:** one sample, played across a two-octave keyboard on the pads.
- **Kit mode:** every white key its own sample.
- 14 slots × 5 banks per mode, seven voices, and the full 84-sample factory card.
- **Buffer:** record from the mic, a line input, or Munchi Tape's own output, then play it.
- **Tape looper:** overdub, ±2x speed through reverse, hand scrubbing.
- **Effects:** DJ filter, saturation, tape warble, delay and reverb, before or after the looper.
- Each slot remembers its own speed, start/end, envelope, gain and pan.
- The samples folder, `presets.json` and `options.json` are in the CHOMPI's own
  formats, so they move between a CHOMPI's SD card and Move.

## Quick start

| Move | Does |
|---|---|
| Pads | the keyboard, C3–C5 as piano rows |
| Jog / steps 1–15 | choose a sample (Keys mode) |
| Knobs 1–8 | Speed, Start, End, Space, Lofi, Filter, Loop speed, Input |
| Left / Right | knobs 1–3 become Gain, Attack, Decay |
| Volume knob | Volume |
| Sample (hold) | record into the buffer |
| Loop / Play | the looper |
| Shift (hold) | the CHOMPI menu: modes, banks, inputs, save/copy/erase on the pads |
| Menu | settings |
| Back ×2 | exit |

The microphone is only monitored on headphones (on the speakers it would feed
back), but it always records. Choose Mic / Line / Resample with Shift + F#3 / G#3 /
A#3 or in Settings. Everything else is in the [manual](docs/MANUAL.md).

## Install

From the Schwung web manager, once it is in the catalog. Or from a build:

```bash
./scripts/build.sh      # fetches the factory card, cross-compiles in Docker
./scripts/install.sh    # copies dist/munchi-tape to the Move
```

Then Tools menu (Shift + Volume + Step 13) → **Munchi Tape**. The first launch
copies the factory card to `/data/UserData/UserLibrary/Samples/Schwung/Munchi Tape/`.

## How it is built

| Path | What |
|---|---|
| `src/engine/tape/` | the TAPE 2.0 engine, looper and effects, from the firmware. The two rewritten files keep their originals in `tape/upstream/` and every change is marked `Munchi:` |
| `src/engine/munchi_engine.*` | the firmware's page logic (`NormalPage`, `MenuPage`, `ui.h`), the preset table, and the card worker thread that owns all file I/O |
| `src/engine/resampler.h` | the engine runs at its native 48 kHz; this converts to and from Move's 44.1 kHz |
| `src/standalone/` | the SPI loop, the Move control mapping, lights and screen |
| `scripts/fetch-card.sh` | fetches the factory card from the CHOMPI repo at a pinned commit (it is not committed here) |
| `tests/render.cpp` | offline harness: scripted key/knob/menu events in, a WAV out |

Card samples are held in memory, so every voice uses the firmware's own
buffer-playback path; the 2x "double speed" read is derived from the sample
rather than loaded from a second file. One thread runs SPI, audio and controls,
as the firmware did; a worker does the file work. The binary needs glibc ≤ 2.35
(Move's), which is why the build uses Ubuntu 22.04.

### Tests

```bash
c++ -std=c++17 -O2 -Isrc/engine tests/render.cpp src/engine/munchi_engine.cpp \
    src/engine/wav_io.cpp src/engine/daisysp/*.cpp -o build-host/render
./scripts/fetch-card.sh build/card
./build-host/render build/card tests/scripts/keys_kit.txt out.wav
MUNCHI_WORKER=/tmp/scratch-card ./build-host/render build/card tests/scripts/record_save.txt out.wav
```

See [tests/README.md](tests/README.md) for the script format.

## Credits

- **CHOMPI TAPE 2.0:** CHOMPI Club / Chase Bliss, on Electrosmith's platform and
  original firmware. MIT. The CHOMPI name, logo and character are CHOMPI Club's
  trademarks and are not licensed; this port is named Munchi Tape for that reason.
- **DaisySP:** Electrosmith. MIT.
- **Reverb, FX engine, limiter:** Émilie Gillet, Mutable Instruments. MIT.

See [THIRD_PARTY.md](THIRD_PARTY.md). Munchi Tape itself is MIT ([LICENSE](LICENSE)).
