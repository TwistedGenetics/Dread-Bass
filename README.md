# Dread Bass

A jungle / drum & bass **character-bass oscillator** for the Korg **microKORG 2** (logue SDK 2.0).

Four engines for the dirty, percussive end of the low end — **DREAD**, **DONK**, **GROWL** and **HOOVER** — with a note-on **knock**, an optional **pluck** decay, a pitch-bending **shape EG** and a pure sine **sub** that is added after the drive, so it never gets dirty.

**DREAD** is the *Dred Bass* sound: a 303-style square note played backwards, so the resonant filter sweeps **up** through each note at a steady rate and the note ends at its brightest. **DONK** is a resonant pulse pluck, with the filter ringing on the 5th harmonic. Both were tuned by measuring reference samples (a reversed dread loop and a donk one-shot): harmonic content, filter sweep and resonance were matched against them at the same pitch and timing.

It's the sibling of [Rollers](https://github.com/TwistedGenetics/Rollers_Bass): Rollers does the clean sub and the Reese, Dread Bass does dread, donk and growl.

## Download & install

Go to [**Releases**](https://github.com/TwistedGenetics/Dread-Bass/releases/latest) and download `DreadBass.mk2unit`.

Needs microKORG 2 firmware **2.0.0 or later** (the version that added logue SDK support).

- **KORG Sound Librarian** (Korg's official app for v2.0): connect the microKORG 2 by USB, add `DreadBass.mk2unit` as a user oscillator and send it to the synth.
- **Or USB mass storage:** hold the first option button while powering on, then copy the file into `Units/Oscs/` (into a `SlotN` folder) on the drive that appears.

Dread Bass then appears at the end of the oscillator list as **Dread**. It's polyphonic, but it's built for mono / legato bass patches.

**`DreadMtr.mk2unit`** (optional, also in each Release) is the same oscillator with its CPU load shown next to MODE, e.g. `DONK 21%` (peak over the last half second; 100 % = out of time, crackles). Load it alongside Dread Bass if you hear crackles and want to know how close to the limit a patch is.

Each Release also has WAV renders of the starter settings below, made by the desktop tests.

## Controls

| Page | Knob | Range | What it does |
|---|---|---|---|
| 1 | **Timbre** | 0–1023 | The mode's main tone control (see below) |
| 1 | **Drive** | 0–100 % | Soft clipper on the main osc, gain compensated, run at 2x rate. The sub is never driven |
| 1 | **Mode** | DREAD / DONK / GROWL / HOOVER | The engine |
| 2 | **Charactr** | 0–100 % | The mode's second control (see below) |
| 2 | **Sub** | 0–100 % | Pure sine sub level, phase-locked to the main osc |
| 2 | **Sub Oct** | -1 OCT / -2 OCT | Sub one or two octaves down |
| 2 | **Knock** | 0–100 % | Short pitched blip on note-on (~6 ms), about 2.5 octaves over the note and sweeping down |
| 2 | **Pluck** | 0–100 % | 0 = sustain. Higher = the whole voice decays faster (0.9 s down to ~25 ms) |
| 3 | **EG Int** | -24…+48 semitones | How far the shape EG bends the pitch. Positive = drops into the note (dread droop, donk drop, hoover dive), negative = rises into it |
| 3 | **EG Time** | 1–1000 ms | How long the shape EG takes to settle. In DREAD it is also the length of the reversed swell |
| 3 | **Retrig** | off / on | On: legato notes restart the shape EG, knock and pluck. Off: legato notes only change pitch (rolling basslines). A new key after a release always restarts them |
| 3 | **Semi** | ±24 | Transpose |
| 3 | **Fine** | ±50 cents | Fine tune |

### The four modes

| Mode | Timbre | Charactr | Drive | Sound |
|---|---|---|---|---|
| **DREAD** | how far the filter opens by the end of the sweep (about 16x the note at the default) | resonance | clipper + Amiga-style crunch (sample rate down to ~10 kHz, 12 → 6 bits) | Reversed 303: square wave into a resonant 24 dB low-pass whose cutoff rises at a steady rate, in octaves, from about 2x the note to the Timbre setting over **EG Time**, at a steady level. Set EG Time to about your note length. Pluck starts once the sweep has peaked (0 = hold bright) |
| **DONK** | brightness: where the resonance sits (the 5th harmonic at the centre) | resonance; also takes the fundamental down under the resonant peak, like a ladder filter | clipper | Resonant pluck: a 45 % pulse into the same low-pass, opening about an octave above the 5th harmonic, snapping down onto it in ~35 ms, then sinking about an octave over **EG Time**. The ringing on the 5th harmonic is the "donk" |
| **GROWL** | FM index (2-op, 1:1) | modulator feedback (gritty, saw-ish) | clipper | Snarling mid-range. The shape EG also lifts the FM index on each hit. Put an LFO on Timbre in the virtual patch for talking wobbles |
| **HOOVER** | PWM depth (slow PWM) | detune of the two pulses (4–30 cents) | clipper | Pulse-hoover with a dive: set EG Int +7…+12 and EG Time 200–400 ms |

### Virtual patch

Dread Bass adds five modulation destinations: **Timbre**, **Drive**, **Charactr**, **Sub** and **Pitch** (±24 semitones at full depth; small depths give fine vibrato). Each voice is modulated on its own.

## Starter settings

| Name | Mode | Timbre | Drive | Charactr | Sub | Knock | Pluck | EG Int | EG Time | Retrig |
|---|---|---|---|---|---|---|---|---|---|---|
| **Dred bass** | DREAD | 700 | 0 | 55 % | 40 % | 0 | 0 | 0 | 300 ms | on |
| **Dread roller** | DREAD | 520 | 30 % | 70 % | 70 % | 0 | 0 | 0 | 170 ms | on |
| **Donk** | DONK | 512 | 10 % | 80 % | 20 % | 0 | 0 | 0 | 250 ms | on |
| **Snarl FM** | GROWL | 700 | 45 % | 55 % | 55 % | 0 | 0 | +5 | 250 ms | on |
| **Hoover dive** | HOOVER | 800 | 20 % | 60 % | 35 % | 0 | 0 | +12 | 300 ms | on |

For **Dred bass**, set EG Time to roughly the length of your notes (300 ms suits notes of three to four 16ths at 175 BPM) so each sweep peaks as the note ends; longer notes hold bright at the top. Play **Donk** around F1, short and bouncy. Keep the synth's own filter open and its amp EG at full sustain with a short release: DREAD and DONK have their own filter and envelopes. Let Sub carry the weight.

## Test checklist (by ear on the hardware)

- Dred bass at 175 BPM: each note sweeps up and ends bright, level steady through the note
- Each mode at C1, C2, C3: no crackle, steady level when switching MODE
- Drive 0 → 100 % on each mode: gets dirtier, not much louder; the sub stays solid
- Donk on fast 16ths: every hit the same, no clicks
- Retrig on / off with legato notes: on restarts the drop, off just slides
- EG Int -24 and +48, EG Time 1 ms and 1000 ms: no clicks or blow-ups
- Virtual patch: LFO → Timbre, Drive, Charactr, Sub, Pitch
- 8-voice chord with DreadMtr loaded: note the load in the MODE display

## CPU

The DSP runs four voices at once in NEON lanes, in short passes that fit the Cortex-A7's registers (checked in the compiled assembly: no register spills in any sample loop). Only the current mode's passes run, the knock pass is skipped when Knock is 0, and a group of four voices with no sounding voice is skipped.

On a desktop CPU, 8 voices take 7–9 µs per 64-sample buffer (1333 µs of audio); DREAD and DONK (two filter stages) are the heaviest. The real number on the microKORG 2 is what DreadMtr shows. The unit is about 36 KB loaded (code + buffers), under the 48 KB oscillator limit.

## Sound quality notes

- Saws and pulses are polyBLEP. DREAD's crunch aliases on purpose (that's the Amiga sound); turn Drive down for a clean reversed acid.
- Drive runs at 2x rate. Heavy drive on high notes (C4 and up at 100 %) still adds some aliasing, most in GROWL with full Timbre and Charactr. It's a bass oscillator: the low two octaves stay clean.
- An output limiter (transparent below 0.8, soft knee to 1) only touches the loudest combinations, e.g. Sub 100 % plus a full main osc.

## Building

The release files are built by GitHub Actions on every push (`.github/workflows/build.yml`), with Korg's own microKORG 2 toolchain.

**Locally (Linux):** `./build.sh` downloads Korg's toolchain and the logue SDK into `.deps/` and writes `dist/DreadBass.mk2unit` and `dist/DreadMtr.mk2unit`.

**With Korg's Docker environment (any OS):** copy `core/dreadbass.h` into `microkorg2/`, copy the `microkorg2/` folder into `logue-sdk/platform/microkorg2/dread-bass`, set `UINCDIR =` (empty) in its `config.mk`, then from `logue-sdk/docker`: `./run_cmd.sh build microkorg2/dread-bass`.

**Desktop DSP tests (no SDK needed):** `./test/run.sh` builds `test/render.cc` with your system compiler. It checks every mode for NaN / clipping / DC, tuning, drive gain compensation, that the sub stays a pure sine under full drive, knock / pluck / retrig and legato behaviour, clicks on legato notes, zipper noise under fast modulation, 300 random settings, that the 4-lane and 1-lane engines match exactly, then plays each starter setting as a two-bar 170 BPM line (gated like the synth's amp EG) and writes it as a WAV to `test/out/`.

## Layout

```
core/dreadbass.h        the DSP (one engine, 1-lane or 4-lane NEON)
microkorg2/             microKORG 2 unit: header.c (params), unit.cc (SDK glue), Makefile, config.mk
tools/mk2-meter/        DreadMtr: same unit with a CPU load meter (diagnostic)
test/                   desktop tests and WAV demos
build.sh                local build with Korg's toolchain
```

## Notes

- Developer ID `0x5477476E` ('TwGn'), unit ID `6` (DreadMtr: `0x106`).
- Parameter order in `microkorg2/header.c` must match `dread::Params` in `core/dreadbass.h`.
- The core has no SDK, table or libm dependencies, so an NTS-1 mkII build would be a new `nts1mkii/` folder with a one-voice `Engine<float>`, like Rollers.

## License

BSD 3-Clause, see [LICENSE](LICENSE).
