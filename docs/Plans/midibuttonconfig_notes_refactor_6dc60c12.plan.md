---
name: MidiButtonConfig Notes Refactor
overview: Analysis of whether to remove MidiButtonConfig::Notes semantic layer (C2, D2_SHARP, etc.) in favor of raw integers, or to expand semantic usage. Includes trade-offs and recommended direction.
todos: []
isProject: false
---

# MidiButtonConfig::Notes — Semantic vs Integer Refactor

## Current State

**[MidiButtonConfig.h](include/Utils/MidiButtonConfig.h)** defines 44 chromatic note constants (C2=36 through G5=79, plus NOTE_3=3):

```134:184:include/Utils/MidiButtonConfig.h
// MIDI Note Constants (Chromatic from C2)
namespace Notes {
    constexpr uint8_t NOTE_3 = 3;    // Special momentary button for length edit
    constexpr uint8_t C2 = 36;
    constexpr uint8_t C2_SHARP = 37;
    // ... through G5 = 79
}
```

**Usage:**

- [MidiButtonConfig.cpp](src/Utils/MidiButtonConfig.cpp): `Notes::C2`, `Notes::C3 + i`, etc. for button configs
- [MidiButtonActions.cpp](src/MidiButtonActions.cpp): `Notes::D2_SHARP` (39) for transport LED feedback
- Already mixed: `addButton(ButtonConfig(3, 16, ...))` and NavButton array uses raw 64–75

**Duplication:** [MidiMapping.h](include/Utils/MidiMapping.h) has `Defaults::NOTE_C2=36`, `NOTE_C2_SHARP=37`, etc. (same values, different names).

**DROID ini:** Uses raw integers (`note1=36`, `note2=37`, etc.) — no semantic names.

---

## Recommendation: Remove Semantic Layer (Use Integers)

Given your preference and the codebase patterns, removing the semantic layer is the better fit.

### Why integers work better here

1. **No functional meaning** — C2 vs 36 does not change behavior. Button functions (Record, Play, etc.) come from `ButtonConfig`, not from note names.
2. **Aligns with DROID** — [midilooper_v1.ini](droid/midilooper_v1.ini) uses raw numbers. Matching firmware and ini makes debugging and cross‑reference easier.
3. **Already inconsistent** — `3`, `64`–`75`, and `Notes::C3 + i` mix styles. One consistent style simplifies the code.
4. **Less indirection** — `Notes::D2_SHARP` → 39 → “what is D2_SHARP?” adds a lookup without improving behavior.
5. **Pitch semantics are misleading** — These are control buttons, not pitched notes. C2/D2_SHARP suggest musical pitch, but the values are just MIDI note numbers in the control range.

### When semantics would help

- Displaying note names on a piano roll (e.g. “C4”)
- Mapping a physical piano layout to notes
- Debug logs showing “C2” instead of “36” for musicians

The current `Notes` namespace is not used for display or musical context, only for configuration, so those benefits do not apply.

---

## Refactor Scope


| File                                                   | Change                                                                                                                     |
| ------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------- |
| [MidiButtonConfig.h](include/Utils/MidiButtonConfig.h) | Remove `Notes` namespace; keep only `NOTE_3 = 3` if still needed, or inline it                                             |
| [MidiButtonConfig.cpp](src/Utils/MidiButtonConfig.cpp) | Replace `Notes::C2` → `36`, `Notes::C3 + i` → `48 + i`, etc. Add brief comments where useful (e.g. `// 36 = C2 per DROID`) |
| [MidiButtonActions.cpp](src/MidiButtonActions.cpp)     | Replace `Notes::D2_SHARP` with `39` (transport LED)                                                                        |
| [MidiMapping.h](include/Utils/MidiMapping.h)           | Optional: keep or remove duplicate `Defaults::NOTE`_* based on whether MidiMapping is still used for button config         |


### Example transformation

**Before:**

```cpp
addButton(ButtonConfig(Notes::C2, 16, "Record/Overdub")
addButton(ButtonConfig(Notes::C3 + i, 2, ...)
```

**After:**

```cpp
addButton(ButtonConfig(36, 16, "Record/Overdub")   // C2
addButton(ButtonConfig(48 + i, 2, ...)             // C3–B3
```

---

## Alternative: Keep and Expand Semantics

If you prefer semantic names for readability or external docs:

- Use them consistently (replace raw 64–75 and `3` with `Notes::E4`, `Notes::NOTE_3`, etc.).
- Add a short comment mapping to numbers, e.g. `// 36`.
- Consider consolidating with MidiMapping `Defaults::NOTE_*` to avoid duplication.

This improves readability for people who think in note names but adds another layer to maintain and does not match the DROID ini.

---

## Summary

Recommendation: **Remove the `MidiButtonConfig::Notes` semantic layer** and use raw integers. The current semantic names add indirection without clear benefit for this control-mapping use case.