# Content Schema

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

Current limitation: per-option explanations are not embedded yet. Firmware shows a clean placeholder and logs the missing detail.

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
