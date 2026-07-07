# Runtime architecture — capture storage

**Parent:** [RuntimeArchitecture.md](RuntimeArchitecture.md)

---

## Question

> What is the authoritative timeline?

Everything downstream is derived. Consumers must not treat materialized or display lists as canonical.

---

## Owners

| Store | Owner | Role |
|-------|-------|------|
| `Loop::passes` | `Loop` | **recordPass**, **overdubPasses[]**, **editPasses[]** |
| `Loop::capture` | `Loop` | Live record/overdub append buffer |
| Chunk payload | `LoopEventStore` pool | 256-event PSRAM chunks referenced by passes |
| `NoteEditSession.store` | `EditManager` | Live RAM overlay during NOTE_EDIT (not a pass row until commit) |

**Rule:** Hot playback uses **merge** of active capture passes + materialize — not full-loop flatten on every stop. See [LOOP_MIDI_STORAGE_AND_VALIDATION.md](../../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md).

---

## Storage revision

Any of these bumps storage authority and invalidates derived representations:

- `sealCapture` / publish pass
- `saveNoteEditPass` / `closeNoteEditPass`
- NOTE_EDIT session store mutation
- Undo restore (`restoreFromSnapshot` — always deep clone)
- SD load into slot

Callers use `Track::invalidateCaches()` (and NOTE_EDIT `sessionPreviewRevision_` where applicable) — not ad-hoc consumer rebuilds.

---

## Non-goals

- Display-phase ticks, wrap, or viewport (→ [IntervalProjection.md](IntervalProjection.md))
- OLED layout (→ [Display.md](Display.md))
- Playback sort order (→ [Playback.md](Playback.md))

**Normative storage guide:** [Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md](../../Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md)
