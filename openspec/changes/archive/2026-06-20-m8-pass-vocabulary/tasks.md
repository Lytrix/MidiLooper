# Tasks — m8-pass-vocabulary

**Change:** `m8-pass-vocabulary`  
**Design:** [design.md](./design.md)

---

## 1. Vocabulary (done / in progress)

- [x] 1.1 Lock **pass** in `.cursor/rules/Naming-Vocabulary-Teensy-Looper.mdc`
- [x] 1.2 OpenSpec change + update `note-edit-modification-session` docs (**moving note range**, **overlapNotes**)
- [x] 1.3 Update `openspec/changes/m8-edit/design.md` §4 — **edit pass** replaces span (legacy code names footnoted)
- [x] 1.4 Update `docs/Guides/LOOP_MIDI_STORAGE_AND_VALIDATION.md` + HITL rule prose

## 2. Code rename (phased)

- [x] 2.1 `NoteEditSession.spanIndex` → `editPassIndex`; `closeNoteEditSpan` → `closeNoteEditPass`
- [x] 2.2 `Edit.spanIndex` → `editPassIndex`; `UndoEntry.noteEditSpanIndex` → `noteEditPassIndex`; `spanEditIds` → `passEditIds`
- [x] 2.3 SD loop I/O: document field rename in v4 header comment (no format bump)
- [x] 2.4 Scripts: `--require-m8-pass-verify` + `--no-m8-pass-verify`; `--require-m8-span-verify` deprecated alias
- [x] 2.5 `pio test -e native` — 110/110 (2026-06-20)
- [x] 2.6 HITL edit baseline with pass wording in serial grep (Teensy `20260620_150954`; overlap checks pre-existing)

## 3. Overlap docs (shipped with note-edit-modification-session)

- [x] 3.1 Overlap engine: **sessionSpan** / **overlap footprint** → **moving note range** on **NoteEditFocus** (`movingNoteRange`, `isNoteWithinMovingNoteRange`, serial logs)

---

## PR order

| PR | Content |
|----|---------|
| A | Tasks 1.x — docs + rules only (this change) |
| B | Tasks 2.x — identifier rename + tests |
| C | Task 3.1 — moving note range rename (shipped with note-edit-modification-session) |
