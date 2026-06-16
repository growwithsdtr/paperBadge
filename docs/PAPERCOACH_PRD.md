# PaperCoach PRD

## Product Goal

PaperBadge is the hardware shell. PaperCoach is the focused, offline e-ink learning mode that runs inside it. The current embedded content pack is senior AI/Product Manager interview preparation, but the product should also support future decks such as Japanese N3-style grammar, vocabulary, kanji, certification drills, or other compact study sets.

It should make prepared cards easier to rehearse, expose weak areas through drills and exams, and keep the experience readable, low-distraction, low-ghosting, and low-power.

## Target User

Primary current user: Daniel, preparing for senior AI/Product Manager interviews, especially roles involving AI product strategy, RAG, evaluation, metrics, and executive communication.

Future user shape: a learner using offline deck content where items can be practiced, drilled, tested, reviewed, and explained without network access.

## Use Cases

- Review prepared answer cards before an interview.
- Drill weak answer patterns, metric framing, and confidence labels.
- Take a short school-test style exam with no immediate feedback.
- See session results, weak areas, recent misses, and next-practice recommendations.
- Check AI/PM/interview glossary terms offline.
- Use static badge mode at conferences without periodic redraws or radios.
- Reuse the same Practice/Drills/Exam/Glossary/Results shell for non-interview decks.
- Support future Japanese N3-style grammar, vocabulary, and kanji drills with weak-area tracking and SRS-like review.

## Design Principles

- Offline-first: embedded fallback content must work without SD.
- E-ink-native: avoid unnecessary redraws, avoid large filled black controls, and force clean refreshes only at high-ghosting transitions.
- Readable under pressure: compact headers, high contrast, stable footer controls, and no decorative clutter.
- Clear navigation model: content taps page within the current item; footer arrows move between items.
- Safe power behavior: no Wi-Fi, Bluetooth, cloud, audio, or LLM calls; deep sleep remains experimental until wake behavior is physically verified.
- Generic learning language: chrome should say Practice, Drills, Exam, Glossary, Results, Category, Question, Answer, Explanation, and Weak areas rather than hardcoding interview labels.

## Current UX Decisions

- Outlined buttons are preferred over black-filled buttons because large black regions ghost heavily on e-ink.
- Reader M is the recommended QA size; Reader L can be useful for short Practice cards but may reduce fit.
- Sans Bold-like is the recommended English default style; High Contrast remains available for maximum density/contrast. Both use the same FreeSansBold bucket, with visual differences coming from density/layout rather than a separate font engine.
- Balanced refresh is the recommended default: clean refresh for major transitions, feedback, badge, and image zoom; faster refresh for ordinary page turns.
- Deep sleep is not automatic until PaperS3 wake behavior is physically verified.
- Japanese live rendering shipped as a separate, self-contained mode in v5.9-dev1 (Daily Questions
  prototype using one embedded Week 1 Day 1 N3-sample set). It uses its own sanitize/wrap/font path
  (`lgfxJapanGothic_*`) and never routes through the English `sanitizeCoachText()`/`wrapTextToLines()`
  functions. SD-loadable Japanese decks, the full 新にほんご500問 book, SRS, and volunteer notes
  remain future work.
- Results are paginated instead of compressed into a tiny dashboard. Japanese Results is a separate,
  simpler RAM-only tally and is never combined with the Interview Practice/Drills/Exam Results screen.

## Mode Definitions

- Badge: static conference badge with optional QR/photo zoom.
- Practice: read answer cards by stage: Question, Answer, Anchor, Watch-out, Follow-up, Suggested response.
- Drills: answer MCQ-style drill items and receive feedback after selection.
- Exam: answer a mixed set without immediate feedback; review score at the end.
- Glossary: browse terms by category grid.
- Results: view RAM/SD-backed session analytics.
- Settings: Reader size, Refresh, Power, Orientation, and Advanced.
- Advanced (under Settings): render trace, deck export, visual QA, font lab, and Power Lab pages.
- Japanese (v5.9-dev1): a separate, self-contained N3-sample mode — Daily Questions (one embedded
  Week 1 Day 1 question per screen with immediate feedback), Mock Test (placeholder only),
  Reference (grammar/vocab/kanji tags from the same set), and Results (RAM-only, separate from the
  Interview Practice/Drills/Exam Results screen).

## Navigation Model

- Home menu shows 8 buttons: Badge, Practice, Drills, Exam, Glossary, Results, Settings, Japanese.
  Debug is not a top-level Home button; diagnostics live under Settings → Advanced.
- Japanese menu shows 4 buttons plus Home: Daily Questions, Mock Test, Reference, Results.
- Japanese Daily Questions: tap a choice to see immediate feedback (correct/wrong, correct choice,
  Japanese answer sentence, Japanese explanation, English meaning, grammar/vocab/kanji tags); Next
  advances to the next question with wraparound; Home returns to Home.
- Practice entry shows: Must cards, All cards, Continue last card, Help/Legend.
- Practice reading: upper content tap = previous page; lower content tap = next page; footer left/right = previous/next card with wraparound; footer center = Home.
- Drills and Exam: question and choices render together when possible; split option pages repeat a compact question reminder.
- Footer controls are consistent: left arrow, Home icon, right arrow where item navigation exists; Home-only screens use a centered Home button.

## Learning Model

- Practice builds recall from prepared cards.
- Drills target weak answer risks, confidence framing, metric precision, and framework choices.
- Exam mode tests mixed recall without immediate feedback.
- Glossary provides quick conceptual refresh for AI/RAG, evals, metrics, product, and interview communication.

## Analytics Model

Each drill/exam answer records session id, millis timestamp, item id, card id, mode/type/category, selected option, best option, correctness, first-attempt flag, and reader setting. RAM is primary; SD persistence writes `/papercoach/progress/session_results.json` using temp-file then rename.

Results shows total answered, correct percentage, category bars, weakest areas, recent misses, and a recommended next practice. Recommendation favors categories under 70% accuracy, then missed Must-card practice, then mixed drills.

## Power Model

The device disables Wi-Fi, Bluetooth, speaker output, and IMU polling. Battery polling is cached. Badge mode is static and avoids auto-redraws. Badge sleep supports Off, Light, and Deep experiment; Light uses timer-based light sleep after idle, while Deep experiment is blocked until touch wake is verified.

## Roadmap

- Add per-option explanations to drill content.
- Add persisted multi-session history and trends.
- Add glossary search and SRS only if the on-device interaction stays simple.
- Physically verify safe touch wake for deep sleep before enabling it.
- Add more interview-specific exam balancing once category coverage grows.
- Add UTF-8/Japanese live rendering, dynamic deck-defined categories, generic stages arrays, a higher category cap, glossary search, and SRS/long-term history.
