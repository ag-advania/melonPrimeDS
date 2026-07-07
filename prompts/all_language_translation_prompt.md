# MelonPrime all-language AI translation prompt

You are translating UI strings for MelonPrimeDS, a desktop emulator/fork UI.

Input rows are JSON objects:
```json
{"surface":"exact","key":"Save","english":"Save","language_id":"Hindi","language_code":"hi","display":"हिन्दी","text":"","notes":"..."}
```

Return JSONL only. One JSON object per input row.

Rules:
- Fill `text`.
- Preserve `surface`, `key`, `language_id`.
- Preserve placeholders exactly: `%1`, `%2`, `{0}`, `%%`, numbers.
- Preserve technical/product tokens unless naturally localized:
  `MelonPrime`, `melonDS`, `MPH`, `ROM`, `HUD`, `DS`, `GBA`, `FPS`, `OpenGL`, `Compute`, `Soft`.
- Keep UI labels concise.
- Do not add explanations.
- Do not omit rows.
- Do not reorder rows if possible.
- For RTL languages, translate normally in the target script; do not insert manual bidi marks unless necessary.
- For emulator/game terms, prefer established local gaming/software terminology.
- For `object` rows, translate the text as a tooltip/help sentence if it is long.
- If the source is already a code-like value such as `ON`, `OFF`, `OK`, translate only when target UI commonly localizes it; otherwise keep it.
