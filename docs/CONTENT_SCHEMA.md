# Content Schema

PaperBadge is the hardware shell. PaperCoach is the generic offline learning mode that runs inside it. The current embedded content pack is interview-focused, but the same schema shape is intended to support future Japanese N3-style grammar, vocabulary, kanji, certification, or other drill decks.

## SD Override Paths

- Cards: `/papercoach/decks/interview_cards.json`
- Legacy cards: `/papercoach/decks/sample_interview.json`
- Glossary: `/papercoach/glossary.json`
- Results output: `/papercoach/progress/session_results.json`
- Render trace: `/papercoach/debug/render_trace.txt`
- Deck export: `/papercoach/debug/embedded_deck_dump.md`

## Card Schema

```json
{
  "schema_version": 1,
  "cards": [
    {
      "id": "A01",
      "card_id": "A01",
      "section_id": "A",
      "section": "Background, Motivation & Fit",
      "number": "1",
      "title": "Self-introduction / career & recent work",
      "must_master": true,
      "theme": "Background/Fit",
      "spoken": "Prepared answer text.",
      "anchor": "Memory cue / key points.",
      "confidence": "Evidence-backed.",
      "watch": "Mistake to avoid."
    }
  ]
}
```

Practice stage names:

- Question: `title`
- Answer: `spoken`
- Anchor: `anchor`
- Watch-out: `watch`
- Follow-up: likely interviewer push
- Defense: strong response principle

## Drill Schema

```json
{
  "schema_version": 1,
  "drills": [
    {
      "id": "A01-watch",
      "type": "weak_answer",
      "card_id": "A01",
      "prompt": "What is the main risk in a weak answer?",
      "options": ["overclaims maturity", "weak metric framing", "too generic", "sounds defensive"],
      "correct_index": 0,
      "explanation": "Why the best option is best."
    }
  ]
}
```

Supported drill types:

- `mcq`
- `weak_answer`
- `metric_precision`
- `hostile_followup` for non-option follow-up/defense prompts

Current limitation: per-option explanations are not embedded yet. Firmware omits the weaker-options block and logs the missing detail.

Embedded firmware caps drill options at four choices. The build script now keeps the correct option when trimming longer source option lists, selects up to three distractors, and remaps `correct_index` to the embedded 0-3 index. Runtime validation also skips invalid keyed MCQ/weak-answer/metric drills from Drill and Exam pools.

For future language-learning decks, MCQ options can be short answers, readings, meanings, grammar choices, or example-sentence completions. UI labels should remain generic; deck categories provide the subject-specific meaning.

## Glossary Schema

```json
{
  "schema_version": 2,
  "terms": [
    {
      "category": "AI/RAG",
      "term": "RAG",
      "definition": "Retrieval-augmented generation.",
      "interview_importance": "Why this matters in an AI/PM interview.",
      "example": "Example sentence."
    }
  ]
}
```

Supported categories:

- `AI/RAG`
- `Evals`
- `Metrics`
- `Product`
- `Interview`

Future SD glossary categories may be deck-specific. Firmware currently has a fixed category grid for the embedded PaperCoach set, so new category grids require a firmware-side mapping update.

## Japanese Item Schema (embedded-only, v5.9-dev1)

Japanese Daily Questions uses a separate `JapaneseItem` struct, embedded directly in firmware
(`kJapaneseDayItems`) — not an SD-loadable schema yet. One originally written N3-style sample set
(Week 1, Day 1; 11 items) is embedded. Fields per item:

```json
{
  "source_id": "n3sample_w1d1_001",
  "book_id": "n3_sample_w1d1",
  "jlpt_level": "N3",
  "week": 1,
  "day": 1,
  "lesson_id": "w1d1_kanji",
  "item_id": "w1d1_q001",
  "source_question_number": 1,
  "category_japanese": "もじ",
  "macro_area": "kanji",
  "prompt_japanese": "「郵便局」の読み方として正しいものはどれですか。",
  "choices": ["ゆうびんきょく", "ゆうべんきょく", "ゆびんきょく", "ゆうびんきょうく"],
  "correct_choice": 0,
  "answer_sentence_japanese": "彼は郵便局へ荷物を取りに行きました。",
  "explanation_japanese": "「郵便局」は「ゆうびんきょく」と読みます。郵便局は荷物や手紙を送る場所です。",
  "explanation_english": "Post office is read 'yuubinkyoku'. It is a place to send packages and letters.",
  "grammar_pattern": "",
  "vocabulary_items": "郵便局,荷物",
  "kanji_items": "郵,便,局",
  "concept_ids": "kanji_yuubinkyoku"
}
```

`category_japanese` is one of もじ (kanji)/ごい (vocabulary)/ぶんぽう (grammar); `macro_area` is the
matching English tag (`kanji`/`vocabulary`/`grammar`) used for the Results breakdown. `book_id` is
`n3_sample_w1d1` (originally written content, not an extraction of any copyrighted book — the full
新にほんご500問 book is intentionally not imported).

Japanese answer records use a separate `JapaneseSessionResult` schema (RAM-only, not yet persisted
to SD): `millis_at`, `item_id`, `source_id`, `macro_area`, `category_japanese`, `week`, `day`,
`selected_choice`, `correct_choice`, `correct`. This is independent of the Results Schema below,
which remains Interview Practice/Drills/Exam only.

## Later TODO

- SD-loadable Japanese deck schema (the v5.9-dev1 Japanese item schema above is embedded-only).
- Dynamic deck-defined categories.
- Generic stages array.
- Category cap increase.
- Glossary search.
- SRS/long-term history.
- Japanese volunteer notes and multi-source concept UI.

## Results Schema

```json
{
  "schema_version": 1,
  "session_id": 123456,
  "result_count": 1,
  "results": [
    {
      "millis": 98765,
      "session_id": 123456,
      "item_id": "A01-watch",
      "card_id": "A01",
      "mode": "Drills",
      "type": "weak_answer",
      "category": "Weak Answer",
      "selected_option": "B",
      "best_option": "A",
      "correct": false,
      "first_attempt": true,
      "reader": "Reader M"
    }
  ]
}
```

Writes are safe: temp file first, then rename to `/papercoach/progress/session_results.json`.
