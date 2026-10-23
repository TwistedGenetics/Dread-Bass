// Desktop test harness for Dread Bass.
// Renders the DSP without any Korg runtime and checks: NaN / clipping / DC on
// every mode and on random settings, tuning, that the sub stays clean under
// full drive, knock / pluck / retrig behaviour, clicks on legato notes,
// zipper noise under fast modulation, level balance across modes, that the
// 4-lane (microKORG 2) and 1-lane builds match, then writes WAV demos of the
// README starter settings to test/out/.
//
// Build & run:  ./test/run.sh

#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "dreadbass.h"

using namespace dread;
typedef Params P;

static const int kRate = 48000;
static int failures = 0;

static void writeWav(const std::string &path, const std::vector<float> &x) {
  FILE *f = fopen(path.c_str(), "wb");
  if (!f) return;
  const uint32_t n = x.size(), data = n * 2, riff = 36 + data;
  const uint16_t pcm = 1, ch = 1, bits = 16, align = 2;
  const uint32_t sr = kRate, br = kRate * 2, fmt = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
  fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  for (float v : x) {
    int16_t s = (int16_t)std::lrint(std::fmax(-1.f, std::fmin(1.f, v)) * 32767.f);
    fwrite(&s, 2, 1, f);
  }
  fclose(f);
}

static void result(bool ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void result(bool ok, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char buf[256];
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  printf("%-68s %s\n", buf, ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

struct Stats { float peak = 0, dc = 0, rms = 0; bool bad = false; };
static Stats stats(const std::vector<float> &x, size_t from = 0, size_t to = 0) {
  if (!to || to > x.size()) to = x.size();
  Stats s; double sum = 0, sq = 0;
  for (size_t i = from; i < to; ++i) {
    const float v = x[i];
    if (!std::isfinite(v)) s.bad = true;
    s.peak = std::fmax(s.peak, std::fabs(v));
    sum += v; sq += double(v) * v;
  }
  const double n = double(to - from);
  s.dc = sum / n; s.rms = std::sqrt(sq / n);
  return s;
}

// ---- settings ----
struct Set {
  int timbre = 650, drive = 25, mode = 0, chr = 60, sub = 55, subOct = 0, knock = 0, pluck = 0;
  int egInt = 0, egTime = 300, retrig = 1, semi = 0, fine = 0;
};
template <class E> static void apply(E &e, const Set &s) {
  const int v[P::kNumParams] = {s.timbre, s.drive, s.mode, s.chr, s.sub, s.subOct, s.knock,
                                s.pluck, s.egInt, s.egTime, s.retrig, s.semi, s.fine};
  for (int i = 0; i < P::kNumParams; ++i) e.setParam(i, v[i]);
}

// A note event inside a render: at sample `at`, lane 0 gets `note`;
// fresh = the voice was silent (allocation) or not (steal / legato)
struct Ev { int at; float note; bool fresh; };

static std::vector<float> render1(const Set &s, float note, int samples, std::vector<Ev> evs = {},
                                  int blk = 64) {
  DreadBass1 e;
  e.init();
  apply(e, s);
  e.setNote(0, note);
  e.noteOn(0, true);
  std::vector<float> out;
  out.reserve(samples);
  float buf[kMaxBlock];
  size_t k = 0;
  for (int pos = 0; pos < samples;) {
    while (k < evs.size() && evs[k].at <= pos) {
      e.setNote(0, evs[k].note);
      e.noteOn(0, evs[k].fresh, !evs[k].fresh);  // not fresh = legato (key still held)
      ++k;
    }
    int len = std::min(blk, samples - pos);
    if (k < evs.size()) len = std::min(len, evs[k].at - pos);
    e.render(buf, len);
    for (int i = 0; i < len; ++i) out.push_back(buf[i]);
    pos += len;
  }
  return out;
}

// estimated fundamental from upward zero crossings (after settling)
static float pitchHz(const std::vector<float> &x, size_t from) {
  int first = -1, last = -1, n = 0;
  for (size_t i = from + 1; i < x.size(); ++i)
    if (x[i - 1] < 0.f && x[i] >= 0.f) {
      const float fr = x[i - 1] / (x[i - 1] - x[i]);
      const int idx = (int)i;
      (void)fr;
      if (first < 0) first = idx;
      last = idx;
      ++n;
    }
  if (n < 2) return 0.f;
  return (float)(n - 1) * kRate / (float)(last - first);
}

// largest jump between neighbouring samples
static float maxStep(const std::vector<float> &x, size_t from, size_t to) {
  float m = 0.f;
  for (size_t i = std::max<size_t>(from, 1); i < to && i < x.size(); ++i) m = std::fmax(m, std::fabs(x[i] - x[i - 1]));
  return m;
}

// energy above ~8 kHz (first difference twice, crude high-pass)
static float hfRms(const std::vector<float> &x, size_t from, size_t to) {
  double sq = 0; size_t n = 0;
  for (size_t i = std::max<size_t>(from, 2); i < to && i < x.size(); ++i) {
    const float d = x[i] - 2.f * x[i - 1] + x[i - 2];
    sq += double(d) * d; ++n;
  }
  return n ? std::sqrt(sq / n) : 0.f;
}

static const char *kModeNames[] = {"DREAD", "DONK", "GROWL", "HOOVER"};

int main() {
  const int sec = kRate;
  if (std::system("mkdir -p test/out") != 0) return 1;

  printf("== every mode, default settings, C1..C4 ==\n");
  float modeRms[4] = {0, 0, 0, 0};
  for (int m = 0; m < 4; ++m) {
    for (float note : {24.f, 36.f, 48.f, 60.f}) {
      Set s; s.mode = m; s.sub = 0;
      const auto x = render1(s, note, sec);
      const Stats st = stats(x, 4800);
      result(!st.bad && st.peak <= 1.f && std::fabs(st.dc) < 0.01f && st.rms > 0.05f,
             "%-6s note %2.0f  peak %.3f rms %.3f dc %+.4f", kModeNames[m], note, st.peak, st.rms, st.dc);
      if (note == 36.f) modeRms[m] = st.rms;
    }
  }
  {
    // DONK is a resonant pluck, so it's levelled by its peak, not its RMS:
    // the sustained modes are compared by RMS, DONK's hit must peak high
    // without being flattened by the output limiter
    float lo = 1e9, hi = 0;
    for (int m = 0; m < 4; ++m) {
      if (m == P::kDonk) continue;
      lo = std::fmin(lo, modeRms[m]);
      hi = std::fmax(hi, modeRms[m]);
    }
    result(hi / lo < 1.41f, "mode loudness spread at C2 (DREAD/GROWL/HOOVER): %.2f dB", 20.f * std::log10(hi / lo));
    Set d; d.mode = P::kDonk; d.chr = 80; d.sub = 20; d.egTime = 220; d.drive = 0;
    const auto x = render1(d, 36.f, sec / 2);
    const Stats st = stats(x, 0, 4800);
    float over = 0.f;
    for (int i = 0; i < 4800; ++i) over += std::fabs(x[i]) > 0.8f;
    result(st.peak > 0.6f && over / 4800.f < 0.02f, "DONK hit peak %.2f, %.1f %% of samples in the limiter knee", st.peak,
           100.f * over / 4800.f);
  }

  printf("== tuning (EG INT 0, sine) ==\n");
  for (float note : {28.f, 36.f, 45.f, 57.f}) {
    Set s; s.mode = P::kGrowl; s.timbre = 0; s.drive = 0; s.chr = 0; s.sub = 0; s.egInt = 0;
    const auto x = render1(s, note, sec);
    const float want = 440.f * std::pow(2.f, (note - 69.f) / 12.f);
    const float got = pitchHz(x, 4800);
    const float cents = 1200.f * std::log2(got / want);
    result(std::fabs(cents) < 3.f, "note %2.0f  want %.2f Hz got %.2f Hz (%+.1f cents)", note, want, got, cents);
  }
  {
    Set s; s.mode = P::kGrowl; s.timbre = 0; s.drive = 0; s.chr = 0; s.sub = 0; s.egInt = 0; s.semi = -12; s.fine = 50;
    const auto x = render1(s, 48.f, sec);
    const float want = 440.f * std::pow(2.f, (36.5f - 69.f) / 12.f);
    const float got = pitchHz(x, 4800);
    result(std::fabs(1200.f * std::log2(got / want)) < 3.f, "SEMI -12 FINE +50: want %.2f got %.2f Hz", want, got);
  }
  {
    // sub alone: SUB 100 %, main silent is not possible, so compare 1 vs 2 octave
    Set s; s.timbre = 0; s.drive = 0; s.sub = 100; s.egInt = 0;
    s.subOct = 1;
    const auto x = render1(s, 48.f, sec);
    result(stats(x, 4800).peak <= 1.f, "SUB -2 OCT renders in range (peak %.3f)", stats(x, 4800).peak);
  }

  printf("== drive: gain compensated, sub stays clean ==\n");
  for (int m = 0; m < 4; ++m) {
    Set a; a.mode = m; a.drive = 0; a.chr = 0; a.sub = 0;
    Set b = a; b.drive = 100;
    const Stats sa = stats(render1(a, 36.f, sec), 4800), sb = stats(render1(b, 36.f, sec), 4800);
    const float db = 20.f * std::log10(sb.rms / sa.rms);
    result(sb.peak <= 1.f && std::fabs(db) < 6.f, "%-6s drive 0 -> 100: %+.1f dB, peak %.3f", kModeNames[m], db, sb.peak);
  }
  {
    // with the main osc turned into a near-square by full drive, the sub's
    // contribution must still be a sine: the difference between SUB 100 and
    // SUB 0 renders should have almost no energy above a few harmonics
    Set a; a.mode = P::kGrowl; a.timbre = 1023; a.drive = 100; a.chr = 100; a.sub = 0; a.egInt = 0;
    Set b = a; b.sub = 100;
    const auto xa = render1(a, 36.f, sec), xb = render1(b, 36.f, sec);
    std::vector<float> d(xa.size());
    for (size_t i = 0; i < d.size(); ++i) d[i] = xb[i] - xa[i];
    const float r = stats(d, 4800).rms, h = hfRms(d, 4800, d.size());
    result(h < r * 0.002f, "sub difference is a pure sine (hf/rms %.5f)", h / r);
  }

  printf("== envelopes ==\n");
  {
    // EG INT +24, 40 ms: pitch starts high and settles
    Set s; s.mode = P::kGrowl; s.timbre = 0; s.drive = 0; s.sub = 0; s.egInt = 24; s.egTime = 40;
    const auto x = render1(s, 36.f, sec);
    std::vector<float> head(x.begin(), x.begin() + 960), tail(x.begin() + 24000, x.end());
    const float f0 = pitchHz(head, 0), f1 = pitchHz(tail, 0);
    const float want = 440.f * std::pow(2.f, (36.f - 69.f) / 12.f);
    result(f0 > want * 1.8f && std::fabs(1200.f * std::log2(f1 / want)) < 3.f,
           "EG INT +24: first 20 ms %.0f Hz, settles at %.1f Hz", f0, f1);
  }
  {
    Set s; s.pluck = 80; s.sub = 60;
    const auto x = render1(s, 36.f, sec);
    const float a = stats(x, 0, 2400).rms, b = stats(x, 24000, 28800).rms;
    result(b < a * 0.05f, "PLUCK 80: level after 0.5 s is %.1f dB", 20.f * std::log10(b / a));
    Set n; n.pluck = 0;
    const auto y = render1(n, 36.f, sec);
    result(stats(y, 24000, 28800).rms > stats(y, 2400, 7200).rms * 0.8f, "PLUCK 0 sustains");
  }
  {
    Set s; s.knock = 100; s.sub = 0; s.egInt = 0; s.mode = P::kDonk;
    Set n = s; n.knock = 0;
    const auto a = render1(s, 36.f, sec), b = render1(n, 36.f, sec);
    std::vector<float> d(a.size());
    for (size_t i = 0; i < d.size(); ++i) d[i] = a[i] - b[i];
    const float head = stats(d, 0, 480).rms, tail = stats(d, 9600, 14400).rms;
    result(head > 0.03f && tail < 1e-4f, "KNOCK: blip rms %.3f in first 10 ms, gone after 200 ms (%.1e)", head, tail);
  }

  {
    // no note start ever arrives (host sends nothing): PLUCK must not fade
    // the voice out for good
    DreadBass1 e;
    e.init();
    Set s; s.pluck = 90;
    apply(e, s);
    e.setNote(0, 36.f);
    std::vector<float> x;
    float buf[kMaxBlock];
    for (int b = 0; b < 1500; ++b) { e.render(buf, 64); for (int i = 0; i < 64; ++i) x.push_back(buf[i]); }
    result(stats(x, 80000, 96000).rms > 0.05f, "PLUCK without any note start: still sounding (rms %.3f)", stats(x, 80000, 96000).rms);
  }
  {
    // DONK: the resonance rings on the 5th harmonic, then fades in place
    Set s; s.mode = P::kDonk; s.timbre = 700; s.chr = 80; s.sub = 0; s.egTime = 220; s.drive = 0;
    const auto x = render1(s, 37.f, kRate);
    auto band = [&](size_t a, size_t b, float hz) {  // level of one frequency (Goertzel)
      double c = 2.0 * std::cos(6.2831853 * hz / kRate), s1 = 0, s2 = 0;
      for (size_t i = a; i < b; ++i) { const double v = x[i] + c * s1 - s2; s2 = s1; s1 = v; }
      return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / double(b - a);
    };
    const float f0 = 440.f * std::pow(2.f, (37.f - 69.f) / 12.f);
    // compare with the same note at no resonance: the 5th must ring well
    // above it during the hit and be back near it once the ring has faded
    Set n = s; n.chr = 0;
    const auto y = render1(n, 37.f, kRate);
    auto bandOf = [&](const std::vector<float> &v, size_t a, size_t b, float hz) {
      double c = 2.0 * std::cos(6.2831853 * hz / kRate), s1 = 0, s2 = 0;
      for (size_t i = a; i < b; ++i) { const double q = v[i] + c * s1 - s2; s2 = s1; s1 = q; }
      return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2);
    };
    auto rel = [&](const std::vector<float> &v, size_t a, size_t b) {
      return 20.f * std::log10(bandOf(v, a, b, 5 * f0) / bandOf(v, a, b, f0));
    };
    (void)band;
    // (the reference has the 5th within a few dB of the fundamental during
    // the ring, ~-14 dB for a plain square, and back down after it)
    const float early = rel(x, 1440, 3840), late = rel(x, 16800, 19200);
    const float plain = rel(y, 1440, 3840);
    result(early > -4.f && early - plain > 3.f && late < -10.f,
           "DONK 5th vs fundamental: %+.1f dB at 30-80 ms (no resonance: %+.1f), %+.1f dB at 350-400 ms", early, plain, late);
  }

  printf("== legato / steal ==\n");
  for (int m = 0; m < 4; ++m) {
    // pluck has decayed; a legato note restarts it: must not click
    Set s; s.mode = m; s.pluck = 70; s.retrig = 1; s.knock = 0; s.drive = 40;
    const int at = 24000 + 17;  // mid-block
    const auto x = render1(s, 36.f, sec, {{at, 39.f, false}});
    const float base = maxStep(x, 4800, at - 100);
    const float around = maxStep(x, at - 2, at + 200);
    // the restart rises to full level, so allow bigger steps than the decayed tail,
    // but nothing like a jump to full scale in one sample
    // reference: the same new note played fresh (steep waveforms are not clicks)
    const auto y = render1(s, 39.f, 9600);
    const float full = maxStep(y, 1, 4800);
    result(around < full * 1.2f + 0.02f, "%-6s retrig legato: max step %.3f (decayed %.3f, fresh %.3f)",
           kModeNames[m], around, base, full);
  }
  {
    // RETRIG off, but the key was released (voice still ringing): a new
    // note must still restart the envelopes
    Set s; s.pluck = 70; s.retrig = 0; s.sub = 60;
    DreadBass1 e;
    e.init();
    apply(e, s);
    e.setNote(0, 36.f);
    e.noteOn(0, true);
    std::vector<float> x;
    float buf[kMaxBlock];
    for (int b = 0; b < 750; ++b) {
      if (b == 375) { e.setNote(0, 39.f); e.noteOn(0, false, false); }
      e.render(buf, 64);
      for (int i = 0; i < 64; ++i) x.push_back(buf[i]);
    }
    result(stats(x, 24000 + 480, 24000 + 2880).rms > stats(x, 480, 2880).rms * 0.7f,
           "RETRIG off, released key: new note restarts PLUCK");
  }
  {
    Set s; s.pluck = 70; s.retrig = 0;
    const int at = 24000;
    const auto x = render1(s, 36.f, sec, {{at, 39.f, false}});
    result(stats(x, at + 2400, at + 4800).rms < stats(x, 0, 4800).rms * 0.2f,
           "RETRIG off: legato note keeps decaying");
  }

  printf("== zipper: fast modulation ==\n");
  for (int m = 0; m < 4; ++m) {
    // TIMBRE swept 0 -> 100 % and back 40 times a second by a mod source,
    // compared with the same note at fixed TIMBRE 0, 50 and 100 %: the sweep
    // must not add jumps the static sounds don't have
    auto run = [&](int modulate, float fixed) {
      DreadBass1 e;
      e.init();
      Set s; s.mode = m; s.sub = 0; s.egInt = 0; s.timbre = 0;
      apply(e, s);
      e.setNote(0, 33.f);
      e.noteOn(0, true);
      std::vector<float> x;
      float buf[kMaxBlock];
      for (int b = 0; b < 750; ++b) {
        const float ph = (float)b / 750.f;
        const float mod = modulate ? 0.5f + 0.5f * std::sin(6.2831853f * ph * 40.f) : fixed;
        e.setMod(0, mod, 0.f, 0.f, 0.f, 0.f);
        e.render(buf, 64);
        for (int i = 0; i < 64; ++i) x.push_back(buf[i]);
      }
      return x;
    };
    float base = 0.f;
    for (float f : {0.f, 0.5f, 1.f}) base = std::fmax(base, maxStep(run(0, f), 100, 36000));
    const auto x = run(1, 0.f);
    const Stats st = stats(x);
    const float step = maxStep(x, 100, x.size());
    result(!st.bad && st.peak <= 1.f && step < base * 1.25f + 0.01f, "%-6s fast TIMBRE mod: max step %.3f (static %.3f)",
           kModeNames[m], step, base);
  }

  printf("== random settings (fuzz) ==\n");
  {
    srand(1234);
    int bad = 0, total = 0;
    float worst = 0.f;
    for (int r = 0; r < 300; ++r) {
      Set s;
      s.timbre = rand() % 1024; s.drive = rand() % 101; s.mode = rand() % 4; s.chr = rand() % 101;
      s.sub = rand() % 101; s.subOct = rand() % 2; s.knock = rand() % 101; s.pluck = rand() % 101;
      s.egInt = -24 + rand() % 73; s.egTime = 1 + rand() % 1000; s.retrig = rand() % 2;
      s.semi = -24 + rand() % 49; s.fine = -50 + rand() % 101;
      const float note = (float)(rand() % 128);
      const auto x = render1(s, note, 9600, {{3000, (float)(rand() % 128), false}, {6000, note, (rand() & 1) != 0}},
                             1 + rand() % 64);
      const Stats st = stats(x);
      ++total;
      worst = std::fmax(worst, st.peak);
      if (st.bad || st.peak > 1.f || std::fabs(st.dc) > 0.2f) {
        ++bad;
        printf("  bad: mode %d timbre %d drive %d char %d sub %d knock %d pluck %d egint %d egtime %d note %.0f  nan %d dc %.3f\n",
               s.mode, s.timbre, s.drive, s.chr, s.sub, s.knock, s.pluck, s.egInt, s.egTime, note, (int)st.bad, st.dc);
      }
    }
    result(bad == 0, "%d random settings / notes / block sizes: %d bad, worst peak %.3f", total, bad, worst);
  }

  printf("== 4-lane build matches 1-lane ==\n");
  {
    float worst = 0.f;
    for (int m = 0; m < 4; ++m) {
      Set s; s.mode = m; s.knock = 60; s.pluck = 30; s.egInt = 12; s.egTime = 60; s.drive = 60; s.chr = 50;
      float md = 0.f;
      DreadBass4 e4;
      e4.init();
      apply(e4, s);
      const float notes[4] = {24.f, 31.f, 36.f, 43.f};
      std::vector<float> ref[4];
      for (int j = 0; j < 4; ++j) {
        e4.setNote(j, notes[j]);
        e4.noteOn(j, true);
        ref[j] = render1(s, notes[j], 9600);
      }
      v4f buf[kMaxBlock];
      for (int pos = 0; pos < 9600; pos += 64) {
        e4.render(buf, 64);
        for (int i = 0; i < 64; ++i)
          for (int j = 0; j < 4; ++j) md = std::fmax(md, std::fabs(buf[i][j] - ref[j][pos + i]));
      }
      printf("  %-6s %.2e\n", kModeNames[m], md);
      worst = std::fmax(worst, md);
    }
    result(worst < 1e-4f, "max difference over all modes: %.2e", worst);
  }

  printf("== speed (desktop, relative only) ==\n");
  for (int m = 0; m < 4; ++m) {
    Set s; s.mode = m; s.knock = 50;
    DreadBass4 e4[2];
    for (auto &e : e4) {
      e.init();
      apply(e, s);
      for (int j = 0; j < 4; ++j) { e.setNote(j, 30.f + j); e.noteOn(j, true); }
    }
    v4f buf[kMaxBlock];
    const int blocks = 20000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) {
      e4[0].render(buf, 64);
      e4[1].render(buf, 64);
      if (b % 4000 == 0) for (auto &e : e4) for (int j = 0; j < 4; ++j) e.noteOn(j, false);
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    printf("  %-6s 8 voices: %.2f us per 64-sample buffer (buffer = 1333 us)\n", kModeNames[m], us / blocks);
  }

  printf("== demos -> test/out/*.wav ==\n");
  {
    // Each demo is a short line played the way the synth would: notes
    // are gated by an amp envelope (1 ms attack, 25 ms release), a note that
    // starts after the last one has died is a fresh voice, an overlapping
    // one is legato. Steps are 16ths.
    struct Step { int at, len; float note; };
    struct Demo { const char *name; Set s; std::vector<Step> steps; };
    std::vector<Demo> demos;
    std::vector<int> bpm;
    // Starter settings from the README. dred_bass and donk follow the
    // reference loops' tempo, timing and pitches.
    { Set s; s.mode = P::kDread; s.timbre = 700; s.drive = 0; s.chr = 55; s.sub = 40; s.pluck = 0; s.egInt = 0; s.egTime = 300;
      demos.push_back({"dred_bass", s, {{0, 4, 36}, {6, 4, 36}, {12, 4, 36}, {16, 8, 34}, {32, 5, 31}, {48, 5, 31}, {54, 5, 30}}});
      bpm.push_back(175); }
    { Set s; s.mode = P::kDread; s.timbre = 520; s.drive = 30; s.chr = 70; s.sub = 70; s.pluck = 0; s.egInt = 0; s.egTime = 170;
      demos.push_back({"dread_roller", s, {{0, 3, 31}, {3, 3, 31}, {6, 2, 34}, {8, 4, 31}, {12, 4, 29},
                                           {16, 3, 31}, {19, 3, 31}, {22, 2, 26}, {24, 4, 29}, {28, 4, 31}}});
      bpm.push_back(170); }
    { Set s; s.mode = P::kDonk; s.timbre = 700; s.drive = 0; s.chr = 80; s.sub = 20; s.pluck = 0; s.egInt = 0; s.egTime = 220;
      demos.push_back({"donk", s, {{0, 2, 29}, {3, 1, 29}, {6, 2, 41}, {8, 2, 29}, {10, 1, 32}, {11, 2, 29}, {14, 2, 36},
                                   {16, 2, 29}, {19, 1, 29}, {22, 2, 41}, {24, 2, 34}, {26, 2, 32}, {28, 2, 29}, {30, 2, 27}}});
      bpm.push_back(160); }
    { Set s; s.mode = P::kGrowl; s.timbre = 700; s.drive = 45; s.chr = 55; s.sub = 55; s.egInt = 5; s.egTime = 250;
      demos.push_back({"snarl_fm", s, {{0, 4, 29}, {4, 4, 29}, {8, 2, 32}, {10, 6, 29}, {16, 4, 27}, {20, 4, 29}, {24, 8, 24}}});
      bpm.push_back(170); }
    { Set s; s.mode = P::kHoover; s.timbre = 800; s.drive = 20; s.chr = 60; s.sub = 35; s.egInt = 12; s.egTime = 300;
      demos.push_back({"hoover_dive", s, {{0, 8, 45}, {8, 8, 48}, {16, 8, 43}, {24, 8, 41}}});
      bpm.push_back(170); }

    for (size_t di = 0; di < demos.size(); ++di) {
      auto &d = demos[di];
      const int step = kRate * 60 / bpm[di] / 4;
      int lastStep = 0;
      for (auto &st : d.steps) lastStep = std::max(lastStep, st.at + st.len);
      lastStep = std::max(lastStep, 32);
      DreadBass1 e;
      e.init();
      apply(e, d.s);
      const int total = step * lastStep + kRate / 4;
      std::vector<float> x;
      x.reserve(total);
      float buf[kMaxBlock];
      float amp = 0.f, gate = 0.f;
      int gateEnd = -1;
      size_t k = 0;
      const float aUp = 1.f - std::exp(-1.f / (0.001f * kRate)), aDn = 1.f - std::exp(-1.f / (0.025f * kRate));
      for (int pos = 0; pos < total;) {
        if (k < d.steps.size() && d.steps[k].at * step <= pos) {
          const bool sounding = amp > 1e-2f;  // the synth frees a voice once its release is done
          const bool held = pos < gateEnd;
          e.setNote(0, d.steps[k].note);
          e.noteOn(0, !sounding, held);
          gate = 1.f;
          gateEnd = (d.steps[k].at + d.steps[k].len) * step - step / 8;  // slight gap, unless tied
          ++k;
        }
        int len = std::min((int)kMaxBlock, total - pos);
        if (k < d.steps.size()) len = std::min(len, d.steps[k].at * step - pos);
        if (pos < gateEnd) len = std::min(len, gateEnd - pos);
        if (pos >= gateEnd) gate = 0.f;
        e.render(buf, len);
        for (int i = 0; i < len; ++i) {
          amp += (gate > amp ? aUp : aDn) * (gate - amp);
          x.push_back(buf[i] * amp);
        }
        pos += len;
      }
      const Stats st = stats(x);
      writeWav(std::string("test/out/") + d.name + ".wav", x);
      result(!st.bad && st.peak <= 1.f, "%-16s peak %.3f rms %.3f", d.name, st.peak, st.rms);
    }
  }

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
