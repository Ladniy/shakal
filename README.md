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

Fill in the form: input file, output directory, output name, container. Dropping a file
onto the **Input file** field or picking it with **Browse…** also fills in the output
directory from the file's own folder, and the name field if it is still empty. The
container combo appends its own suffix, so the name field needs no extension. If the
target file exists you are asked to **Overwrite**, **Choose another name**, or
**Cancel** before anything runs.

An encode that ends with exit code 0 is *not* treated as success on its own: shakal
also requires ffmpeg's final `progress=end` marker and a non-empty output file,
because ffmpeg exits 0 after declining to overwrite a file.

## Presets

There is no preset picker. Every encode runs the **default** preset unless the
**Shakal ULTRA** checkbox next to the container combo is ticked, which is the only way
to reach ULTRA. Both presets encode video with `libx265`.

| Preset | CRF | Encoder speed | Audio |
| --- | --- | --- | --- |
| default | 32 | fast | AAC 96k |
| ULTRA | 42 | ultrafast | AAC 48k, plus `scale=320:180,fps=12,scale=1920:1080:flags=neighbor` |

ULTRA downscales to 320x180 at 12 fps and nearest-neighbour upscales back to
1920x1080. Those numbers are hardcoded, so a source that is not 16:9 gets
aspect-distorted — that is the preset's intent, not a bug to fix.

Both presets re-encode audio, so no container choice can fail on a copied stream.
WebM is the exception at the *video* level: it takes no HEVC at all, so shakal
re-expresses the codec flags for that container — see `AGENTS.md`.
