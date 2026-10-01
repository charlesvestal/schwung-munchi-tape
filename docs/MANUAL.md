# Munchi Tape — Manual

Munchi Tape turns Ableton Move into a **CHOMPI**: a sampler you play like a
keyboard, a tape looper, and a set of grubby effects. It runs the actual CHOMPI
TAPE 2.0 firmware code, so it behaves like the hardware, quirks included.

1. [What it is](#1-what-it-is)
2. [Starting and stopping](#2-starting-and-stopping)
3. [A first session](#3-a-first-session)
4. [The pads](#4-the-pads)
5. [Keys mode and Kit mode](#5-keys-mode-and-kit-mode)
6. [Shaping a sample](#6-shaping-a-sample)
7. [Effects](#7-effects)
8. [Recording into the buffer](#8-recording-into-the-buffer)
9. [The looper](#9-the-looper)
10. [The Shift layer](#10-the-shift-layer)
11. [Saving, copying and erasing](#11-saving-copying-and-erasing)
12. [The card: your samples](#12-the-card-your-samples)
13. [Settings](#13-settings)
14. [MIDI](#14-midi)
15. [The screen and the lights](#15-the-screen-and-the-lights)
16. [Control reference](#16-control-reference)
17. [Differences from a real CHOMPI](#17-differences-from-a-real-chompi)

---

## 1. What it is

Three things, all playing at once:

- **A sampler.** 14 sample slots per bank, 5 banks, in two modes. **Keys** plays
  one sample across the keyboard at different pitches; **Kit** gives every white
  key its own sample. Seven voices.
- **The buffer.** A slot you fill yourself, from the microphone, a line input or
  Munchi Tape's own output. It plays like any other sample.
- **A tape looper.** Records what you play, overdubs, and runs at any speed from
  double to stopped to reverse — or can be scrubbed by hand.

Every sound passes through a filter, saturation, a tape warble, a delay and a
reverb.

## 2. Starting and stopping

Open Schwung's Tools menu (**Shift + Volume + Step 13**) and choose **Munchi
Tape**. Move stops and Munchi Tape takes over the whole device: pads, knobs,
buttons, lights and screen. The first launch copies the factory samples into
place, which takes a few seconds (the screen says *FIRST RUN: COPYING CARD*);
after that it starts at once. For the first second and a half the pads are
ignored, as on the hardware.

To leave, press **Back**, then **Back** again within three seconds. Move
restarts.

> Leaving clears the **buffer** and the **looper** — they live in memory, as on
> a CHOMPI. Save anything you want to keep to a slot first
> ([section 11](#11-saving-copying-and-erasing)). Slot settings and your
> preferences are saved automatically.

## 3. A first session

1. **Play.** Press the pads. You start in Keys mode on the **buffer**, which
   holds a plain test tone until you record into it — the same sound, pitched up
   and down the keyboard.
2. **Pick a sample.** Turn the **jog wheel**, or press **step buttons 1–14**.
   Each step is a different sample from the card. (The factory samples in a bank
   are all tuned to C, so they change character, not key.)
3. **Bend it.** Turn **knob 1 (Speed)** left — past the middle, the sample plays
   backwards. Turn **knobs 2 and 3 (Start, End)** to play only a slice of it.
4. **Add space.** Turn **knob 4** up for delay and reverb, **knob 5** for
   saturation, **knob 6** to filter (left: low-pass, right: high-pass).
5. **Try a kit.** Hold **Shift** and press the **second black pad** of the bottom
   octave (D#3). Every white pad is now its own sound. Shift + D#3 again gives
   the next kit. Shift + C#3 goes back to Keys.
6. **Loop it.** Press **Loop**, play something, press **Loop** again — the loop
   starts repeating and you are adding to it. Press **Play** to stop adding.
   Turn **knob 7** to change its speed.
7. **Sample yourself.** Hold **Sample**, make a noise into Move's microphone,
   let go. Press **step 15** (or jog to *BUF*) and play the pads: your noise is
   the instrument.

## 4. The pads

The pads are the CHOMPI's two-octave keyboard, C3 to C5, laid out as piano rows:

```
row 4   .  C#4 D#4  .  F#4 G#4 A#4  .
row 3  C4  D4  E4  F4  G4  A4  B4  C5
row 2   .  C#3 D#3  .  F#3 G#3 A#3  .
row 1  C3  D3  E3  F3  G3  A3  B3  C4
```

White keys are lit grey, black keys darker. The two **C4** pads are the same
key. **Up / Down** shift the keyboard by octaves (±2) in Keys mode.

Pads play at full velocity, as the CHOMPI's keys did; turn on **Pad Velocity**
in [Settings](#13-settings) to play them dynamically.

## 5. Keys mode and Kit mode

### Keys (the CHOMPI's "Jammi" mode)

One sample, played chromatically. C4 plays it at its recorded pitch.

- **Choose a sample:** jog wheel, step buttons 1–14, or **Shift + a white key**
  (slots 1–14 run up the white keys from C3; the top C is the buffer).
- **Choose a bank:** **Shift + C#3** steps through banks A–E. This *browses* —
  the sample you are playing does not change until you pick a slot in the new
  bank. The screen shows `KEYS A3 [B]` while you play A3 and look at bank B.
- **Step 15**, or jog to *BUF*, plays the buffer.

### Kit (the CHOMPI's "Cubbi" mode)

Each white key is a slot: C3 is slot 1, D3 slot 2 … B4 slot 14, and C5 is the
buffer. Black keys do nothing. Steps 1–15 play the same slots.

- **Shift + D#3** enters Kit mode, then steps through kits A–E.
- Lit pads are slots that have a sample.
- The knobs edit **the slot you played last**, and each slot remembers its own
  settings.

Kit and Keys have separate banks: `cubbi_*` files and `jammi_*` files
([section 12](#12-the-card-your-samples)).

## 6. Shaping a sample

These settings belong to **the slot** — each sample remembers its own speed,
start, end, envelope, gain, pan, looping and sustain, and gets them back when you
select it again.

| Knob | Does |
|---|---|
| **1 Speed** | Playback speed and pitch: 2x reverse at the far left, stopped in the middle, 2x forward at the far right (it starts at 1x forward). With **Shift**: steps in fifths and octaves |
| **2 Start** | Where the sample starts |
| **3 End** | Where it ends. Start and end can't get closer than about 85 ms |
| **Right**, then **1 Gain** | Level of this sample (0 to 2x) |
| **Right**, then **2 Attack** | Fade-in, up to 20 s |
| **Right**, then **3 Decay** | Release time, up to 4 s |

**Left / Right** switch knobs 1–3 between these two pages; the arrow that is lit
is the way to the other page.

With **Shift** held:

- **Shift + knob 2 or 3** slides the start/end window together, keeping its
  length (on the Gain page: sets attack and decay together).
- **Shift + knob 1** on the Gain page sets **pan**.

Two switches, on the track buttons:

- **Track 1 — Auto-loop.** On (lit): a held note loops between start and end.
  Off: it plays once.
- **Track 2 — Sustain.** On: the sound holds while the pad is held. Off: it
  plays its attack then fades over the decay time, held or not.

**Delete + touch knob 1** resets speed to 1x forwards (on the Gain page: gain
and pan).

## 7. Effects

| Knob | Does | With Shift |
|---|---|---|
| **4 Space** | Delay and reverb together | Delay time (also sets the reverb size) |
| **5 Lofi** | Saturation | Warble (tape wow and flutter) |
| **6 Filter** | Middle is open; left is low-pass, right is high-pass | Resonance |
| **8 Input** | Input gain for recording | Output compressor |
| **Volume knob** | Volume | Output compressor |

**Delete + touch knob 4, 5 or 6** resets all the effects.

**Split Delay** ([Settings](#13-settings)) changes knob 4: left of centre is
delay only, right of centre is reverb only.

**Effects before or after the looper.** By default (**Shift + C#4**) the effects
colour what you play, and the looper records the coloured sound — loop playback
itself is dry, so you can change the effects over a loop without changing the
loop. **Shift + D#4** moves them after the looper, so they process everything,
loop included.

## 8. Recording into the buffer

The buffer is one sample that you record yourself, up to about 165 seconds.

1. Pick a source: **Shift + F#3** microphone, **Shift + G#3** line input,
   **Shift + A#3** resample (records Munchi Tape's own output — your playing,
   the loop and the effects).
2. Set the level with **knob 8 (Input)**. The **Sample** button's light shows the
   input level while monitoring is on (green, yellow, pink as it gets louder).
3. **Hold Sample** to record; let go to stop. With **Record Latch** on, tap to
   start and tap to stop.
4. Play the buffer: in Keys mode press **step 15** (or jog to *BUF*); in Kit mode
   it's the top C.

A new recording resets the buffer's speed, start, end, gain and envelope.

**Monitoring — Track 4.** Lit red, you hear a **line** input as you record. The
**microphone is never monitored**: Move's mic sits beside its speakers, so
hearing it would feed back. It still records normally — you just hear it
afterwards.

**Track 3** chooses where a monitored line input goes: *Dry* (straight to the
output), *Thru FX* (through the effects and into the looper, the default) or
*Send/Ret* (back in after the effects).

Move has one input. With nothing plugged into the line jack it's the internal
microphone; with a cable, it's the line. Choose the matching source yourself
(Shift + F#3 or G#3) — Munchi Tape doesn't switch automatically.

## 9. The looper

The looper records the sound coming out of the sampler and any monitored input.
Its length is set by the first recording, up to about 165 seconds.

| Do | Result |
|---|---|
| **Loop** (empty looper) | Start recording |
| **Loop** again | The loop starts playing back, and you **keep recording on top** (overdub) |
| **Loop** again | Stop overdubbing; it keeps playing |
| **Play** while recording | Stop recording; it keeps playing |
| **Play** | Pause / resume |
| **Play**, held 2 s while paused | Back to the start |
| **Loop + Play** together, on an empty looper | Arm: the next note you play starts the recording |
| **Loop + Play** held 2 s, or **Delete + Loop** | Clear the looper |

**Rec** does the same as **Loop**.

**Knob 7 (Loop speed)** while the loop plays: −2x to +2x, through stop and into
reverse. Pitch and speed change together, like tape. **Shift + knob 7** steps in
fifths and octaves. **Delete + touch knob 7** returns to 1x.

While the loop is **paused**, turning knob 7 **scrubs** — drag the tape back and
forth by hand.

**Overdub level:** hold **Shift** and press **Loop** (more) or **Play** (less).
It sets how much of the existing loop survives each pass of overdubbing, so
lower values let old layers fade away.

**Tape Slew** ([Settings](#13-settings)) makes speed changes glide like a real
transport.

## 10. The Shift layer

Holding **Shift** turns the pads into the CHOMPI's menu. The black keys become
functions, lit when they apply:

```
row 4   .  FX>  >FX   .   ERASE COPY SAVE  .
row 2   .  KEYS KIT   .   MIC   LINE RSMP  .
```

| Pad | Does |
|---|---|
| **C#3 — Keys** | Keys mode; press again for the next bank |
| **D#3 — Kit** | Kit mode; press again for the next kit |
| **F#3 / G#3 / A#3** | Input: Mic / Line / Resample |
| **C#4 / D#4** | Effects before / after the looper |
| **F#4 — Erase** (or Shift + Delete) | Erase a slot |
| **G#4 — Copy** (or Shift + Copy) | Copy a slot, the buffer or the looper |
| **A#4 — Save** (or Shift + Capture) | Save the buffer |

In Keys mode, **white keys** select slots (the playing slot is lit white).
**Loop** and **Play** set the overdub level. The knobs do their Shift jobs
(sections [6](#6-shaping-a-sample), [7](#7-effects) and [9](#9-the-looper)).

## 11. Saving, copying and erasing

All three work the same way: choose the action, pick a slot, confirm with
**Shift**. The menu stays open while you pick, so you can let go of Shift.

**Save the buffer to a slot**

1. Hold **Shift**, press **A#4** (Save). Slots blink.
2. Press the **white key** of the slot to save into (it turns blue). It goes to
   the current mode and bank. Press **Loop** or **Play** instead to put the
   buffer into the looper.
3. Press **Shift** to confirm. The slot is written to the card and selected.

**Copy**

1. **Shift + G#4** (Copy).
2. Pick the source: a white key, the top C (the buffer), or **Loop/Play** (the
   looper). It turns green.
3. Pick the destination: a white key, the top C, or Loop/Play. It turns blue. You
   can change mode or bank in between to copy across banks.
4. **Shift** to confirm.

**Erase**

1. **Shift + F#4** (Erase).
2. Pick the slot (it turns red).
3. **Shift** to confirm. The file is deleted from the card.

To cancel, press the same function pad again. Saving and copying overwrite
whatever was in the destination.

## 12. The card: your samples

Munchi Tape keeps its samples where a CHOMPI did, on a "card" — here a folder:

```
/data/UserData/UserLibrary/Samples/Schwung/Munchi Tape/
```

Copy files in and out with the Schwung web manager's file browser
(`http://move.local:7700`). Names follow the CHOMPI's scheme:

| File | Slot |
|---|---|
| `jammi_a1.wav` | Keys mode, bank A, slot 1 |
| `jammi_c14.wav` | Keys mode, bank C, slot 14 |
| `cubbi_b3.wav` | Kit mode, bank B, slot 3 |

Banks A–C hold the factory samples; **D and E start empty** for your own. New
files appear within about five seconds, with no restart. Munchi Tape plays WAVs
at any sample rate, 8/16/24/32-bit or float, mono or stereo.

Everything Munchi Tape writes is in the CHOMPI's own format (48 kHz, 16-bit
stereo, with the `_double` companion files), and `presets.json` (every slot's
settings) and `options.json` (settings) are the CHOMPI's files. The folder can go
straight onto a CHOMPI's SD card, and a CHOMPI card's contents can come here.

To get factory samples back after erasing them, delete the hidden file
`.munchi-card` from the folder and relaunch. It restores missing factory files
and never overwrites ones that are there.

## 13. Settings

Press **Menu** (or click the jog wheel). Turn the jog to move, click to change,
Shift + click to step backwards, **Back** or **Menu** to close. Settings are
saved to the card's `options.json`.

| Setting | Does |
|---|---|
| **Record Latch** | Sample is tap-on / tap-off instead of hold-to-record |
| **Tape Slew** | Looper speed changes glide |
| **Monitor** | Where a monitored line input goes (as Track 3) |
| **Split Delay** | Knob 4: delay to the left, reverb to the right |
| **Shift Snap** | Shift + knobs 1 and 7 step in fifths and octaves (off: fine control) |
| **Pad Velocity** | Pads play at their velocity |
| **MIDI In Ch / MIDI Out Ch** | USB-A MIDI channels |
| **Exit** | Leave Munchi Tape |

## 14. MIDI

A controller on Move's **USB-A** port can play Munchi Tape, on **MIDI In Ch**:

- **Notes 24–72** play the keyboard, with velocity. In Kit mode the white keys
  48–72 are the slots.
- **CC 20–25** set the CHOMPI's six encoders: knobs 1–3 (on their current
  page), the effects knob (whichever of Space / Lofi / Filter you turned last),
  Loop speed (only while the loop plays) and Volume.
- **CC 26 / 27** are Play and Loop (above 84 pressed, below 42 released).

Munchi Tape sends its key presses as notes and its knob moves as CCs on **MIDI
Out Ch**, as the CHOMPI did.

## 15. The screen and the lights

The screen shows, top to bottom: mode, bank and slot (and the input source, with
`REC` when monitoring); the sample with its start–end window and a playhead; the
last control you touched, or the knob values; and the looper's state with its
position.

| Light | Means |
|---|---|
| Pads | Keyboard layout in grey; a playing key white. Kit mode: lit in the kit's colour where a slot has a sample; the buffer pink |
| Steps 1–15 | Slots with a sample (bank colour), the playing slot white |
| Sample | Red while recording; the input level while monitoring |
| Loop | Red: first recording. Yellow: overdubbing. Grey: playing. Blinking: armed |
| Play | Teal: playing (blinks with the loop). White: armed. Grey: paused |
| Track 1 / 2 | Auto-loop / sustain on |
| Track 3 | Monitor path: blue Thru FX, orange Dry, yellow Send/Ret |
| Track 4 | Red: monitoring on |
| Left / Right | Which way the other knob page is |
| Shift | The Shift layer is open |

Banks have colours, as on the CHOMPI: A purple, B orange, C teal, D dark orange,
E lime.

## 16. Control reference

| Control | Alone | With Shift |
|---|---|---|
| Pads | play | menu functions / pick slots |
| Knob 1 | Speed (Gain page: Gain) | Speed in steps (Gain page: Pan) |
| Knob 2 | Start (Attack) | Slide window (Attack + Decay) |
| Knob 3 | End (Decay) | Slide window (Attack + Decay) |
| Knob 4 | Space | Delay time |
| Knob 5 | Lofi | Warble |
| Knob 6 | Filter | Resonance |
| Knob 7 | Loop speed / scrub | Loop speed in steps |
| Knob 8 | Input gain | Compressor |
| Volume | Volume | Compressor |
| Left / Right | knob 1–3 page | — |
| Up / Down | octave | — |
| Jog | next/previous slot (Keys) | — |
| Jog click / Menu | settings | — |
| Steps 1–15 | select slot (Keys) / play slot (Kit) | — |
| Sample | record buffer | — |
| Loop / Rec | looper record / overdub | overdub level up |
| Play | looper play / pause | overdub level down |
| Delete + Loop | clear looper | — |
| Delete + touch knob 1 / 4–6 / 7 | reset speed / effects / loop speed | — |
| Delete / Copy / Capture | — | erase / copy / save |
| Track 1 / 2 / 3 / 4 | auto-loop / sustain / monitor path / monitor | — |
| Back ×2 | exit | — |

## 17. Differences from a real CHOMPI

- **Controls.** Move has more knobs and no encoder clicks, so pages are on
  Left/Right and clicks are Delete + touch or the Track buttons. Shift is the
  menu (the CHOMPI key in play mode), Sample records (the CHOMPI key in record
  mode), and Track 4 is only the monitor switch.
- **Output.** Move has one stereo output, so you hear the CHOMPI's headphone mix
  at line level.
- **Input.** The microphone is never monitored, and the input source never
  switches by itself.
- **Samples in memory.** The CHOMPI streamed samples from its card; Munchi Tape
  holds them in memory, so every voice uses the firmware's buffer playback. A
  looping sample's crossfade at the loop point is always about 3 ms, and speed
  changes take effect at once.
- **No screen reader** while Munchi Tape owns the device: Schwung's speech runs
  inside Move, which is stopped.
