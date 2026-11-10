# Groove: non-destructive quantize, swing and groove templates

Status: design, owner-reviewed direction (2026-11-10); not authority (see `docs/Authority/README.md`). Implementation
needs a `DEC-###` (formal trigger: a persistent state-model change and a composition rule change).

Owner direction (2026-11-10): quantize the full loop, or only range A or B (DEC-284 amendment 3); non-destructive; an
amount (default 50 %) on a fader or the encoder; swing; 1/16 by default, other grids selectable; interactive enough to
jam with (and later record jams); the piano roll shows the notes where they play; opened by a long press on PLAY/STOP;
a short list of groove templates rather than one swing knob (reference: jazz drum MIDI feels — medium / uptempo swing,
bebop, brush ballad, bossa / Latin, fusion); an auto-groove option that re-applies at the loop wrap.

## 1. Model: the groove pass

A groove setting makes a new state of the slot whose content is:

```
[ source passes … ] [ groove pass ] [ passes made after it … ]
```

- **Source passes** are the slot's passes as they were, kept in the state but **not composed**. They are what every
  rebuild starts from, so the original timing is never lost (amount 0 % or groove off = a state without the groove
  pass and without the source marking).
- **The groove pass** is one sealed run holding the source passes' notes **as heard** — the composer's output after
  overlap / occlusion (DEC-244 order) — moved to their grooved positions. It keeps each note's `noteId`, velocity and
  length, so NOTE_EDIT and copy work on it as on any pass, and the piano roll, LEDs and progress bar show the notes
  where they play with no change to the drawing code.
- **Its marking row** (fit run, after DEC-283 / DEC-284's window and range rows, `tick` 0xFFFFFFFF): the groove settings
  (template, grid, amount, scope, auto) and the **position** of the groove pass in the state's run list. Positions,
  not RunIds: a state's run list survives save / load in order, RunIds do not (see § 6, finding R1).
- The composer skips runs before the groove pass position; everything else composes as today. No HideNote rows, so the
  edit-hide index (4,096 hides per state) is not used at all — the cap that could silence a large loop is gone.

Red-team findings folded in (2026-11-10):

| # | Finding | Folded in as |
|---|---------|--------------|
| 1 | Hidden notes still occlude (OVL-8): hiding raw rows of several passes leaves ghosts cutting notes at old places | the groove pass is built from the notes as heard; the sources are not composed at all |
| 2 | 4,096 hides a state; more makes the slot uncomposable | no hides (sources skipped by position); the build is chunked (§ 3) |
| 3 | Same-pitch notes pulled onto one tick, or overlapping after the shift | the builder merges same-pitch notes on one tick (the longer, the louder) and ends a note where the next same-pitch note starts |
| 4 | A note pulled past the stored loop's end is kept but never played | grooved positions wrap around the stored loop (DEC-283 frame); with range scope a note stays inside its range |
| 5 | Re-grooving moves edited notes again (drift) | edit passes (`kEdit`) after the groove pass stay after it, untouched, on a rebuild; overdub passes after it are folded into the sources on the next rebuild (§ 4) |

## 2. The timing

For a note at stored position `t` (absolute in the stored loop, DEC-283), with grid `g` and the template's offset table:

```
q      = nearest grid line to t                       (grid: 1/32, 1/16, 1/8, 1/4, 1/8T, 1/16T)
target = q + template.offset[(q / g) mod template.steps] × g
t'     = t + amount × (target − t)                    (amount 0–100 %, default 50 %)
```

- The **template** is a short table of per-step offsets (and optional velocity accents), one beat or one bar long,
  repeating — swing is not one loop-wide shift but "every second (or third) step later". Counted from the stored loop's
  bar lines, so a range starting off-beat keeps the feel.
- Classic-machine swing (owner 2026-11-10: "grooves extracted from famous drum samplers like the older MPCs and the
  SP-1200"). What is documented: Roger Linn's swing (LM-1, MPC60 / 3000) delays **every second 16th** (or 8th) inside its
  pair; the percentage is where that note sits in the pair (50 % straight, 66 % triplet feel, 75 % dotted). The LM-1
  offered 50 / 54 / 58 / 60 / 62 / 66 / 70 / 75 %; the SP-12 / SP-1200 (and Drumulator) almost the same set — commonly
  given as 50 / 54 / 58 / 62(63) / 66(67) / 71 % plus 8th and 16th triplets. Linn: no random timing in the great grooves;
  swing matters most. At this engine's 48 ticks a 16th, the delay of the second 16th is `(p − 50) / 50 × 48` ticks:
  54 % → 3.84, 58 % → 7.68, 62 % → 11.52, 66 % → 15.36, 71 % → 20.16, 75 % → 24 — finer than the MPC60's 96 PPQ
  (24 a 16th), so the machines' own rounding (e.g. 54 % ≈ 4 ticks at 96 PPQ) is a choice per template.
  Measured per-machine grooves exist: *Grooves From Mars* (Samples From Mars, free) — 478 MIDI / `.agr` templates from
  27 machines including five MPCs and the SP-1200, made by analysing the machines' audio for where each 16th fell. Their
  licence must be checked before shipping any table derived from them; a host script could convert a user-supplied
  groove MIDI file into a template table instead.
- First list (to tune by ear on the DROID; each a table, easy to add to):
  `STRAIGHT`; `MPC 16 54 / 58 / 62 / 66 / 71` (the second 16th delayed); `MPC 8 58 / 66` (the second 8th);
  `SP 16 54 / 58 / 63 / 67 / 71`; `TRIPLET 8`, `TRIPLET 16`; `JAZZ RIDE` (triplet swing with a slightly late
  backbeat), `BOSSA` (straight 8ths, 3rd and 7th 16th pushed early), `LAID BACK` (everything a little late), `PUSH`
  (everything a little early). Velocity accents optional per template.
- Note ends move with their starts (lengths kept). Scope FULL / A / B selects the notes by their stored position.

## 3. Building without stalling or refusing (owner: "keep the old state and make it the new truth only after completion")

- **Chunked, like the playback windows and `SaveJob`**: the builder composes the sources a window at a time and writes
  the grooved rows in grains from the main loop's spare time; the current state keeps playing untouched.
- **Atomic publish**: only when the groove pass is complete and sealed is the new state committed (one undo step for a
  GROOVE window visit, replace-in-place as LOOP_EDIT). A newer setting arriving mid-build restarts the build; the
  half-built run is released.
- **Musical switch-over**: the tracks playing the slot move to the new state at the next **bar line** (or the launch
  quantisation, DEC-274), never mid-beat — so a change is heard as a musical step.
- **No refusal**: if the content rows run short mid-build, the build pauses while the old state stays the truth, and it
  first frees space the way the history already does (oldest undo states of the shared pool dropped) before continuing.
  Only if the slot itself cannot hold two copies does it stop, with `GROOVE: NO ROOM` and the old state unchanged.
- Cost: rows = notes in scope × 2 (NoteOn + NoteOff) once — no hide rows.

## 4. Passes made after a groove; auto-groove

- **Overdubs** after the groove pass play as recorded. With **AUTO** on (a GROOVE setting), the slot regrooves at the
  next loop wrap after an overdub pass closes: the overdub pass becomes a source and the new groove pass covers it.
- **NOTE_EDIT edits** after the groove pass are deliberate placements: they stay after it on a rebuild and keep their
  positions (their hides act on the groove pass's notes by `noteId`).
- Undo steps back through these states as usual.

## 5. Controls (slice 2)

- **Long press PLAY/STOP** opens the GROOVE window (like the length popup): `TEMPLATE`, `GRID`, `AMOUNT`, `SCOPE`
  (FULL / A / B), `AUTO`. F1 amount, F2 template, F3 grid, F4 scope; the minimal setup: encoder push = next field,
  turn = value. A long press closes it; it hides after a few seconds idle.
- Each setting change builds in the background; the window shows `…` until it is heard.

## 6. Open findings to settle before or beside slice 1

- **R1 — fit-run lookups may not survive a load** (found while red-teaming): DEC-257's fit rows store the content run's
  RunId in `tick`; `LoadJob` gives the runs new RunIds and copies the rows unchanged. A shortened loop may then play
  notes past its end after a reboot. Confirmed by the design review's host probe (§ 7, C3); fixed first, as slice 0.
- **R2 — the undo depth (99)** was chosen, not measured. To size it: count states born per minute in real sessions
  (jam, edit, groove), and cost what a state holds — the manifest (~140 B), the rows only old states keep alive, and the
  preallocated store (`kHistoryDepth × (9 + 2 × runs)` bytes a slot file, a format-level size, DEC-253). A separate
  investigation.
- Measure on the device (slice 1): the build time of a dense 8-bar and a 64-bar loop, and the bar-line switch-over.

## 6b. Groove library on the SD card (owner 2026-11-10: "an import function so the pack can be downloaded and stored in
## a folder on the SD and pre load 10 most liked")

- **The folder.** The owner unzips a groove pack (e.g. *Grooves From Mars*, MIDI files) into `/GROOVES/` on the card,
  subfolders allowed. The firmware **only reads** it — never writes, renames or deletes there (device-SD rule); nothing
  from a pack ships in the firmware, so its licence is the owner's own download terms.
- **The import.** At boot, in the background after the set loads (as the library index), the firmware lists the
  folder (name, size, CRC) — up to a bound (e.g. 1,024 files, 64 KB a file, names cut to 24 characters). A file is read
  only when its template is needed: a Standard MIDI File, format 0 or 1, at its own PPQ. Every NoteOn is measured
  against its nearest 16th; per step of the file's cycle (one to four bars, ≤ 64 steps) the template keeps the mean
  offset (a signed fraction of a 16th) and the mean velocity relative to the file's mean. Steps without notes take no
  offset (the note stays on the grid). A file that is not a usable MIDI file is listed as such and skipped.
- **Inside the state, not on the card.** The groove marker carries the template itself (its ≤ 64 offsets and accents,
  ~8 marker rows) plus the file's name and CRC as a label. So a grooved loop plays, saves, loads, copies and rebuilds
  the same with the card's folder changed or gone.
- **The pack as downloaded (2026-11-10, *Grooves From Mars*, kept out of git via `.git/info/exclude` at
  `grooves/Grooves From Mars/`):** `MIDI/<machine>/<machine> 16 Swing NN.mid` (plus `16T`, `8T`, `4T`), 27 machines, 478
  MIDI files; LinnDrum, CR-78 and R8 also have `Patterns/` (whole patterns, not timing grooves — the import skips
  `Patterns` folders and the `Ableton/` tree). Measured: format 0, PPQ 96 (24 a 16th → offsets in steps of 2 engine
  ticks), 64 NoteOns = one per 16th over 4 bars, velocity 127 (timing only; the CR-78 hi-hat grooves carry velocity).
  The machines' own irregularity is in the files (MPC60 at 58 %: the 4th 16th at step 3.958, slightly early), so the
  template keeps **all 64 steps as measured** (4 bars, repeating), not a per-position mean. The shop's terms forbid
  reproducing its content without permission and the pack's Docs give no other licence: the files stay the owner's —
  not committed, not shipped; tests use synthetic MIDI fixtures.
- **The preloaded ten.** Ten templates are kept parsed in RAM (≈ 10 × 64 × 3 B) for instant selection in the GROOVE
  window. "Most liked" is measured, not guessed: every time a template is applied its count rises (kept in the engine's
  settings, not in `/GROOVES/`), and the ten most used are the preloaded ones. Before any use, the seed ten (by path
  under `/GROOVES/`, matched to the pack's names): `MPC60/MPC60 16 Swing 54`, `… 58`, `… 62`, `… 66`;
  `MPC3000/MPC3000 16 Swing 58`, `… 62`; `SP 1200/SP 1200 16 Swing 58`, `… 63`, `… 67`; `TR-909/TR-909 16 Swing 03`.
  A seed whose file is missing is skipped. The built-in templates (§ 2) are always there, with or without a card
  folder.
- **Getting the files onto the card**: copy the pack's `MIDI` folder's machine folders into `/GROOVES/` on the card (the
  card out of the Teensy, or a later serial upload tool — not in this design).
- **In the GROOVE window**: `TEMPLATE` lists the built-ins, then the preloaded ten, then `MORE…` — the folder by
  machine (subfolder) and name; choosing one reads and parses it on demand (a few ms), and it joins the counts.

## 7. Revision 2 — contracts from the design review (2026-11-10)

An independent review reproduced several failures with host probes (recorded in `PROJECT_STATE`, 2026-11-10). Each was
checked against the code and holds. They replace the matching parts of §§ 1–4:

| # | Review finding | Contract |
|---|----------------|----------|
| C1 | Folding a later overdub into the sources moves it before a retained edit: the composer's precedence is the run order (`Composer::noteOn`: later passes occlude and shorten, EDIT-1), so an edited note's length changed 49 → 200 ticks | **Prefix rule**: the sources are always a contiguous prefix of the run list; the groove pass sits right after it; every later run keeps its order. A rebuild with AUTO extends the prefix through the last overdub — edits before it go into the prefix *in order* — so precedence is never reordered. **No drift**: while building the as-heard copy, a note whose sounding version comes from an edit run keeps its composed position (the composer knows each note's run). |
| C2 | `windowOf` reads only the last three marker rows: a fourth (groove) row hides the window (start 96 / frame 1536 read as 0 / 768); resize and rotate rebuild the fit run and keep only window + ranges | `windowOf` reads every trailing marker row (`tick` 0xFFFFFFFF), by status, with no fixed count. The groove settings and the source boundary are part of the marks that `resize` / `rotate` (`SlotTransport::mark`) carry into the rebuilt fit run. |
| C3 | R1 is real: after save / load a source RunId changed (1 → 0) while its fit lookup kept the old one; a note past a shortened end came back | **Slice 0, before any groove work**: fit lookups name runs by their position in the state's run list (or are remapped on save and load), covered for save / load, slot copy, get a loop and the library's current-state reader. |
| C4 | Choosing the nearest straight grid line first, then its swung target, is wrong: grid 48, 75 % swing — a note at 73 picks line 96 and moves away from its swung target 72 | Compute the grooved targets of the neighbouring grid lines (the line before and after `t`, and their neighbours), take the target nearest to `t`; a tie goes to the earlier target; distances and the result wrap around the stored loop. |
| C5 | Peak storage is sources + the current groove pass + the replacement being built, and other references (playback windows, cursors, saves, other slots) can keep pages alive; dropping undo states need not free them | Before a build starts it **reserves** its rows, one run and one manifest (writer-class admission, as `LoadJob`). If it cannot, it waits — the old state stays the truth, the window shows pending — and retries when rows are reclaimed. No promise of freeing by dropping undo states; it may *ask* the history to drop the oldest, then still waits for the reservation. |
| C6 | Skipping every source run means the groove pass must carry everything they played, not only the notes in scope; and voice admission (Q-OUT-2) must not drop notes from the derived content | The groove pass is the **as-heard copy of the whole source prefix over the whole stored loop** (DEC-283 frame, not just the playing window): notes in scope grooved, notes out of scope and every non-note event (controllers, pitch bend, program changes) at their positions. It is built from the effective composition (occlusion, hides, edits) **before** voice selection, so a voice refused at output time is still in the content. Cost: the rows of that copy (≈ the sources' sounding rows), not "notes in scope × 2". |
| C7 | The edit-hide index is reserved over every edit run of a state, not only the composed ones | Source skipping applies to index preparation as well: `EditIndexStore::reserve` / `buildStep` see only the runs from the groove pass on. |
| C8 | A background build could publish over an intervening undo, load or edit | Each build carries (slot, base state identity, settings generation). It publishes only if the slot's current state is still that identity and the generation is the latest; otherwise its run is released and, if still wanted, a new build starts from the new state. |
| C9 | Which note survives a collision merge, and what later edits do when the merge splits again | A merge keeps the noteId of the earliest source note (pass order, then `seq`); the other is absent from the groove pass. A later edit names that surviving id. If a rebuild splits them again (a lower amount), both reappear and the edit still applies to the surviving id only — documented, tested. |

## 8. Slices

0. R1 — fit lookups that survive save / load, copy and get a loop (C3). A fix on its own, needed with or without groove.
1. The groove pass under contracts C1, C2, C4, C6, C7, C9: timing and templates, scope, the as-heard build, marker
   rows, save / load, edits / overdubs after it; native tests including the review's probes.
2. The background build: reservation and waiting (C5), generation-qualified publish (C8), switch-over at the bar line;
   device timing measurements.
3. The GROOVE window and its controls, AUTO, the encoder path.
4. The groove library (§ 6b): `/GROOVES/` listing, the MIDI-file template reader (host-tested on sample files), the
   template carried in the state, usage counts and the preloaded ten, `MORE…` in the window.
5. R2 — undo depth sizing (separate).
