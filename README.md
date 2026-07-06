# MasterDesk

**An analog-modelled mastering console in a single plugin** — inspired by classic
one-knob-per-job mastering hardware. Four main gestures (Volume, Foundation,
Tone, THD) drive a complete 64-bit mastering chain: dynamic low-end
architecture, morphing tone voicings, a program-dependent glue compressor, an
oversampled harmonic stage with component-level analog behaviour, stereo
enhancement, and a true-peak lookahead limiter — watched over by the big
**Dynamic Range** needle meter from the reference faceplate.

```
IN ─ Volume ─▶ [L/R · M/S · dual-mono] ─▶ Foundation ─▶ Tone ─▶ Compressor
   ─▶ Stereo Enhance ─▶ analog floor (noise / crosstalk / PSU) ─▶ ▲THD (2–16× OS)▲
   ─▶ Output Trim + Auto-Gain ─▶ True-Peak Limiter ─▶ Wet/Dry (latency-aligned) ─▶ OUT
```

---

## Building

Requires CMake ≥ 3.22, a C++20 compiler, and (on Linux) the usual JUCE
dependencies (`libasound2-dev libx11-dev libxext-dev libxrandr-dev
libxinerama-dev libxcursor-dev libfreetype-dev libfontconfig1-dev
libgl1-mesa-dev`). JUCE 8.0.14 is fetched automatically.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure   # offline DSP verification
```

| Format     | How |
|------------|-----|
| VST3       | built everywhere by default |
| Standalone | built everywhere by default |
| AU         | built automatically on macOS |
| AAX        | set `-DMASTERDESK_AAX_SDK_PATH=/path/to/AAX_SDK` (needs Avid SDK + PACE signing) |
| CLAP       | `-DMASTERDESK_BUILD_CLAP=ON` (fetches clap-juce-extensions) |

Options: `MASTERDESK_ENABLE_OPENGL` (default ON) attaches a GPU context to the
UI; `MASTERDESK_BUILD_TESTS` (default ON) builds the console verification app.

### Windows installer

CI (`.github/workflows/windows-installer.yml`) builds MasterDesk with MSVC on
every push, runs the DSP test suite, and packages
`MasterDesk-<version>-Windows-Setup.exe` with Inno Setup — grab it from the
workflow run's artifacts, or from the GitHub release when a `v*` tag is
pushed. The installer offers VST3 (→ `C:\Program Files\Common Files\VST3`),
CLAP (→ `...\Common Files\CLAP`) and the Standalone app as components, and
leaves user presets in place on uninstall.

To build it locally on a Windows machine instead:

```powershell
cmake -B build -A x64 -DMASTERDESK_BUILD_CLAP=ON
cmake --build build --config Release --target MasterDesk_VST3 MasterDesk_Standalone MasterDesk_CLAP
iscc installer\MasterDesk.iss   # → installer\Output\MasterDesk-*-Setup.exe
```

The binaries are unsigned, so SmartScreen will warn on first run
("More info → Run anyway"); code-signing has to happen outside this repo.

---

## Controls

### Faceplate
| Control | Range | What it does |
|---|---|---|
| **1. Volume** | ±12 dB | Drives the programme into the fixed-threshold chain — one knob for level *and* glue amount |
| **2. Foundation** | 0–10 | Dynamic low-end: ZDF shelf + resonance, ducked by incoming bass energy, thickened by a hysteretic transformer model |
| **3. Tone** | 0–100 % | Morphs from flat to the selected voicing (A modern · B presence · C air · D warm) |
| **THD** | −90…−24 dB | Target harmonic level of the oversampled saturation stage |
| **Stereo Enhance** | 0–100 % | M/S width on the side signal, bass kept mono below ~120 Hz |
| **Output Trim** | ±12 dB | Post-chain, pre-limiter trim |
| **Compressor** | SOFT / HARD | 2:1 wide-knee glue vs 4:1 punch |
| **Meter** | — | Dynamic-range needle (crest factor, 3 s window) with DR LCD; click for the spectrum analyser |

### Toolbar
Presets (factory + user, prev/next/save) · A/B compare with copy · Undo/Redo ·
Oversampling Off/2×/4×/8×/16× · min-phase / linear-phase anti-aliasing ·
Stereo / Mid-Side / Dual-Mono processing · true-peak limiting on/off ·
auto-gain · external sidechain + SC high-pass · Analog amount · virtual
Temperature · Mix · Ceiling · latency-compensated Bypass.

Right-click any knob to **lock** it (locked parameters survive preset
browsing) or reset it to default.

---

## Design goals → implementation

### Sound quality
- **No unwanted artifacts / low aliasing** — the nonlinear THD stage runs
  inside a 2–16× oversampled section (polyphase half-band IIR or equiripple
  FIR); saturation is soft (`tanh` family), LF-weighted, and level-scaled.
- **Intelligent anti-alias filtering** — choose minimum-phase (lowest
  latency) or linear-phase (mix-safe) AA filters; integer-latency mode keeps
  the parallel dry path sample-aligned either way.
- **Transparent when desired** — Analog 0 % + THD −90 dB + mix logic gives a
  bit-exact dry path (verified by the test suite to < 1e−9).
- **Beautiful coloration when desired** — natural harmonics whose level and
  even/odd balance move with programme level, bias and temperature.
- **Transient preservation** — 2 ms lookahead limiting with a sliding-window
  maximum: gain lands *before* the transient instead of clipping it.
- **Phase coherence** — ZDF (TPT) filters throughout; optional fully
  linear-phase oversampling; latency-aligned wet/dry mix.
- **High internal precision** — the entire chain is 64-bit double; the plugin
  reports native double-precision support to hosts.
- **Proper gain staging** — fixed-threshold chain driven by Volume, saturation
  make-up, compressor auto-makeup, and measured loudness-matched auto-gain.

### Analog behaviour model
- **Randomised component tolerances** *(the "TOL inside" badge)* — every
  instance seeds per-channel ±0.8 % deviations applied to filter corners,
  gains and thresholds; the seed is saved with the session so "your unit"
  stays yours.
- **Micro-component drift & temperature** — bounded random walks scaled by a
  virtual 15–45 °C operating temperature that also biases the tube stage and
  raises the noise floor ~2 dB / 10 °C.
- **Power-supply sag** — the virtual rail droops (~40 ms) under sustained
  programme energy and recovers (~400 ms); saturation headroom follows it.
- **Transformer hysteresis** — the Foundation low band passes a magnetic-core
  model whose flux state lags the signal (rate-dependent loop), plus
  charge-modelled coupling capacitors (DC blockers whose pole moves with
  accumulated charge).
- **Tube bias interaction** — the shaper's operating point shifts with a slow
  programme envelope and the analog bias → level-dependent even harmonics.
- **Memory effects** — the waveshaper input includes nonlinear feedback of its
  previous output (hysteretic transfer), and compressor/limiter release is
  history-dependent.
- **Frequency-dependent saturation** — pre/de-emphasis tilt drives lows into
  the shaper harder than highs (console/iron behaviour, not fuzz).
- **Noise modelled after hardware** — decorrelated pink-ish channel noise at
  ≈ −114 dBFS nominal; **crosstalk** is HF-weighted L↔R bleed at ≈ −66 dB.
- All of it scales with one **Analog** control and disables completely at 0 %.

### Dynamics
- **Program-dependent attack/release** — compressor and limiter both run dual
  release envelopes blended by how long gain reduction has been sustained;
  compressor attack shortens on transient-dense material.
- **Feedback-topology detector** — the compressor keys on a blend of input
  and previous output (nonlinear feedback simulation).
- **True-peak detection & limiting** — 4× polyphase windowed-sinc
  interpolation in the limiter detector (BS.1770-style) plus an exact ceiling
  guard; switchable.
- **High-quality lookahead** — monotonic-wedge sliding maximum, O(1) per
  sample, allocation-free.
- **Sidechain** — internal key post-Volume, or the external stereo sidechain
  bus, both through a 20–500 Hz high-pass.
- **Adaptive processing** — Foundation ducks its own boost against incoming
  bass energy; harmonic content follows programme level (frequency-masking-
  aware low-end: boost yields when the band is already dense).

### Metering
- **LUFS** — full ITU-R BS.1770-4 K-weighting, momentary / short-term /
  integrated with two-stage gating (absolute −70 LUFS, relative −10 LU) in
  constant memory.
- **RMS / peak / true peak** — 300 ms RMS, decaying sample peak, 4×
  interpolated true peak with hold.
- **DR meter** — crest-factor dynamic range over 3 s driving the needle and
  the `DR:` LCD, with green comfort / red over-compression zones.
- **Spectrum** — click the meter face: 4096-point FFT, log frequency, peak
  hold with decay.

### Workflow & UI
- Vector-drawn, **fully resizable** (fixed aspect), **HiDPI/Retina** clean at
  any scale, optional **OpenGL** acceleration, window size persists.
- **Presets** — 8 factory styles + user presets on disk; **A/B** with copy;
  **Undo/Redo** history; **parameter locking** while browsing.
- **Automation** — every continuous parameter is sample-ramped (zipper-free);
  filter updates are control-rate interpolated in 32-sample steps.
- **Well-designed defaults** — insert it, push Volume, done.
- **Mid/Side and Dual-Mono** processing domains, multiband-style low-band
  dynamics via Foundation, wet/dry **Mix** with phase-aligned dry path,
  latency-compensated bypass.
- **CPU / stability** — allocation-free audio thread after prepare (all
  oversamplers and per-rate saturators pre-built; switching never allocates),
  denormal-safe everywhere, bounded meters, sub-block (64-sample) control
  updates for large host buffers.
- **Low latency whenever possible** — latency = limiter lookahead (2 ms) +
  the chosen AA filter; min-phase 2× keeps it to a few ms and the host is
  always told the exact figure (including changes, asynchronously).

### Honest notes (what is approximated)
- The analog stages are *behavioural* component-level models (tolerances,
  sag, hysteresis, bias, drift as state equations), not SPICE netlists or
  dynamic convolution captures.
- Psychoacoustic loudness handling is provided as measured LUFS-matched
  auto-gain; there is no separate equal-loudness monitoring curve.
- The Foundation stage gives dedicated low-band dynamics; the compressor
  itself is single-band by design (mastering glue).
- Aliasing from the gentle limiter ceiling knee and the LF transformer model
  is negligible by construction but not oversampled.

---

## Testing

`tests/DspTests.cpp` runs the whole chain headless and asserts:
1. finite, ceiling-compliant output under hot drive (−1 dBTP respected),
2. bit-exact latency-aligned dry path at mix 0,
3. monotonic harmonic generation from the THD control (H3 ≥ +12 dB span,
   clean below −70 dBc at minimum),
4. modelled noise floor between −160 and −100 dBFS when idle,
5. crest-factor reduction when Volume drives the compressor.

## License

The source in `src/` and `tests/` is provided under GPLv3 (it links JUCE
under its AGPLv3/GPL-compatible open-source terms). Commercial distribution
requires a JUCE licence.
