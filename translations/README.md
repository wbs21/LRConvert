# Adding a language to LRconvert

Translation files live here, one Qt `.ts` per language, compiled to `.qm` at
build time. Source strings in the code are Chinese (`tr("中文")`); each language
file supplies its own translations. English ships as the default.

## Layout

```
translations/
├── LRconvert_en.ts      # English (ships as the default language)
├── LRconvert_en.qm      # compiled by lrelease (build artifact; gitignored)
└── README.md            # this file
```

The `.qm` files are embedded into the exe via a generated qrc, so the default
English works without any external file. A `translations/` folder **next to the
exe** takes priority at runtime: drop a newer or additional `.qm` there and it
is picked up without rebuilding (see `src/core/I18n.cpp`).

## Adding a new language (e.g. Japanese)

1. **Register it** in `src/core/I18n.cpp`:

   ```cpp
   static const QVector<LanguageInfo> kLanguages = {
       {QStringLiteral("en"), QStringLiteral("English")},
       {QStringLiteral("zh_CN"), QStringLiteral("中文")},
       {QStringLiteral("ja"), QStringLiteral("日本語")},   // <- add
   };
   ```

   `id` is the filename segment (`LRconvert_<id>.qm`); `nativeName` is what the
   dropdown shows (the language's own name — never translated).

2. **Create the `.ts`** and extract the strings:

   ```bash
   "C:/Qt/6.12.0/mingw_64/bin/lupdate.exe" \
       src/main.cpp src/MainWindow.cpp src/ui/TimelineWidget.cpp src/ui/mainui.ui \
       src/tasks/RemuxTask.cpp src/tasks/TranscodeTask.cpp src/core/I18n.cpp \
       -ts translations/LRconvert_ja.ts
   ```

3. **Translate** — open the `.ts` in Qt Linguist (or edit the XML directly;
   `<source>` is Chinese, fill `<translation>`). Placeholders `%1`, `%2`, …
   must be kept verbatim.

4. **Wire it into the build** — add the file to `qt_add_lrelease(TS_FILES ...)`
   and the `<file>` list of the generated qrc in `CMakeLists.txt`, then rebuild.

## Regenerating the English file

After touching any `tr(...)` / `QCoreApplication::translate(...)` call:

```bash
# 1. extract new/changed strings (existing translations are kept)
"C:/Qt/6.12.0/mingw_64/bin/lupdate.exe" -no-obsolete \
    src/main.cpp src/MainWindow.cpp src/ui/TimelineWidget.cpp src/ui/mainui.ui \
    src/tasks/RemuxTask.cpp src/tasks/TranscodeTask.cpp src/core/I18n.cpp \
    -ts translations/LRconvert_en.ts

# 2. translate the new entries in Qt Linguist, then compile
"C:/Qt/6.12.0/mingw_64/bin/lrelease.exe" translations/LRconvert_en.ts
```

The normal build does both steps for the registered `.ts` files automatically.

## Notes

- **Chinese is the source language**: it has no `.qm` of its own — selecting
  中文 simply unloads the app translator (and loads Qt's own
  `qtbase_zh_CN.qm` so standard dialog buttons are Chinese too).
- Only wrap **user-visible** strings. Log-only diagnostics (`qInfo`/`qWarning`),
  the crash-dump text, and the deep FFmpeg-layer error strings
  (`src/ffmpeg/`, kept Qt-free on purpose) intentionally stay Chinese.
- lupdate drops namespaces from `tr()` inside namespaced QObject subclasses
  (e.g. `lr::ui::TimelineWidget`), which breaks the runtime context. In such
  files call `QCoreApplication::translate("full::Class", ...)` explicitly —
  that is why `TimelineWidget.cpp` does.
