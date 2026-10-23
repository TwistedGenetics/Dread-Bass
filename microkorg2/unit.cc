/*
 *  File: unit.cc
 *
 *  Dread Bass - logue SDK callbacks for the microKORG 2 (polyphonic)
 *
 *  Voices are rendered four at a time, one per NEON lane, by the shared
 *  engine in core/dreadbass.h (Engine<v4f>). Idle voices are skipped: a group
 *  of four with no sounding voice costs (almost) nothing.
 *
 *  Note starts. Everything percussive (shape EG, knock, pluck, the DREAD
 *  sweep, the DONK ring) restarts on a note start, so finding them reliably
 *  matters more than anything else here. v1.0 only listened for voice
 *  events, and on the hardware those don't seem to arrive: the envelopes
 *  never restarted, PLUCK faded the sound out for good and the EG controls
 *  did nothing. Now four sources are combined, first one wins, and repeats
 *  within ~4 ms are ignored so two sources can't double-trigger a note:
 *    E  unit_osc_voice_event (allocation / steal), with fresh / legato info
 *    T  the runtime context's per-voice trigger bits (rising edge)
 *    N  unit_note_on (matched to the voice playing that note)
 *    P  last resort, only while none of the above has ever been seen: a
 *       pitch jump of 3/4 semitone or more inside one buffer
 *  How each note starts:
 *    - a silent voice starts clean (phases at zero, filters cleared)
 *    - a voice still ringing keeps its phases (no click) and restarts its
 *      envelopes
 *    - a legato note (key still held, known from voice events only)
 *      restarts the envelopes only when RETRIG is on
 *  Until the first voice event arrives every voice renders, so nothing goes
 *  silent if the host never sends events.
 *
 *  The virtual patch can modulate Timbre, Drive, Character, Sub and Pitch per
 *  voice. Buffer layout, mod data access and voice events follow Korg's
 *  microkorg2/waves and vox examples (and Rollers / Dusty Pads).
 *
 *  Built with -DDREAD_LOADMETER (tools/mk2-meter), the MODE display also
 *  shows the CPU load of this unit and which note-start sources have been
 *  seen, e.g. "DONK 21%ET" (E / T / N / P as above, "-" = none yet).
 */

#include "unit.h"
#include "runtime.h"
#include "unit_osc.h"
#include "utils/io_ops.h"
#include "utils/mk2_utils.h"
#include "dreadbass.h"
#ifdef DREAD_LOADMETER
#include <stdio.h>
#include <time.h>
#endif

using dread::DreadBass4;
using dread::Params;
using dread::v4f;

namespace {

enum { kDestTimbre = 0, kDestDrive, kDestChar, kDestSub, kDestPitch, kNumDest };
enum { kNumGroups = kMk2MaxVoices / 4 };

// Per-voice output trim: voices are summed by the synth (same as Rollers /
// Dusty Pads; Korg's waves uses 0.3)
constexpr float kVoiceTrim = 0.35f;

// Pitch mod depth: +-24 semitones at full depth, squared so small depths
// give fine control (vibrato) and large ones give big sweeps
constexpr float kPitchModRange = 24.f;

DreadBass4 s_group[kNumGroups];
float s_mod[kNumDest][kMk2MaxVoices];
int32_t s_cached[Params::kNumParams];
unit_runtime_desc_t s_desc;
v4f s_out4[dread::kMaxBlock];

// Voice state from unit_osc_voice_event
bool s_active[kMk2MaxVoices];  // sounding (allocated, not yet deallocated)
bool s_held[kMk2MaxVoices];    // key held (allocated / stolen, not released)
bool s_events_seen = false;

// Note-start detection (see the top of the file)
enum { kSrcEvent = 1, kSrcTrigger = 2, kSrcNoteOn = 4, kSrcPitch = 8 };
constexpr uint32_t kRetrigGuard = 192;  // frames (~4 ms) to ignore a second start
bool s_pend[kMk2MaxVoices];             // a note start waits for the next render
bool s_pendFresh[kMk2MaxVoices];
bool s_pendLegato[kMk2MaxVoices];
uint32_t s_sinceStart[kMk2MaxVoices];   // frames since the last note start
float s_lastNote[kMk2MaxVoices];
uint8_t s_lastTrig = 0;
uint8_t s_srcSeen = 0;
int s_noteOnPend = -1;                  // note number from unit_note_on

// Queue a note start for voice v unless one fired very recently
void requestStart(int v, bool fresh, bool legato, uint8_t src) {
  s_srcSeen |= src;
  if (s_pend[v]) {  // a richer source (voice event) can still fill in details
    if (src == kSrcEvent) {
      s_pendFresh[v] = fresh;
      s_pendLegato[v] = legato;
    }
    return;
  }
  if (s_sinceStart[v] < kRetrigGuard) return;
  s_pend[v] = true;
  s_pendFresh[v] = fresh;
  s_pendLegato[v] = legato;
}

#ifdef DREAD_LOADMETER
// Time spent in unit_render as a share of the buffer's real-time length.
// Peak over ~0.5 s. 100 % = no time left (crackles).
float s_load_peak = 0.f, s_load_show = 0.f;
uint32_t s_load_frames = 0;
char s_meter_str[24];
inline double nowNs() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}
#endif

inline const unit_runtime_osc_context_t *context() {
  return static_cast<const unit_runtime_osc_context_t *>(s_desc.hooks.runtime_context);
}

inline int voiceCount(const unit_runtime_osc_context_t *ctx) {
  const int n = ctx->voiceLimit;
  return n < 0 ? 0 : (n > kMk2MaxVoices ? kMk2MaxVoices : n);
}

// NaN/inf check on the bits: the Makefile builds with -ffast-math, which lets
// the compiler drop float NaN comparisons
inline bool finiteBits(float x) {
  union { float f; uint32_t u; } b;
  b.f = x;
  return (b.u & 0x7F800000u) != 0x7F800000u;
}

void writeName(char *dst, const char *name) {
  int i = 0;
  for (; name[i] && i < 8; ++i) dst[i] = name[i];
  dst[i] = 0;
}

}  // namespace

__attribute__((used)) int8_t unit_init(const unit_runtime_desc_t *desc) {
  if (!desc) return k_unit_err_undef;
  if (desc->target != unit_header.target) return k_unit_err_target;
  if (!UNIT_API_IS_COMPAT(desc->api)) return k_unit_err_api_version;
  if (desc->samplerate != 48000) return k_unit_err_samplerate;

  s_desc = *desc;
  s_events_seen = false;
  for (int k = 0; k < kNumGroups; ++k) s_group[k].init();
  s_lastTrig = 0;
  s_srcSeen = 0;
  s_noteOnPend = -1;
  for (int v = 0; v < kMk2MaxVoices; ++v) {
    s_active[v] = false;
    s_held[v] = false;
    s_pend[v] = s_pendFresh[v] = s_pendLegato[v] = false;
    s_sinceStart[v] = kRetrigGuard;
    s_lastNote[v] = -1.f;
    for (int d = 0; d < kNumDest; ++d) s_mod[d][v] = 0.f;
  }
  for (int id = 0; id < Params::kNumParams; ++id) {
    s_cached[id] = static_cast<int32_t>(unit_header.params[id].init);
    for (int k = 0; k < kNumGroups; ++k) s_group[k].setParam(id, s_cached[id]);
  }
  return k_unit_err_none;
}

__attribute__((used)) void unit_teardown() {}

__attribute__((used)) void unit_reset() {
  for (int k = 0; k < kNumGroups; ++k) s_group[k].resetState();
}

__attribute__((used)) void unit_resume() {}

__attribute__((used)) void unit_suspend() {}

__attribute__((used)) void unit_render(const float *in, float *out, uint32_t frames) {
  (void)in;
#ifdef DREAD_LOADMETER
  const double t_start = nowNs();
#endif
  const unit_runtime_osc_context_t *ctx = context();
  const int n = voiceCount(ctx);

  // ---- note starts: trigger bits, unit_note_on and the pitch fallback ----
  {
    const uint8_t trig = ctx->trigger;
    const uint8_t rise = (uint8_t)(trig & ~s_lastTrig);
    s_lastTrig = trig;
    const bool primarySeen = (s_srcSeen & (kSrcEvent | kSrcTrigger | kSrcNoteOn)) != 0;
    int noteOnVoice = -1;
    float best = 0.6f;  // the voice playing unit_note_on's note (within a semitone)
    for (int v = 0; v < n; ++v) {
      float note = ctx->pitch[v];
      if (!finiteBits(note) || note < 0.f || note > 140.f) note = 60.f;
      if (rise & (1u << v)) requestStart(v, s_events_seen && !s_active[v], false, kSrcTrigger);
      if (s_noteOnPend >= 0) {
        const float d = __builtin_fabsf(note - (float)s_noteOnPend);
        if (d < best) {
          best = d;
          noteOnVoice = v;
        }
      }
      if (!primarySeen && s_lastNote[v] >= 0.f && __builtin_fabsf(note - s_lastNote[v]) >= 0.75f)
        requestStart(v, false, false, kSrcPitch);
      s_lastNote[v] = note;
    }
    if (noteOnVoice >= 0) requestStart(noteOnVoice, s_events_seen && !s_active[noteOnVoice], false, kSrcNoteOn);
    s_noteOnPend = -1;
    for (int v = 0; v < kMk2MaxVoices; ++v) {
      if (s_pend[v]) {
        s_group[v >> 2].noteOn(v & 3, s_pendFresh[v], s_pendLegato[v]);
        s_pend[v] = false;
        s_sinceStart[v] = 0;
      }
      if (s_sinceStart[v] < kRetrigGuard) s_sinceStart[v] += frames;
    }
  }

  for (int base = 0; base < n; base += 4) {
    DreadBass4 &grp = s_group[base >> 2];
    const int lanes = (n - base) < 4 ? (n - base) : 4;
    const uint32_t offset = GetBufferOffset(ctx, base, frames);
    bool live[4] = {false, false, false, false};
    bool any = false;
    for (int j = 0; j < lanes; ++j) {
      const int v = base + j;
      live[j] = !s_events_seen || s_active[v];
      if (!live[j]) continue;
      any = true;
      // ctx->pitch is a fractional MIDI note number per voice
      float note = ctx->pitch[v];
      if (!finiteBits(note) || note < 0.f || note > 140.f) note = 60.f;
      grp.setNote(j, note);
      grp.setMod(j, s_mod[kDestTimbre][v], s_mod[kDestDrive][v], s_mod[kDestChar][v], s_mod[kDestSub][v],
                 s_mod[kDestPitch][v]);
    }

    // Voice v writes lane v%4 of its group of four (Korg's waves, Rollers)
    if (!any) {
      for (uint32_t i = 0; i < frames; ++i)
        for (int j = 0; j < lanes; ++j) write_oscillator_output_x1(out, 0.f, offset, ctx->outputStride, i, (uint32_t)j);
      continue;
    }
    uint32_t done = 0;
    while (done < frames) {
      const uint32_t len = (frames - done) < (uint32_t)dread::kMaxBlock ? (frames - done) : (uint32_t)dread::kMaxBlock;
      grp.render(s_out4, (int)len);
      for (uint32_t i = 0; i < len; ++i)
        for (int j = 0; j < lanes; ++j) {
          const float y = live[j] ? s_out4[i][j] * kVoiceTrim : 0.f;
          write_oscillator_output_x1(out, y, offset, ctx->outputStride, done + i, (uint32_t)j);
        }
      done += len;
    }
  }

#ifdef DREAD_LOADMETER
  const double used = nowNs() - t_start;
  const float load = (float)(used / ((double)frames * (1e9 / 48000.0)));
  if (load > s_load_peak) s_load_peak = load;
  s_load_frames += frames;
  if (s_load_frames >= 24000) {
    s_load_show = s_load_peak;
    s_load_peak = 0.f;
    s_load_frames = 0;
  }
#endif
}

__attribute__((used)) void unit_set_param_value(uint8_t id, int32_t value) {
  if (id >= Params::kNumParams) return;
  value = clipminmaxi32(unit_header.params[id].min, value, unit_header.params[id].max);
  s_cached[id] = value;
  for (int k = 0; k < kNumGroups; ++k) s_group[k].setParam(id, value);
}

__attribute__((used)) int32_t unit_get_param_value(uint8_t id) {
  if (id >= Params::kNumParams) return 0;
  return s_cached[id];
}

__attribute__((used)) const char *unit_get_param_str_value(uint8_t id, int32_t value) {
  switch (id) {
    case Params::kMode:
#ifdef DREAD_LOADMETER
      snprintf(s_meter_str, sizeof s_meter_str, "%s %d%%%s%s%s%s%s", Params::modeName(value), (int)(s_load_show * 100.f + 0.5f),
               (s_srcSeen & kSrcEvent) ? "E" : "", (s_srcSeen & kSrcTrigger) ? "T" : "",
               (s_srcSeen & kSrcNoteOn) ? "N" : "", (s_srcSeen & kSrcPitch) ? "P" : "", s_srcSeen ? "" : "-");
      return s_meter_str;
#else
      return Params::modeName(value);
#endif
    case Params::kSubOct:
      return Params::subOctName(value);
    default:
      return nullptr;
  }
}

__attribute__((used)) const uint8_t *unit_get_param_bmp_value(uint8_t id, int32_t value) {
  (void)id;
  (void)value;
  return nullptr;
}

__attribute__((used)) void unit_set_tempo(uint32_t tempo) { (void)tempo; }

// Mono-style note on (some logue hosts send this to oscillators): matched to
// the voice playing that note at the next render
__attribute__((used)) void unit_note_on(uint8_t note, uint8_t velocity) {
  (void)velocity;
  s_noteOnPend = note;
}

// Note events per voice
__attribute__((used)) void unit_osc_voice_event(uint8_t event, uint8_t voice, uint8_t note, uint8_t velocity) {
  (void)note;
  (void)velocity;
  if (voice >= kMk2MaxVoices) return;
  s_events_seen = true;
  switch (event) {
    case k_voice_event_allocation:
    case k_voice_event_steal:
      // silent voice: clean start. Ringing voice: keep phases.
      // Key still held: legato (RETRIG decides). Applied at the next render.
      requestStart(voice, !s_active[voice], s_active[voice] && s_held[voice], kSrcEvent);
      s_active[voice] = true;
      s_held[voice] = true;
      break;
    case k_voice_event_release:
      s_held[voice] = false;  // keep rendering through the amp release
      break;
    case k_voice_event_deallocation:
      s_active[voice] = false;
      s_held[voice] = false;
      break;
    default:
      break;
  }
}

// Virtual patch: modulation data and destination names
__attribute__((used)) void unit_platform_exclusive(uint8_t messageId, void *data, uint32_t dataSize) {
  (void)dataSize;
  switch (messageId) {
    case kMk2PlatformExclusiveModData: {
      const unit_runtime_osc_context_t *ctx = context();
      const int n = voiceCount(ctx);
      const int32_t *index = GetModIndex(data);
      const float *depth = GetModDepth(data);
      for (int d = 0; d < kNumDest; ++d)
        for (int v = 0; v < kMk2MaxVoices; ++v) s_mod[d][v] = 0.f;
      for (int src = 0; src < kNumMk2ModSrc; ++src) {
        const int32_t dest = index[src];
        if (dest < 0 || dest >= kNumDest) continue;
        float amt = depth[src];
        if (dest == kDestPitch) amt = amt * (amt < 0.f ? -amt : amt) * kPitchModRange;
        const float *mod = GetModSourceData(data, src, ctx->voiceLimit, 0);
        for (int v = 0; v < n; ++v) s_mod[dest][v] += mod[v] * amt;
      }
      break;
    }
    case kMk2PlatformExclusiveModDestName: {
      // data[0] holds the destination index; the name is written from data[1]
      char *name = GetModDestNameData(data);
      switch (name[0]) {
        case kDestTimbre: writeName(name + 1, "Timbre"); break;
        case kDestDrive: writeName(name + 1, "Drive"); break;
        case kDestChar: writeName(name + 1, "Charactr"); break;
        case kDestSub: writeName(name + 1, "Sub"); break;
        case kDestPitch: writeName(name + 1, "Pitch"); break;
        default: break;
      }
      break;
    }
    default:
      break;
  }
}
