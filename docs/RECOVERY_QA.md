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
- Interview Exam header says Exam and scores only at end.
- Glossary opens categories, list, then detail.
- Japanese Practice uses Source -> Week/Unit -> Day/Lesson.
- Japanese Mock Test scores at end and review opens explanations.
- Manga small CBZ still opens; reading is fullscreen.
- Manga top tap returns library; bottom-left cycles fit state; bottom-right toggles orientation state.
- Settings pages open: Fonts, Reader, Manga, Power, Refresh.
- Refresh profile cycles Fast/Balanced/Clean and persists after reboot.

Stop on any Interview route becoming unreachable, Badge failing to render, or small CBZ failing to open.
