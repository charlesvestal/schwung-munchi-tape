# Munchi

**CHOMPI TAPE 2.0 on Ableton Move.** A seven-voice chromatic sampler and varispeed
tape looper, with the full factory sample card, delay, reverb, lofi, warble and a
DJ filter. Munchi is a [Schwung](https://github.com/charlesvestal/schwung)
standalone tool: launched from the Tools menu, it stops Move and runs the device
itself, the way the CHOMPI's own firmware runs its hardware. Exiting (Back, twice)
hands Move back.

It is a port of the firmware CHOMPI Club released as open source
([CHOMPI-Club/CHOMPI](https://github.com/CHOMPI-Club/CHOMPI), MIT). The engine,
voices, looper and effects are the TAPE 2.0 code, running at its native 48 kHz;
every key, knob and menu gesture goes through TAPE's own page logic. Munchi is not
an official CHOMPI Club release; see [Credits](#credits).

## Controls

The CHOMPI keyboard is on the pads, as two octaves of piano rows: the bottom row
is C3-C4 (white keys), the row above it the black keys between them; the upper two
rows are C4-C5. The two C4 pads are the same key.

| Move | Munchi (CHOMPI) |
|---|---|
| Pads | the 25 keys. **Keys** mode: play the selected sample chromatically. **Kit** mode: each white key is its own slot, top C is the buffer |
| Knob 1 / 2 / 3 | Speed / Start / End. Press **Right** to flip them to Gain / Attack / Decay, **Left** to flip back |
| Knob 4 / 5 / 6 | the magic knob's three pages: Space (delay + reverb) / Lofi / Filter |
| Knob 7 | Loop speed (-2x to 2x, through reverse). Turned while the looper is stopped, it scrubs |
| Knob 8 | Input gain |
| Volume knob | Volume |
| **Sample** | the CHOMPI key in record mode: record into the buffer (hold, or tap on/off with Record Latch) |
| **Loop** (or Rec) | the looper's record key: record, overdub, stop overdubbing |
| **Play** | the looper's play key: play/pause; hold 2 s when stopped to return to the start |
| Loop + Play, 2 s | clear the looper (also Delete + Loop). Press both together on an empty looper to arm it; the next key starts recording |
| **Shift** (hold) | the CHOMPI key in play mode: the menu layer (below) |
| Up / Down | keyboard octave (Keys mode) |
| Steps 1-15 | slots 1-14 and the buffer. Keys: select. Kit: play |
| Jog | Keys mode: step through the slots on the card |
| Track 1 / 2 | auto-loop on/off, sustain on/off (for the current sample) |
| Track 3 | monitor path: Dry / Thru FX / Send-Return |
| Track 4 | the record switch: input monitoring on (red) or off |
| Delete + touch knob 1 | reset speed (on the Gain page: gain and pan) |
| Delete + touch knob 4-6 | reset all effects |
| Delete + touch knob 7 | reset loop speed |
| Menu, or jog click | settings |
| Back, twice | exit to Move |

### The menu layer (hold Shift)

As on the CHOMPI, the menu reuses the keyboard. Black keys:

| Key | Function |
|---|---|
| C#3 | Keys mode; press again for the next bank (A-E) |
| D#3 | Kit mode; press again for the next bank |
| F#3 / G#3 / A#3 | input: Mic / Line / Resample |
| C#4 / D#4 | effects before the looper / after the looper |
| F#4 (or Shift + Delete) | erase a slot |
| G#4 (or Shift + Copy) | copy a slot, the buffer or the looper |
| A#4 (or Shift + Capture) | save the buffer to a slot (or to the looper) |

White keys select a slot (Keys mode), or pick the slot an erase, copy or save
acts on. Loop and Play pick the looper. **Shift confirms**. In the menu, Loop and
Play also set the overdub level, and the knobs do their menu jobs:

| Knob | with Shift |
|---|---|
| 1 | speed in fifths and octaves (Gain page: pan) |
| 2 / 3 | slide the start/end window together (Gain page: attack and decay together) |
| 4 / 5 / 6 | delay time / warble / filter resonance |
| 7 | loop speed in fifths and octaves |
| 8, volume | output compressor |

## The card

Munchi keeps its samples where the CHOMPI kept them, on a card: a folder of WAVs
at `/data/UserData/UserLibrary/Samples/Schwung/Munchi`, created from the factory
card the first time you launch it. Files are named as on the CHOMPI:
`jammi_a1.wav` is Keys mode, bank A, slot 1; `cubbi_c14.wav` is Kit mode, bank C,
slot 14. Banks D and E start empty.

Add your own samples by copying them there (the Schwung web manager's file browser
works), named that way. The CHOMPI wanted 48 kHz 16-bit stereo; Munchi also takes
8/24/32-bit, float, mono and other sample rates, and converts them. Files Munchi
writes are the CHOMPI's format, `_double` companions included, so the folder can
be copied to a CHOMPI's SD card. `presets.json` (each slot's settings) and
`options.json` are the CHOMPI's formats too.

Delete `.munchi-card` from the folder to have Munchi copy back any factory samples
that are missing. It never overwrites a file that is there.

The buffer and the looper live in memory, as on the CHOMPI: they are gone when you
exit. Save the buffer to a slot first (Shift + A#4, pick a slot, Shift).

## Settings

Menu (or jog click) opens them; jog to move, click to change, Back to close. They
are saved to the card's `options.json`.

- **Record Latch** — the Sample button toggles recording instead of recording while held
- **Tape Slew** — the looper's speed changes glide like a tape transport
- **Monitor** — as Track 3
- **Split Delay** — the Space knob becomes delay (left) / reverb (right)
- **Shift Snap** — the Shift layer's speed knobs step in fifths and octaves (off: fine)
- **Pad Velocity** — pads play at their velocity (the CHOMPI's keys are fixed at full)
- **MIDI In / Out Ch** — USB-A MIDI: notes 24-72, CC 20-25 set the knobs, CC 26/27 are Play/Loop; Munchi sends the keys and knobs on the out channel

## Differences from the hardware

- Move has one stereo output, so Munchi plays the CHOMPI's headphone mix at line
  level (the line mix plus, in Dry mode, the input monitor).
- Move's single input is the internal mic until a cable is in the line jack; Munchi
  follows the jack like the CHOMPI did. Mic and Line select how that input is
  treated (the CHOMPI's mic filter and gain, or line gain).
- Samples are held in memory rather than streamed from a card, so the voice uses
  the firmware's buffer-playback path for every sample. The audible differences:
  a sample's loop fade is always 150 frames, and speed changes land immediately
  instead of after the SD read-ahead drains.
- Encoder pages and clicks: Move's knobs don't click, so pages are on
  Left/Right, and clicks are Delete + touch (reset) or the Track buttons (toggles).
- The record/play switch no longer opens the menu: Shift is the menu, Sample
  records, and Track 4 is only the monitor switch.
- The screen reader does not run while Munchi owns the device.

## Building

```bash
./scripts/build.sh      # fetches the factory card, cross-compiles in Docker
./scripts/install.sh    # copies dist/munchi to the Move
```

`tests/render.cpp` drives the engine offline and renders a WAV, which is how the
port is checked against the firmware's behaviour without a device.

## Credits

- **CHOMPI TAPE 2.0** — CHOMPI Club / Chase Bliss, on Electrosmith's platform and
  firmware. MIT. The CHOMPI name and marks are CHOMPI Club's and are not licensed;
  this port is named Munchi for that reason.
- **DaisySP** — Electrosmith. MIT.
- **Reverb, FX engine, limiter** — Émilie Gillet, Mutable Instruments. MIT.

See [THIRD_PARTY.md](THIRD_PARTY.md).
