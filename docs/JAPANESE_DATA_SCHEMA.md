# Japanese Data Schema and Index Strategy

PaperBadge should not require firmware recompiles for new Japanese study sources.

Recommended hybrid:

- Canonical JSON-LD sidecars on SD card.
- Optional Mac-side SQLite/search index generated during preprocessing.
- Firmware loads a compact `source_registry.json` from `/paperBadge/content/japanese/` when present.
- Embedded sample questions remain a safe fallback.
- Progress is append-only NDJSON under `/paperBadge/state/`.

On-device SQLite is not selected for the first ESP-IDF scaffold because it adds flash/RAM and build risk. A Mac-side SQLite database can still be generated for authoring/search, then exported to compact JSON-LD for the device.

Core entities:

- `Source`
- `Section`
- `Page`
- `Item`
- `Exercise`
- `Concept`
- `GrammarConcept`
- `VocabularyConcept`
- `KanjiConcept`
- `MangaPage`
- `MangaSlice`
- `OCRRegion`
- `Result`
- `ReviewEvent`
- `CrossReference`

Exercise types:

- `MCQ`
- `cloze`
- `reading`
- `listening_metadata`
- `note`
- `grammar_explanation`
- `vocab_item`
- `kanji_item`
- `mock_test_section`
- `manga_example`

Cross-reference rule:

Use stable `concept_id` values and cite source/page/item refs plus optional manga page/slice/OCR refs.
