# Next Work Queue

Recommended next prompt:

```text
Continue heavy-paperbadge-sprint from HEAD. Focus on physical QA fixes only:
flash /dev/cu.usbmodem1101, verify Badge, Reader bad EPUB, small CBZ, Interview flows,
then patch only regressions found on-device.
```

High-priority follow-ups:

- Hardware QA the touch zones for Badge QR and fullscreen Manga controls.
- Implement true manga fit-width/fit-height and landscape slice rendering in the decoder/cache pipeline.
- Decide whether PNG-in-CBZ support is worth adding to page_loader.
- Persist Reader/Interview/Japanese font-size levels to settings.
- Replace Japanese registry detection with a compact SD JSON loader.
- Add SD NDJSON progress logging for Japanese and Interview.
- Add a dedicated FontLab build profile if more font candidates are embedded.
- Validate Gemini OCR output shape and normalize raw model JSON into schema fields.
