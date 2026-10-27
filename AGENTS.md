# AGENTS.md

Guidance for automated agents working in this repository.

## What shakal is

A native **C++17 / Qt 6 Widgets** desktop app that drives `ffmpeg` through `QProcess` to apply one
deliberately aggressive video-degradation preset. A second, much harsher one — **Shakal ULTRA** — sits
behind a checkbox and is a joke that came out of a meme; it is not part of a graded ladder, and there
used to be a four-preset ladder here. See `## Presets`.

There is **no Python in this project**. The original `questionary` TUI was deleted on 2026-10-26 and
replaced by the Qt app. Do not look for `pyproject.toml`, `uv.lock`, or a venv, and do not propose
`uv`/`pip`/`pytest` commands — none apply.

## Commands

```sh
cmake --preset debug            # or: release
cmake --build --preset debug
ctest --test-dir build/debug --output-on-failure
./build/debug/shakal            # GUI
```

- `CMakePresets.json` pins the generator to **Unix Makefiles** — `ninja` is not installed on the
  development host.
- `ctest` target `presets` is pure logic and instant. Target `encoder` (~10 s) spawns real
  `ffmpeg`/`ffprobe` subprocesses and needs them on `PATH`.
- Host has Qt **6.12.0**, CMake 4.x, GCC/Clang. CMake 4.x rejects `cmake_minimum_required(VERSION
  <3.5)`; this project floors at 3.21.

## Layout

| Path | Role |
| --- | --- |
| `src/main.cpp` | `QApplication` bootstrap |
| `src/Presets.h` | The preset table — data only, no logic |
| `src/EncoderController.{h,cpp}` | All ffmpeg/ffprobe interaction. Owns argv assembly, progress parsing, cancel, cleanup |
| `src/MainWindow.{h,cpp}` | The form, overwrite dialog, progress bar, log pane |
| `tests/tst_presets.cpp` | argv assembly, container suffixing, preset table |
| `tests/tst_encoder.cpp` | QtTest round-trips against real ffmpeg: success, refused overwrite, mux failure, cancel |

Keep encode-path logic in `EncoderController`. `MainWindow` should stay a thin view.

## Presets

There is **one** preset, labelled `default` in `src/Presets.h`: HEVC at CRF 32 with AAC 96k. That is
what runs on every encode.

`ULTRA` is not a rung on a ladder — it is the easter egg behind the **Shakal ULTRA** checkbox, a joke
that came out of a meme in the Russian-speaking space. It is deliberately absurd rather than merely
stronger: it downscales to 320x180 at 12 fps and nearest-neighbour upscales back to 1920x1080, which
aspect-distorts any source that is not 16:9. That distortion is the point.

The table used to hold a graded four — `Light` / `Medium` / `Strong` / `ULTRA` — picked from a combo.
That ladder is gone and should not be reconstructed: `Strong` became `default`, `Light` and `Medium`
were deleted, and the combo was replaced by the checkbox. Nothing in the code, comments, tests or docs
should keep describing the current shape as a range of strengths.

## The success invariant — do not simplify

`EncoderController::onProcessFinished` treats an encode as successful only when **all three** hold:
`progress=end` was seen, **and** exit code is 0, **and** the output file exists and is non-empty.

This is not defensive padding. Each signal is individually unreliable, verified on the host's ffmpeg
(Lavc 63.1.102):

| Situation | exit | `progress=end` | output file |
| --- | --- | --- | --- |
| Refused overwrite (`-n`, target exists) | **0** | absent | non-empty (the user's pre-existing file) |
| Mux rejected (e.g. HEVC into WebM) | 234 | **present** | **262-byte stub written before the failure** |
| Graceful cancel via `q` | 0 | **present** | valid, playable, truncated |

So: exit code alone lies, `progress=end` alone lies, and file size alone lies. If you "clean up" the
condition to just check the exit code, you reintroduce silent success on failed encodes.

Related ordering constraints, both load-bearing:

1. **The cancel check must precede the success check.** A graceful `q` cancel reports `progress=end`,
   exit 0, and leaves a valid short file — without the early return, a cancelled encode is announced
   as a success.
2. **`errorOccurred` arrives before `finished`.** A cancel escalated to `terminate()`/`kill()` surfaces
   first as `QProcess::Crashed`; `onProcessError` must stay silent while `m_cancelRequested` is set so
   the outcome is reported once, as "Cancelled".

## Output-file rules

- Never pass `-nostdin`. Stdin must stay open so `q` can request a graceful stop.
- Progress is read from **stdout only** (`-progress pipe:1`); stderr is the human log pane. The two
  channels are parsed separately and never merged.
- Use `out_time_us`, not `out_time_ms`, for elapsed time.
- `-y` is passed **only** after the user explicitly confirms an overwrite; otherwise `-n` is passed so
  ffmpeg cannot hang on an interactive prompt.
- On failure or cancel, `discardOutputIfOurs()` deletes the partial file — but **never** a pre-existing
  file that `-n` protected. That distinction is what keeps a refused overwrite from destroying the
  user's earlier encode; `tst_encoder` asserts it by checking sentinel bytes survive.

## UI directives (from the user, not style preferences)

- **There is no preset picker.** One preset (`default`, CRF 32) runs on every encode, and the checkbox
  labelled **Shakal ULTRA** sitting to the right of the container combo is the only way to reach the
  ULTRA easter egg. An unchecked box always means `default`. Do not reintroduce a preset combo, do not
  add preset rungs, and do not rename the checkbox label — "Shakal ULTRA" is the user's wording, and it
  is the one place the app name appears in the UI.
- **The progress bar is a plain, full-width bar with no percentage text.** `setTextVisible(false)` is
  deliberate: the percentage reserves space and insets the groove, making the bar narrower than the log
  pane below it. Keep it spanning the full row.
- Indeterminate (gradient) mode is wanted when duration is unknown — but the bar must return to a
  determinate range when the run ends.
- ULTRA's hardcoded `scale=320:180,fps=12,scale=1920:1080` **aspect-distorts non-16:9 sources on
  purpose**. Do not "fix" it; maximum degradation is the preset's point.
- **The Input file field takes drops**, and dropping or browsing for a file fills the output directory
  from that file's own folder (`MainWindow::applyInputPath`). The drop target is the field, not the
  window, so a drop meant for another widget is never swallowed.

## Container/codec mapping (WebM)

Both table entries are written in `libx265` + AAC terms, but the WebM muxer accepts only VP8/VP9/AV1
video and Vorbis/Opus audio. Selecting WebM used to fail at mux time with exit 234 for either one.

`EncoderController::buildCommand` now re-expresses the codec flags when the container is WebM:

| Flag in the table | WebM argv |
| --- | --- |
| `-vcodec libx265` | `-vcodec libvpx-vp9` |
| `-acodec aac` | `-acodec libopus` |
| `-acodec copy` | `-acodec libopus` — stream-copying AAC into WebM cannot work. No entry asks for a copy any more; the row encodes the container rule, not a current flag. |
| `-preset fast` / `-preset ultrafast` | `-cpu-used 3` / `-cpu-used 4` |

Keep the CRF numbers and the `-vf` chain as they are: they *are* the degradation, and VP9's CRF range
(0-63) covers both values. The mapping is deliberately not a blind overwrite — a codec outside the
tables is passed through untouched so ffmpeg's own `Only VP8 or VP9...` error reaches the log pane
instead of an encoder being substituted silently. That choice is also what keeps a real mux rejection
reachable for `muxFailureRemovesJunkFile`: neither entry can fail that way any more.

`-preset` is replaced rather than kept because libvpx ignores it, emitting "Codec AVOption preset has
not been used" into the log for a flag that does nothing. `-cpu-used` is the equivalent knob (higher =
faster), and both values stay inside 0-4, which works in every libvpx rate-control mode; the table
lists only the two speed names the entries actually emit. mp4 and mkv are permissive (HEVC, and even
copied Vorbis/Opus, all mux fine on this host) and get the args unchanged.

## Out of scope

Batch/queue, folder input, user-editable presets, preset persistence, video preview,
CMake `install()` targets, `.desktop` entries, CPack packaging. Delivery is build-dir only.
Drag-and-drop is **no longer** out of scope: the Input file field takes drops by user request on
2026-10-27, and a preset *picker* is gone for good — the ULTRA checkbox replaced it the same day.

## Verifying GUI changes

A green build and passing tests are **not** sufficient to claim UI work is done — but the human check
belongs to the user, not to the agent.

1. Build, then launch the real binary on the desktop: `QT_QPA_PLATFORM=wayland ./build/debug/shakal`.
2. **Do not take screenshots.** The user banned assistant-initiated captures on 2026-10-27. Launch the
   app, say what to look at, and hand it over. Never report what the window looks like as something you
   observed.
3. An `offscreen` platform run proves construction only. Never report "the GUI works" from it.
4. Do not synthesize input or narrate a test you did not run. State plainly which paths the automated
   suites cover and which still need the user's click-through.

**Baseline confirmed by the user's own run on 2026-10-27:** Encode (including the WebM path), Cancel,
the overwrite dialog, drag-and-drop with output-directory autofill, and the checkbox-to-preset link all
work as shipped. That closes those paths for the current build only — a new UI change reopens the
requirement, so never cite this line as evidence that your own change works.

## Git conventions

- Commit subjects are **lowercase, terse, no conventional-commit prefixes** (`add new presets`,
  `clean up`). Bodies explain *why*, in plain prose.
- Commit only when asked. Never push without an explicit request.
- Stage only the paths belonging to the change — not `git add -A`.
