# Changelog

All notable changes to the **C++ edition** (the repository root). The original
Python/PySide6 version lives in [`python/`](python/) and is no longer developed.

This file follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
version numbers come from `src/core/Version.h` (the single source of truth).

## [4.0.2] — 2026-10-16

A second stability pass. **No functional changes** — everything here is about
how the program behaves when things go wrong.

### Added

- **Bilingual interface.** English is the default; 中文 is available from the
  dropdown at the top right of the About tab. The choice applies immediately and
  is remembered across restarts. English ships as a compiled `.qm` embedded in
  the exe; a `translations/` folder next to the exe overrides it at runtime.
  (See [`translations/README.md`](translations/README.md) to add a language.)
- **Freeze watchdog.** Each subsystem reports a heartbeat; 15 seconds without
  one is treated as a hang and a dump is written automatically. Previously a
  "alive but unresponsive" freeze could only be caught by running into it.
- **I/O timeouts.** Opening and reading media now have deadlines, and remote
  paths (UNC, mapped drives) are probed for readability before being opened
  instead of blocking on the OS indefinitely.
- **Safe mode after an unclean exit.** If the previous run did not shut down
  cleanly, the next launch skips hardware-encoder detection and says so — the
  app still comes up and gets the job done. Recovers after one clean exit.
- **Damage fuzzing suite** (`scripts/fuzz_corpus.sh`): mutates a healthy clip
  into a corpus and runs every entry point against each variant, asserting only
  **no crash**, **no hang**, **no leftover temp file**.
- **Named threads** (`LRConvert/GUI`, `LRConvert/Playback`, …) so dumps are
  readable, and **8 MB stacks** for decode/playback threads.

### Fixed

- **Atomic output.** Outputs are now written to a temp file, closed, and only
  then renamed into place. A crash, power loss or manual stop leaves the final
  path either complete or untouched — never a half-written file over an old one.
- **Writing to the input path is rejected up front** instead of corrupting the
  source file.
- **Crash dumps are complete.** The dump thread is created at startup and waits
  suspended, so the crash path only writes the file (it used to spawn a thread
  mid-exception and occasionally produce a 0-byte dump). Two CRT abort paths
  (invalid parameter, pure virtual call) are now caught as well, and a system
  WER LocalDumps fallback is registered as a last resort.
- **Task exceptions no longer kill the process** — the task body is wrapped, so
  an escaping exception can't reach `std::terminate`.
- **The frame grabber no longer serializes unrelated tasks.** Throttled work
  (preview frames, thumbnails) now uses a timed `try_lock` and skips a beat
  instead of queueing behind a long import.
- **Packaging produced a corrupt archive**: `tar -a` does not produce a zip on
  MSYS/GNU tar 1.35, so the "`.zip`" released for v4.0 and v4.0.1 was actually
  an uncompressed tar (114 MB, and Windows Explorer cannot open it). Now built
  with Python's `zipfile`: a real zip, 114 MB → 41 MB.
- **Packaging no longer wipes `dist/`** (destroying the previous release and any
  chance of rollback); old directories are retired to `dist_*/` instead.

### Changed

- FFmpeg resources are now held by RAII wrappers (`src/ffmpeg/FfObjects.h`), so
  error and exception paths can't leak contexts, frames or scalers.
- Hardware-encoder probing is time-boxed (10 s each) and runs under a timeout.

### Known limitations

- **Read timeouts do not apply to files on network shares.** That blocking
  happens inside the OS, where the interrupt callback is never consulted.
  Opening is probed and the watchdog covers the rest, but "the cable is pulled
  mid-read" still waits for the OS. Local files are unaffected.

## [4.0.1] — 2026-10-15

Decoding robustness and crash dumps. No functional changes.

### Fixed

- **Playback no longer stops on damaged data.** A decode failure used to be
  treated as end-of-stream: the last frame is now held (time keeps running),
  audio is filled with a decaying tail, and playback stops only after 1.5 s of
  continuous failure.
- **Long damaged regions no longer spin.** Consecutive bad packets are counted
  per session; after 64, the reader skips 1 second ahead.
- Import and playback now report how many damaged packets were skipped.

### Added

- Crash dumps (`logs/crashdumps/*.dmp`) plus `logs/LRConvert.log` capturing Qt
  and FFmpeg log output.

## [4.0.0] — 2026-10-15

First release of the C++20 + Qt6 rewrite.

- Cut / Join / Split / Merge / Batch Compress / Custom Compress, with the same
  behavior and naming rules as the Python version.
- FFmpeg is loaded **in-process** at runtime (`src/ffmpeg/FfmpegApi`) rather than
  invoked as a subprocess; the DLLs ship with the program.
- The UI shows its first frame immediately and loads the libraries in the
  background — missing libraries never block startup.
- GPL-3.0 (the bundled FFmpeg is a `9.0.2-gplfull` build); the Python original
  remains MIT in [`python/`](python/).
