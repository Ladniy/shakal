# shakal — Qt6 GUI plan

**Date:** 2026-10-26
**Status:** Core encode flow complete. Step 0 passed, the app builds clean from an empty `build/`,
and both test suites pass. The Python half is **deleted** (`shakal/`, `pyproject.toml`, `uv.lock`).
Committed on `main` as two commits (app, then docs); not pushed. See "Verification status" below.
**Scope decision:** native C++ Qt 6 Widgets app replacing the Python questionary TUI entirely.

---

## Objective

Rewrite shakal as a C++17 / Qt 6 Widgets desktop app at the repo root. The Python package,
`pyproject.toml`, and `uv.lock` are deleted (recoverable from git — `17dda77` is the oldest commit
holding that code). One video at a time: pick input → preset → output dir + name + container →
encode with live progress, real failure reporting, and working cancel.

## Decisions locked

| Question | Choice |
| --- | --- |
| Binding | C++ Qt6 (not PySide6/PyQt6) |
| UI style | Qt Widgets |
| First-version scope | Core encode flow |
| Existing TUI | GUI replaces it |
| Python | Dropped entirely |
| Repo layout | C++ project at repo root |
| Existing output | Confirm before overwriting |
| Delivery | CMake + build dir only (no `install()`, no CPack) |

## Environment verified live

| Need | Evidence |
| --- | --- |
| Qt 6.12.0 | `/usr/lib/cmake/Qt6/Qt6ConfigVersionImpl.cmake` → `set(PACKAGE_VERSION "6.12.0")` |
| `Qt6::Widgets` headers | `/usr/include/qt6/QtWidgets/qwidget.h`, `/usr/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake` |
| `moc` (AUTOMOC prerequisite) | `/usr/bin/moc`, `/usr/lib/qt6/moc` |
| Compilers / build tools | `/usr/bin/{cmake,g++,clang++,make,qmake,qmake6}` |
| **`ninja` is absent** | → CMake presets must use `Unix Makefiles`, not Ninja |
| Media tools | `/usr/bin/ffmpeg`, `/usr/bin/ffprobe` |

### Now verified (these four were blocked while planning)

- CMake **4.4.4** — it rejects `cmake_minimum_required(VERSION <3.5)`, so the project floors at 3.21
- **`libx265` is present**, so all four presets are viable (`hevc_vaapi` and `h264_nvenc` too)
- A display exists: `DISPLAY=:0`, `WAYLAND_DISPLAY=wayland-1`, `XDG_SESSION_TYPE=wayland`
- `QtGui` headers exist (`/usr/include/qt6/QtGui/QGuiApplication`), and so does `Qt6::Test`

## Step 0 — pre-flight (run before writing code)

1. `cmake --version`
2. `ffmpeg -hide_banner -encoders | grep -E '265|264'`
3. `echo "$DISPLAY $WAYLAND_DISPLAY"`
4. `ls /usr/include/qt6/QtGui/QGuiApplication`

---

## Background: defects carried in from the current TUI

`shakal/main.py` is an 88-line linear script reviewed in full on 2026-10-26 (working tree then
clean at HEAD `afd0361`). Three open defects motivate the decisions below:

1. **No cancel path** — `questionary....ask()` returns `None` on Ctrl-C instead of raising, so
   interrupts fall through to `os.path.join(None, None)` (`TypeError`), to an unbound `params`
   when the preset question is skipped (no `else` in the `if/elif` chain), or to `None` inside
   argv.
2. **Output filename never gets an extension** — nothing appends a container suffix and no `-f`
   is passed, so ffmpeg rejects the default path:
   `Unable to choose an output format for 'out_noext'` (exit 234).
3. **Failure is silent** — `subprocess.run` without `check=True`. Reproduced on this machine: when
   the output exists and `-y` is absent, ffmpeg prints `File 'x' already exists. Exiting.` plus
   `Error opening output file`, **and still returns 0** (Lavc 63.1.102).

## Files to delete

- `shakal/main.py`, `shakal/__init__.py`, and the `shakal/` directory
- `pyproject.toml`, `uv.lock`
- `.gitignore`: replace the Python/venv/uv entries with CMake entries — add `build/`, `*.o`,
  `moc_*.cpp`; drop `__pycache__/`, `*.pyc`, `*.pyo`, `*.pyd`, `.venv/`, `venv`,
  `.python-version`, `*.egg-info/`

## Files to create

| Path | Purpose |
| --- | --- |
| `CMakeLists.txt` | `find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)`, `CMAKE_AUTOMOC ON`, `CMAKE_CXX_STANDARD 17`, single `shakal` executable target; use `qt_add_executable` if available at 6.12 |
| `CMakePresets.json` | `Debug` + `Release`, both with generator `Unix Makefiles` (no ninja on this machine) |
| `src/main.cpp` | `QApplication`, construct `MainWindow`, show |
| `src/Presets.h` | `struct Preset { QString label; QStringList args; }` plus a `const` table of the four presets |
| `src/EncoderController.h/.cpp` | all ffmpeg/ffprobe logic; `Q_OBJECT`; lives on the GUI thread (QProcess is signal-driven, so no `QThread` needed) |
| `src/MainWindow.h/.cpp` | the form: inputs, progress bar, log pane |

`shakal/__init__.py` is currently empty and `main.py` exposes no importable helpers, so nothing is
reusable as a utility — the preset table is the only content worth porting, and it moves as data.

## Ported preset argv (verbatim from `shakal/main.py` lines 24-79)

| Preset | Arguments |
| --- | --- |
| SHAKAL Light | `-vcodec libx265 -crf 22 -preset slow -acodec copy` |
| SHAKAL Medium | `-vcodec libx265 -crf 26 -preset medium -acodec aac -b:a 128k` |
| SHAKAL Strong | `-vcodec libx265 -crf 32 -preset fast -acodec aac -b:a 96k` |
| SHAKAL ULTRA | `-vf scale=320:180,fps=12,scale=1920:1080:flags=neighbor -vcodec libx265 -crf 42 -preset ultrafast -acodec aac -b:a 48k` |

## Core design decisions

**1. Full command line.**
`ffmpeg -hide_banner -progress pipe:1 <presetArgs> -i <input> <output>`, plus `-y` **only** when the
user confirmed overwrite. stdout carries only `key=value` progress lines; stderr is the human log
pane. They are read from separate signals and never merged.

**2. Progress as a real percentage.**
Total duration from `ffprobe -v error -show_entries format=duration -of json <input>`, parsed with
`QJsonDocument` (already in `Qt6::Core`, so no extra component). Numerator is `out_time_us`, not
`out_time_ms`, which is unreliable across ffmpeg builds. Buffer partial lines across
`readyReadStandardOutput` and split on `\n`. If duration is unavailable, show an indeterminate busy
bar plus frame count instead of a fake percentage. Completion is the `progress=end` line.

**3. Success invariant — the critical one.**
Because `returnCode == 0` does not mean an encode happened (see defect 3), success requires **all
three**: `progress=end` seen, `returnCode == 0`, and the output file exists and is non-empty.
Anything else surfaces the stderr tail in a visible error dialog.

**4. Overwrite confirm.**
Before launching, if the target path exists, `QMessageBox` with **Overwrite / Choose another name /
Cancel**. Overwrite → pass `-y`; another name → reopen the save dialog; Cancel → abort with nothing
run. This also eliminates the silent no-op in defect 3.

**5. Container becomes mandatory.**
The combo offers `mp4`, `mkv`, `webm`. The filename gets that suffix if the user omitted it, and the
save-dialog filter follows the combo. This closes defect 2, which broke every default TUI run.

**6. Cancel.**
Write `q` to ffmpeg's stdin for graceful finalization, then `waitForFinished(5000)`, escalating to
`terminate()` and then `kill()`. A killed encode leaves a truncated file, so cancelled output is
reported as incomplete and deleted (or renamed `.part`) rather than passed off as finished. `q` over
a pipe needs confirming during implementation; if it proves unreliable, `terminate()` is the
fallback and the UI must say so.

**7. Encoder probe at startup.**
Scan `ffmpeg -hide_banner -encoders` for `libx265`; if absent, disable the encode button and state
why, instead of letting every run fail silently.

**8. No equivalent of a `None` answer.**
Defect 1 came from a linear script with no error model. The C++ version keeps state in one
`EncoderController` with explicit signals (`progress`, `logLine`, `succeeded`, `failed`), so the
"cancelled but the code kept going" class of bug is structurally absent.

## Known caveats, deliberately not changed

- **ULTRA keeps its hardcoded geometry.** `320:180` → `1920:1080` aspect-distorts non-16:9 sources.
  Maximum degradation is the preset's purpose, so the numbers are kept byte-for-byte.
- **`-acodec copy` into a container that cannot hold the source codec fails at mux time** (Light
  preset). AC3→mp4 copy was verified working on this build, so this only bites other streams — and
  now it surfaces as a visible error instead of exit 0.

## Verification

1. `cmake -S /home/ladniy/Git/shakal -B /home/ladniy/Git/shakal/build` then
   `cmake --build build -j` — clean configure and compile, no warnings from `AUTOMOC`.
2. Generate a fixture:
   `ffmpeg -f lavfi -i testsrc=size=1280x720:rate=25 -f lavfi -i sine -t 8 -c:v libx264 -c:a aac /tmp/shakal_in.mp4`
3. Launch `./build/shakal` and confirm for **each of the four presets**: progress reaches 100% with
   a genuine percentage, a non-empty output file exists, and the log pane shows ffmpeg text.
4. Cancel mid-encode → bar stops promptly, no bogus success message, truncated file removed or
   marked.
5. Overwrite case → dialog appears and all three buttons behave as specified.
6. Failure paths are visibly failures: nonexistent input (blocked by the file dialog) and an audio
   stream the container cannot hold (proves the error surfaces instead of exiting 0).
7. If no display is available, say so and report only the build result — do not claim the GUI was
   exercised. Non-GUI logic (argv assembly, duration parsing) can be checked headlessly.

## Out of scope

Batch/queue, drag-and-drop, folder input, user-editable presets, persisting presets to disk, video
preview, `install()` targets, `.desktop` entry, CPack packaging, pybind11 embedding.

## Commit plan

No commits without an explicit request. If approved: one commit for the Python removal, one for the
Qt app.

## Verification status (2026-10-26)

| Check | Result |
| --- | --- |
| Step 0 pre-flight | **Passed** — CMake 4.4.4, `libx265` present, `DISPLAY=:0` + `WAYLAND_DISPLAY=wayland-1`, `QtGui/QGuiApplication` exists |
| Configure + build (`--preset debug`) | **Passed** after one fix: `target_include_directories(... src)` was required, since `qt_standard_project_setup()` puts the repo root — not `src/` — on the include path |
| `ctest` (`tst_presets`) | **Passed** — 1/1, asserts argv assembly, `-progress pipe:1`, `-y`/`-n` selection, container suffixing, verbatim preset tables |
| App constructs and runs | **Passed** — `QT_QPA_PLATFORM=offscreen` survived an 8s timeout (exit 124), so the window, form and HEVC probe all initialize |
| Progress protocol end-to-end | **Passed** — real encode emitted `out_time_us` lines then `progress=end`, exit 0, 69,941-byte output |
| `-n` on an existing file | **Passed** — exit **0** with **zero** `progress=end` lines; the `sawEnd` term of the invariant is what rejects it (a size check alone would not) |
| Cancel via `q` | **Passed on attempt 3** — first two tries were inconclusive because the fixture finished in 2.2s, before `q` landed. With `veryslow -threads 1` on 10s of 1080p: `q` **is honored over a pipe**, stop at 3.17s of 10s, valid playable file. Side effect: a graceful cancel still reports `progress=end` + exit 0, so the cancel check must precede the success invariant |
| 4-case integration suite (`tst_encoder`) | **Passed** — clean encode succeeds; refused overwrite reports failure *and* leaves the user's pre-existing bytes intact; mux failure reports failure and removes the stub; cancel reports "Cancelled" and removes the partial file |
| GUI exercised by a human (encode click, cancel button, overwrite dialog) | **Not done** — offscreen construction only; no window has ever rendered on your display |
| `-acodec copy` into an incompatible container | **Passed** — AC3→webm fails at mux (exit 234, ffmpeg still prints `progress=end` and leaves a 262-byte stub); shakal detects the failure and deletes the stub |
| Two defects found and fixed by that testing | (a) ffmpeg left a junk stub behind on mux failure → added `discardOutputIfOurs()`, which never touches a file protected by `-n`; (b) `errorOccurred` arrives before `finished`, so a `terminate()`d cancel was reported as "ffmpeg crashed." instead of "Cancelled." |
| Deleting the Python half | **Done** — `shakal/`, `pyproject.toml`, `uv.lock` removed; verified by a clean rebuild from an empty `build/` with both suites green |


## Sources

Retained only to explain why a Python binding was viable before native C++ was chosen; that branch
is no longer relevant to this plan:

https://pypi.org/project/PySide6/
https://doc.qt.io/qtforpython-6/release_notes/pyside6_release_notes.html
https://www.qt.io/blog/qt-for-python-release-6.12-is-here
