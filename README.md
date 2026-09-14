# Chocola

<img width="250" height="340" alt="image" src="https://github.com/user-attachments/assets/df236cd1-e2fa-4c5c-bff7-b4cd80abb151" />

A one-knob "crazy wide" stereo effect for vocals, drums, snares, claps — anything
you want to blow up into a huge stereo image. Brown-themed sibling to HYPER SCAPE.

## How it works

One knob: **MIX** (0-100%). At 0% the signal passes through untouched. Turning it
up crossfades in the full effect, which is a Shaper Box-style chain:

1. **Haas pan stage** — splits the signal into low / mid / high bands (crossovers
   at 2 kHz and 8 kHz). Lows stay centered. The mid band is sent to the right
   channel direct and to the left channel delayed 40ms. The high band is sent to
   the right channel direct and the left channel delayed 2ms. That inter-channel
   timing offset is what reads as "hard panned" without losing mono energy.
2. **Width stage** — the result is converted to Mid/Side. The side signal is
   split at 120 Hz and 6 kHz: below 120 Hz is forced mono (no mud, no phase
   cancellation), 120 Hz-6 kHz is boosted to 155% width, above 6 kHz to 210%
   width.
3. **Decorrelation stage** — the shaped side signal runs through two cascaded
   all-pass filters (700 Hz and 3 kHz). This is phase-only — no EQ coloration —
   and adds extra "bigness" on top of the Haas/M-S widening. It's then boosted
   further and run through a soft clipper (tanh) so the bigger side signal
   stays smooth instead of getting harsh or clipping.
4. **Chorus stage** — two slowly modulated ~18ms delay taps (Auto-Chroma style)
   are layered underneath, added on the left and *subtracted* on the right so
   the chorus itself contributes width rather than just thickness.

The knob is a dry/wet blend between your original signal and that fully-cooked
"crazy wide" signal, rather than scaling each stage individually — so at low
settings it's a subtle blend-in, and at 100% it's the full effect exactly as
described above.

UI is now byte-for-byte the same layout/fonts/sizes as HYPER SCAPE and
CloudOne (part of the same one-knob plugin bundle) — a black outer hardware
chassis with the coloured panel inset inside it, corner screws, a glossy
knob with tick marks and gauge-style labels, and a level-reactive LED under
"by erisa" — just recoloured brown instead of purple/pink. Double-click the
knob to reset it to 0%.

## Requirements

- CMake (3.22+)
- Git (needed so CMake can fetch JUCE automatically)
- Visual Studio Community (with the "Desktop development with C++" workload)

## Building on Windows

1. Unzip this project.
2. Open the **Developer PowerShell for VS** (Start menu → your Visual Studio version)
3. Run:

```powershell
cd C:\*YOUR-PATH*
cmake -B build
cmake --build build --config Release
```

The first build will take a while — CMake's `FetchContent` downloads JUCE itself
the first time. After that, rebuilds are much faster.

**Tip:** if you hit an out-of-memory / heap error mid-build (like on HYPER SCAPE),
try building again — sometimes it's just a low-memory moment — and consider
closing other heavy apps (browser, DAW) while it compiles, since MSVC can be
memory-hungry compiling JUCE's GUI code.

## Output location

After a successful build:

- VST3: `build\Chocola_artefacts\Release\VST3\Chocola.vst3`
- Standalone app: `build\Chocola_artefacts\Release\Standalone\Chocola.exe`

Copy the `.vst3` into your DAW's VST3 folder (usually
`C:\Program Files\Common Files\VST3`) if it isn't picked up automatically —
`COPY_PLUGIN_AFTER_BUILD` is already set in `CMakeLists.txt` so this normally
happens for you.
