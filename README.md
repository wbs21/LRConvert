# LRconvert (Little Rabbit Converter) · C++ Edition

A graphical UI for FFmpeg: **Cut / Join / Split / Merge / Batch Compress / Custom Compress**.

This directory is the **C++20 + Qt6 rewrite**. The original Python/PySide6 version
(v3.4) is preserved in [`python/`](python/) with its full history and MIT license.

## Differences from the Python version

| | Python version (`python/`) | C++ version (this directory) |
|---|---|---|
| FFmpeg invocation | Subprocess running `ffmpeg.exe`, parsing `-progress pipe:1` output | **In-process runtime loading of the libav\* DLLs**, direct API calls |
| ffmpeg installation | User must install it and add it to PATH | Not needed — the DLLs ship with the program |
| Progress | Parses command-line text output | Computed from pts inside the encode/copy loops |
| Stopping | Sends `q`, then kills the process | Cooperative cancel: flush decoders/encoders, then write the trailer |
| Startup | ffmpeg detected at startup | **UI shows its first frame immediately; libraries load in the background** — missing libraries never block startup |
| Interface | PySide6 + mainui.ui | Qt6 + the same mainui.ui (identical widget names and interactions) |

The behavior of all six features (inputs/outputs, the `name_new_timestamp.ext`
naming rule, `(1)` conflict suffixes) matches the original, so existing users
need zero re-learning.

## Language

The interface is available in **English (default)** and **中文**. Switch anytime
from the dropdown at the top right of the **About** tab — the change applies
immediately and is remembered across restarts.

Source strings in the code are Chinese; the English translation lives in
[`translations/`](translations/) as a Qt `.ts`/`.qm` pair (built into the exe, and
overridable by a `translations/` folder next to the exe). To add a language, see
[`translations/README.md`](translations/README.md).

## Building

Requirements:

- **Qt 6** (MinGW build, ≥ 6.5; the MSVC build is ABI-incompatible with the
  MinGW-w64 FFmpeg build and must not be mixed)
- **CMake** ≥ 3.20
- Compiler: **the MinGW shipped with Qt** (`Qt/Tools/mingw1310_64`, GCC 13.1).
  Do not use a newer system g++ — the official Qt packages are built with
  GCC 13.1, and mixing libstdc++ across major versions tends to crash at runtime.

```bash
./scripts/build.sh          # Release build
./scripts/build.sh clean    # Wipe build/ and reconfigure
./scripts/build.sh asan     # ASan + UBSan test build (for the fuzz suite)
```

FFmpeg headers live in the repo at [`vendor/ffmpeg/include/`](vendor/ffmpeg/include/)
and are used directly for building. The 29 runtime DLLs (74.6 MB; selection
rationale in [`vendor/ffmpeg/README.md`](vendor/ffmpeg/README.md)) are **not** in
the repo (size reasons, gitignored). Two ways to get them:

- From the [GitHub Release](../../releases) package (the `ffmpeg/` subdirectory
  after extraction);
- Or set the environment variable `LRCONVERT_FFMPEG_DIR` to your own FFmpeg DLL
  directory.

Building only needs `include/`; missing DLLs do not affect compilation.
`scripts/package.sh` copies `vendor/ffmpeg/bin/` into the release directory when
packaging.

> The project **does not link** libav\* — symbols are resolved at runtime by
> `src/ffmpeg/FfmpegApi`, so the DLLs never enter the PE import table and are not
> preloaded at process start. See
> [`docs/LRConvert_Cpp_Qt_重构方案.md`](docs/LRConvert_Cpp_Qt_重构方案.md) §6.

## Running

```bash
./scripts/run.sh
```

FFmpeg DLLs are looked up in this order (`src/ffmpeg/FfmpegApi.cpp`):

1. The directory named by the `LRCONVERT_FFMPEG_DIR` environment variable;
2. The `ffmpeg/` subdirectory next to the exe (the default layout of the build
   and package scripts);
3. The exe's own directory.

> ⚠️ **Do not put the FFmpeg DLLs next to the exe.** A full build ships
> `zlib1.dll` / `libstdc++-6.dll` / `libbz2-1.dll` and other common runtime
> libraries. Once they sit next to the exe, Windows' loader resolves **Qt's own
> dependencies** against them first (the exe directory has the highest search
> priority), and a version mismatch kills the process at startup (exit code 127,
> with no error message at all).
> The `ffmpeg/` subdirectory keeps them physically isolated.

## Headless verification tools

These bypass the GUI but reuse the exact task-layer code paths, for regression
and CI:

```bash
./build/capability_check.exe                        # Engine capability list (no Qt dependency)
./build/remux_check.exe cut    <input> <output> <start-sec> <end-sec>
./build/remux_check.exe video  <input> <output>        # Video track only
./build/remux_check.exe audio  <input> <output>        # Audio track only
./build/remux_check.exe merge  <video> <audio> <output>
./build/remux_check.exe concat <output> <input1> <input2> ...
./build/transcode_check.exe  <input> <output> [encoder] [video-kbps] [scale] [audio-kbps]
```

One-command full regression (item-by-item comparison against ffmpeg CLI output
with identical parameters):

```bash
./scripts/regression.sh
```

Damage fuzzing — mutates one healthy clip into a corpus (truncation, bit flips,
`0xFF` fill, moov scrambling, empty file, …) and runs every entry point against
each variant with a hard timeout. It asserts only three things: **no crash**,
**no hang**, and **no leftover temp file** (the atomic-write guarantee):

```bash
./scripts/fuzz_corpus.sh                    # Release build
./scripts/build.sh asan && ASAN_BUILD=build-asan ./scripts/fuzz_corpus.sh
```

## Diagnostics

Both crash and freeze end up in the same place, `logs/crashdumps/`:

```bash
LRCONVERT_CRASH_TEST=1 ./scripts/run.sh     # Deliberate access violation → .dmp
```

- Catches SEH exceptions, `std::terminate`, `abort()`/CRT handlers, and **hangs**
  (a watchdog writes a dump when a heartbeat goes missing for 15 s);
- Registers a system-level WER LocalDumps fallback at startup, for deaths our own
  filter cannot catch;
- After an unclean exit, the next launch **starts in safe mode** (hardware encoder
  detection skipped) so the app still comes up and gets the job done.

## Packaging

```bash
./scripts/package.sh        # Build + windeployqt + FFmpeg DLLs
```

Artifacts (version read from `src/core/Version.h`, the single source of truth):

- `dist/LRConvert-<version>/` — Full release directory (exe, Qt runtime,
  `ffmpeg/` subdirectory, README, LICENSE, `translations/`)
- `dist/LRConvert-<version>-win64.zip` — The above, zipped; unzip and run
- `dist/LRConvert-<version>-ffmpeg-dll-win64.zip` — FFmpeg DLLs only

The packaging script finishes with a self-containment smoke test under a clean
PATH without any Qt development environment.

> **The Qt DLLs in the release directory cannot be moved into a subfolder.**
> They are written into the exe's PE import table and resolved by the Windows
> loader before `main()` runs, searching `exe directory → system directories →
> PATH`; subfolders are not in the search path — moving them makes the program
> fail to start (exit code 127, no message).
> The FFmpeg DLLs are the exception: they live in the `ffmpeg/` subdirectory
> because the program loads them at runtime by absolute path, never entering
> the import table.

## Known limitations

- Cut/Join are stream copies that start at the nearest keyframe, so the start
  may be slightly earlier than the requested time (same as the original).
- Join requires identical encoding parameters across segments (same as the
  original, per its README).
- **Hardware encoders are detected at startup**: the encoder dropdowns on Batch
  Compress (Tab5) and Custom Compress (Tab6) start with the software encoder
  libx264 only; hardware options appear after the background probe confirms
  they are available on this machine (i.e. the matching GPU exists).
  Unsupported options never show up, and the log explains why. If a selected
  hardware encoder still fails to open, the task falls back to libx264
  automatically.
- Cut boundaries differ from the ffmpeg CLI by about 2 frames (66 ms): the CLI
  finishes by dts and carries a few extra frames under B-frame reordering; this
  implementation finishes by pts, so the output is exactly the `[start, end)`
  interval. See docs/ regression plan §9.8.

## Changelog

Release history: [CHANGELOG.md](CHANGELOG.md).

## License

- C++ version: **GPL-3.0** (see [LICENSE](LICENSE)).
  It links `libavcodec` / `libavformat` etc. in-process at runtime, and the
  bundled FFmpeg is a `9.0.2-gplfull` build (including libx264, libx265 and
  other GPL components), so under the GPL this program is a derivative work.
  Note: runtime `LoadLibrary` linking counts as linking; "delayed loading" is
  not an exemption.
- Python original: MIT (see [python/LICENSE](python/LICENSE)).
  It only invokes `ffmpeg.exe` as a subprocess, communicating through
  command-line arguments and standard output — mere aggregation of independent
  programs, so the GPL does not propagate to it.

### Source-code offer required by GPL-3

This program is released under GPL-3.0; distributing binaries requires offering
the complete corresponding source. The source is in this repository branch:

```
https://github.com/wbs21/LRConvert          (the C++ version is at the root, under src/)
```

The repo does not contain the 29 FFmpeg runtime DLLs (74.6 MB, size reasons).
They are likewise GPL-3 and can be obtained from the
[GitHub Releases](../../releases) packages — the `ffmpeg/` subdirectory inside.
