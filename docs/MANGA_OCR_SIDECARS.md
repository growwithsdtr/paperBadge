# Manga OCR Sidecars

`tools/manga_preprocess.py` can optionally write OCR sidecars with:

```sh
GEMINI_API_KEY=... tools/manga_preprocess.py book.cbz --ocr gemini --downscale portrait
```

OCR is optional and has no firmware dependency. Outputs are under `ocr/`:

- `text.json`: JSON-LD region/page text records.
- `vocab.json`: vocabulary candidates.
- `concepts.json`: kanji/grammar/concept candidates.
- `page_map.json`: links optimized pages/slices to OCR records.

Fallback structure is page-level text when panel/bubble regions are uncertain.
