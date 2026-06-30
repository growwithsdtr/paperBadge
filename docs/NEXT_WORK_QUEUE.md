# Next Work Queue

Recommended next prompt:

```text
Continue font-manga-reader-sprint from HEAD. Do physical QA only:
flash /dev/cu.usbmodem1101 manually, verify Font Lab candidate pages, Badge,
small JPEG CBZ, PNG CBZ, large non-ZIP64 CBZ, Reader EPUB around 10 MB,
Manga landscape fit-width slices, Settings persistence, and Interview routes.
Patch only regressions found on-device.
```

High-priority follow-ups:

- Physical QA the landscape touch mapping on Paper S3 hardware.
- Compare manga fit-width visual quality; consider a higher-quality downscale path if nearest-neighbor is too rough.
- Add PNG fit-width / slice viewport rendering if PNG manga is common.
- Add a real BMI270/BMI270-like IMU driver only after confirming address, config requirements, and shared I2C timing with GT911.
- Decide whether a production runtime font candidate should replace or augment BIZ/IPA; Font Lab candidates are QA subsets only.
- Implement chapter-lazy Reader pagination if 2 MB extracted EPUB text is still too much for large books.
- Consider a WebP decoder only if flash/RAM cost stays low.
- Validate Gemini OCR output shape and normalize raw model JSON into schema fields.
