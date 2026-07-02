# Next Work Queue

Recommended next prompt:

```text
Continue qa-font-manga-interview-fixes from HEAD. Do physical QA only at first:
flash /dev/cu.usbmodem1101 manually, verify Font Lab candidate pages, Badge,
small JPEG CBZ, PNG CBZ fit/slices, large non-ZIP64 CBZ, ZIP64/CBR/WebP
diagnostics, Reader EPUB cap messages, Manga landscape fit-width slices,
overlay controls, Settings persistence, and all Interview routes.
Capture serial logs for the IMU probe at addresses 0x68/0x69.
Patch only regressions found on-device.
```

High-priority follow-ups:

- Physical QA the new manga touch map and overlay on Paper S3 hardware.
- Confirm whether the IMU probe reports a BMI270-compatible chip ID. Implement real auto-rotation only after the address/chip is known.
- Compare manga fit-width visual quality; consider a higher-quality downscale path if nearest-neighbor is too rough.
- Stress-test PNG fit-width / slice rendering with wider PNG pages near the PNGdec row-buffer limit.
- Decide whether a production runtime font candidate should replace or augment BIZ/IPA; Font Lab candidates are QA subsets only.
- Implement chapter-lazy Reader pagination if 2 MB extracted EPUB text is still too much for large books.
- Consider a WebP decoder only if flash/RAM cost stays low.
- Validate Gemini OCR output shape and normalize raw model JSON into schema fields.
- Remove or implement any remaining preview-only/decorative Settings affordances if physical QA finds them confusing.
