# Engine UI / UX guide

Status: guide (2026-11-11), for the fixed-memory engine's screen, windows and DROID controls. It collects the design
language and the workflow decisions the owner has made so far, so new screens and windows start from them. Not
authority: where it and a `DEC-###` in [`DECISION_LOG.md`](../DECISION_LOG.md) disagree, the decision wins, and the
guide is corrected. Code: `src/EngineFirmware/EngineDisplay.h` (drawing), `include/ContentEngine/Surface.h` (input,
gestures, window state), `droid/midilooper_v1.ini` (the DROID patch).

## 1. Principles

1. **Consistency over novelty.** Reuse an element that exists (the window frame, the hint row, the save dots, the
   loop bar, the A/B pattern) before inventing one. The owner asks for this explicitly (DEC-295 amendments).
2. **Musical, not instant.** A change is heard on a musical boundary: the next beat for a setting (DEC-292), a bar
   line for a scheduled turn (DEC-294, DEC-295 am. 3). Work that takes time is started ahead so it lands on time.
3. **No intermediate sounds.** While the musician browses values, only what they chose to hear changes. Editing the
   thing not playing is silent until it is chosen (DEC-295 am. 4).
4. **Static windows.** A window's layout does not move while it is used; only highlights, fills and values change
   (DEC-295: "a window should be static so only switch the highlight").
5. **Every change is undoable, once.** A window visit is one undo step (replace in place); leaving keeps what plays.
6. **The hardware is the interface.** Everything is reachable from the DROID's buttons and faders (the encoder is
   optional); serial verbs are diagnostics only.
7. **Mockup first.** New or changed screens are shown as mockups before they are built (§ 11).

## 2. The screen

- SSD1322, 256 × 64, 4-bit grey (0–15). Two fonts: the **3 × 5 label font** (`label`, 4 px a letter) for all window
  content, hints, labels; the **5 × 7 font** (`text`) only for the status band's big values.
- The performance screen: the track list on the left, the piano roll, and the status band on the bottom rows
  (`EngineDisplay.h` header comment): the loop's progress bar (DEC-282, DEC-283), the transport icon and position, the
  save dots over the BPM.
- A missing glyph is added to `micro()` (rows of 3 bits, bit 14 top-left), e.g. `%` (DEC-294).

## 3. Windows (Atari GEM style)

Every window is a GEM window over the performance screen:

- A 3-px drop shadow (level 2), a black body, a 1-px frame (10).
- A hatched title bar (rows of level 5 every other line), its rule under it; the **title centred** in its own black box
  (13). Title-bar **widgets** sit at the right edge, where GEM keeps its fuller box: e.g. `[TRK] ☐ AUTO Q` — the button
  that toggles it, a 5 × 5 checkbox, the name (DEC-295 am. 1).
- Content spaced as the **load / save window** (DEC-267): its frame (x 27–228), rows 9 px apart, columns separated by dim
  vertical rules (4), a dim rule under a header row.
- **Static** (principle 4). A window opens on the current state; the musician's choices are highlighted in place.
- **Leaving:** a long press of any button closes a window (DEC-279 am. 2). A window with live changes **applies**
  (keeps what plays) rather than cancels — its hint says `APPLY`, otherwise `EXIT`. The gesture that opened a window
  toggles it closed (e.g. note 43 for GROOVE).

## 4. Brightness and highlight

| Level | Use |
|------:|-----|
| 15 | a value being changed; a bright playhead; done dots |
| 13 | values, titles; the inverted highlight's fill |
| 10 | normal text, frames, unselected header names |
| 9 / 7 | hint actions / hint button names (`hintLabel`: the `[BTN]` dimmer than its action) |
| 6 | field labels (GROOVE, AMOUNT), a loop bar's unplayed part |
| 4 | rules, dim alternatives |
| 2–3 | waiting / pending (a column not playing, pending dots), the shadow |

- **Selection** (the field the encoder / faders change): inverted — a level-13 box, the text at 0.
- **What is active / heard** (e.g. `OFF A B`): the header inverted the same way; it moves, the layout does not.

## 5. The hint row (keys along the bottom)

- Every window shows its keys on the screen's bottom row (`hint()`: cleared rows 57–63, text at y 59), as the load /
  save window does. Format: `[BTN] ACTION`, the button name dimmer (`hintLabel`).
- Order: the moves first — `[REC]{ [EDT]}` (up / down) — then the window's actions; the leave action right-aligned
  (`HOLD [PLY] APPLY`, `HOLD EXIT`).
- `2*` marks a double press, `HOLD` a long press: `[ADD] A/B  2*OFF  HOLD CYCLE` (the button named once, its gestures
  after it). The row holds 64 characters; keep the left part clear of the right-aligned action.
- The hints change with the mode (cycling: `[ADD] KEEP  2*OFF  HOLD STOP`).

## 6. Button roles

The same buttons mean the same things across windows:

| Button | In a window |
|--------|-------------|
| **REC** / **EDIT** (`[REC]` / `[EDT]`) | move the selection up / down (previous / next field) |
| **Encoder** | turn: the selected value; push: next field |
| **TRK** (next track) | **enter**: the window's confirm (CONFIRM, OPEN, SAVE, LOAD in the load / save window) or its title-bar checkbox (AUTO Q) |
| **ADD** (NOTELEN) | the **value / compare** key: single toggles A ↔ B, double back to the base (FULL, OFF), hold the mode (a popup, the cycle) |
| **PLAY/STOP** hold | apply / leave (the DROID sends its long press as note 43); its short press does nothing in a window (never stops the transport) |
| Any other button held | leave (apply) |

ADD's pattern is the one NOTELEN already has in performance: single press switches the loop's ranges A / B, double the
whole stored loop (DEC-284 am. 3) — the GROOVE window's A / B / OFF follows it (DEC-295 am. 5). Outside windows: EDIT
short toggles the track buttons' select / mute mode, its long press cycles NOTE_EDIT ↔ LOOP_EDIT, a 1.5 s hold leaves
(DEC-291).

## 7. Feedback elements

- **Save dots** — four single pixels 3 px apart (`SaveIndicator`): dim (2) while pending, one running (8) while the
  work runs, bright (15) for 0.8 s once done. Reuse them for any background job (the groove build: DEC-295 am. 3).
- **Loop bar** — the main screen's progress bar (DEC-282): a 1-px line, a gap at every bar line (the finest of 1 / 4 /
  8 / 16 / 32 bars that fits), `[ ]` brackets around the part playing, its playhead bright (15), the rest 6, outside
  3. Reuse it for anything that counts down in bars (the GROOVE cycle's turn under each column).
- **Status words** for what cannot happen: `WAIT`, `TOO BIG`, `FAILED` — short, uppercase, beside the thing they
  concern; no toasts for routine states (DEC-276 am. 1 removed them).

## 8. Changes, timing and audio

- A fader / encoder change is applied once it **rests** `kLengthSettleMs` (300 ms) — one undo step for a burst.
- Heard from the **next beat** (DEC-292) — build first, then switch; a turn (a cycle, later a chain) is built **a bar
  ahead** and switched on its bar line (DEC-295 am. 3).
- Only what is heard is rebuilt; the rest waits silently (principle 3).

## 9. The A / B compare pattern

Introduced by the GROOVE window (DEC-294, DEC-295), meant for reuse (the planned "loop chain": slots queued as a
preset, `A A A B A B A C`):

- Two editable versions side by side, **A** (as it was) and **B** (a copy to change), plus **OFF**.
- ADD single A ↔ B, double OFF, hold **cycle**: A and B take turns of a global number of bars (2 by default) from the
  next bar line; while cycling ADD keeps the one playing. OFF is never part of the cycle.
- The heard one is the inverted header; each column's loop bar counts its turn.
- Applying keeps what plays; one undo step.

## 10. Faders (the DROID's motor faders)

- Each window that uses the faders has its **own DROID bank**: the engine sends a Program Change on channel 16
  (`Surface::bank`: 0 loop edit, 1 note edit, 2 groove); the patch's `[motorfader]`s use `select = _EDIT_MODE`,
  `selectat = <bank>`.
- Prefer **CCs** for window faders (the GROOVE bank: CC 20–23 on channel 16); pitch bend only where 14 bits are needed
  (tempo, note position).
- Order the faders as the window's fields (F1, F2 the first column; F3, F4 the second).
- When a bank opens the DROID **reports each fader's position**: the first CC of each fader after opening is not a
  move (`faderSeen_`, DEC-295 am. 2). Motor writes need echo handling (NOTE_EDIT: `EditSurface` motor settle).

## 11. Workflow for UI changes

1. **Mockups first**, at the real size and font: render 256 × 64 frames in Python with the `micro()` glyphs over a real
   frame (`!ENGINE fbdump` → `scripts/render_display_dump.py`); two to three alternatives, each a short caption; save
   them in `docs/Plans/images/` and show them. Build only what the owner picks.
2. Keep the decision in `DECISION_LOG.md` (a `DEC-###` or an amendment quoting the owner), and update this guide when
   a rule here changes.
3. Verify on the device with `fbdump` frames of each state, not only native tests.
4. Leave the owner's set as found: undo a test visit on the slot visited — only that slot (the history pool is shared;
   when full, a new step evicts another slot's oldest, so "every slot whose depth changed" is wrong).

## 12. Words

Short, uppercase, the musician's vocabulary: `APPLY`, `KEEP`, `CYCLE` (Cubase), `AUTO Q` (Cubase's auto quantize),
`OFF / A / B`, `GROOVE`, `AMOUNT`. Avoid words with another meaning on the device (`AUDITION` is the sets browser's
preview). Names follow [`NAMING.md`](../Authority/NAMING.md).

## References

DEC-267 (the load / save window), DEC-276 (transport under an external clock), DEC-279 (copy mode; a long press leaves),
DEC-282 / DEC-283 (the progress bar), DEC-284 (ranges A / B, NOTELEN), DEC-291 (EDIT), DEC-292 (open until closed, the
next beat), DEC-294 / DEC-295 and its amendments (the GROOVE window). Mockups: `docs/Plans/images/groove_*.png`.
