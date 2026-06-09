# PaperCoach PRD

## Product Goal

PaperCoach turns PaperBadge into a focused, offline e-ink learning engine. The current embedded content is senior AI/Product Manager interview preparation, but the product should also support future decks such as Japanese grammar, vocabulary, kanji, certification drills, or other compact study sets.

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
- High Contrast is the recommended default style.
- Balanced refresh is the recommended default: clean refresh for major transitions, feedback, badge, and image zoom; faster refresh for ordinary page turns.
- Deep sleep is not automatic until PaperS3 wake behavior is physically verified.
- Results are paginated instead of compressed into a tiny dashboard.

## Mode Definitions

- Badge: static conference badge with optional QR/photo zoom.
- Practice: read answer cards by stage: Question, Answer, Anchor, Watch-out, Follow-up, Defense.
- Drills: answer MCQ-style drill items and receive feedback after selection.
- Exam: answer a mixed set without immediate feedback; review score at the end.
- Glossary: browse terms by category grid.
- Results: view RAM/SD-backed session analytics.
- Settings: reader, refresh, power, and badge sleep controls.
- Debug: render trace, deck export, visual QA, font lab, and power audit.

## Navigation Model

- Home menu shows: Badge, Practice, Drills, Exam, Glossary, Results, Settings, Debug.
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
