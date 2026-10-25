# shakal

A Qt 6 desktop front end for a set of deliberately aggressive `ffmpeg` presets.

## Requirements

- CMake >= 3.21
- A C++17 compiler
- Qt 6.5 or newer (`Core`, `Gui`, `Widgets`, and `Test` for the tests)
- `ffmpeg` and `ffprobe` on `PATH`, built with `libx265`

## Build

```sh
cmake --preset debug          # or: release
cmake --build --preset debug
./build/debug/shakal
```

`CMakePresets.json` uses the `Unix Makefiles` generator. If you want another
generator, configure directly:

```sh
cmake -S . -B build -G "Ninja"
cmake --build build
```

## Tests

```sh
ctest --test-dir build/debug --output-on-failure
```

Two suites run:

- `presets` — argv assembly, container suffixing, preset table (no media tools needed)
- `encoder` — real `ffmpeg`/`ffprobe` round-trips covering a clean encode, a refused
  overwrite, a mux failure that must leave no junk file, and a mid-encode cancel

## Usage

Fill in the form: input file, preset, output directory, output name, container. The
container combo appends its own suffix, so the name field needs no extension. If the
target file exists you are asked to **Overwrite**, **Choose another name**, or
**Cancel** before anything runs.

An encode that ends with exit code 0 is *not* treated as success on its own: shakal
also requires ffmpeg's final `progress=end` marker and a non-empty output file,
because ffmpeg exits 0 after declining to overwrite a file.

## Presets

All four encode video with `libx265`.

| Preset | CRF | Encoder speed | Audio |
| --- | --- | --- | --- |
| SHAKAL Light | 22 | slow | copied, untouched |
| SHAKAL Medium | 26 | medium | AAC 128k |
| SHAKAL Strong | 32 | fast | AAC 96k |
| SHAKAL ULTRA | 42 | ultrafast | AAC 48k, plus `scale=320:180,fps=12,scale=1920:1080:flags=neighbor` |

ULTRA downscales to 320x180 at 12 fps and nearest-neighbour upscales back to
1920x1080. Those numbers are hardcoded, so a source that is not 16:9 gets
aspect-distorted — that is the preset's intent, not a bug to fix.

`SHAKAL Light` copies the audio stream. If the source codec cannot live in the
container you picked, ffmpeg fails at mux time and shakal reports the error and
removes the partial file it left behind.
