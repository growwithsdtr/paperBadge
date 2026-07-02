# Heavy Sprint QA Checklist

Build:

```sh
pio run
```

Manual flash when ready:

```sh
pio run -t upload --upload-port /dev/cu.usbmodem1101
```

Do not use `/dev/cu.usbmodemHA0E246012845`.

## Physical QA

- Badge opens from Home in normal orientation.
- Badge has no footer Home.
- Badge body tap toggles English/Japanese.
- Badge QR tap zooms QR; tap zoom returns to badge.
- Badge top tap returns Home.
- Sleep Now and Power Off final frame is English, no footer, 180-degree strap orientation.
- Home/Settings/Reader/Japanese/Interview are not sideways after wake.
- Reader bad EPUB shows ReaderError and only Back/Home navigate.
- TXT/MD still open; Reader Size cycles S/M/L/XL.
- Interview Practice opens menu/list, reveals paged answer sections, and does not truncate long answers.
- Interview Size S/M/L/XL visibly changes Practice, Drill, Feedback, and Glossary spacing.
- Interview Exam header says Exam and scores only at end.
- Glossary opens categories, list, then detail.
- Japanese Practice uses Source -> Week/Unit -> Day/Lesson.
- Japanese Mock Test scores at end and review opens explanations.
- Manga small CBZ still opens; reading is fullscreen.
- Manga top tap returns library.
- Manga center tap opens overlay; overlay Fit, Orientation, Refresh, Clean now, Library, and Close buttons respond.
- Manga portrait opens fit-page by default.
- Manga portrait left/right thirds page backward/forward; center does not accidentally page.
- Manga overlay Orientation toggles portrait / landscape; landscape visually rotates to 960x540.
- Manga landscape fit-width shows a zoomed vertical section.
- Manga landscape left/right body taps page through slices before changing page.
- Manga back from the first slice of a page lands on the previous page's last slice.
- Manga slice/page/fit/orientation chrome is visible without covering too much of the page.
- Manga leaving to library/settings restores portrait orientation.
- PNG-in-CBZ follows fit-page / fit-width / fit-height and slice behavior if the page width is within PNGdec row limits.
- Large non-ZIP64 CBZ over 50 MB is allowed to attempt open.
- ZIP64 CBZ shows a ZIP64-specific diagnostic.
- CBR/RAR, WebP-only, corrupt ZIP, empty/no-JPEG-PNG, and allocation-failure manga archives show distinct diagnostics.
- Serial boot log shows IMU probe results for candidate addresses `0x68` and `0x69`; auto-rotation is not expected yet.
- Settings pages open: Fonts, Reader, Manga, Power, Refresh.
- Refresh profile cycles Fast/Balanced/Clean and persists after reboot.
- Refresh clean cadence is visible and persists.
- Font Lab shows BIZ UDPGothic, Noto Sans JP, M PLUS, Inter, and Source Serif candidate pages.
- Font Lab samples are not clipped, dense, or overlapping; Inter and Source Serif are clearly labeled Latin only.
- Reader EPUB around 10 MB opens when it has normal unencrypted XHTML spine content.
- EPUBs that hit per-entry or total extracted-text caps show distinct messages and do not cut UTF-8 mid-character.

Stop on any Interview route becoming unreachable, Badge failing to render, or small CBZ failing to open.
