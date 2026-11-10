# Groove: non-destructive quantize, swing and groove templates

Status: design, revision 3 (2026-11-10) — one effective contract after two independent reviews (their host probes are in
`PROJECT_STATE`, 2026-11-10). Not authority (`docs/Authority/README.md`). Implementation needs a `DEC-###` (formal
trigger: a persistent state-model change and a composition rule change).

Owner direction (2026-11-10): quantize the full loop, or only range A or B (DEC-284 amendment 3); non-destructive; an
amount (default 50 %) on a fader or the encoder; groove templates, 1/16 by default; interactive enough to jam with; the
piano roll shows the notes where they play; opened by a long press on PLAY/STOP; AUTO re-applies at the loop wrap after
an overdub; changes heard on a musical boundary; no refusal while it can still fit; classic-machine grooves, imported
from the owner's own copy of a pack on the card, the ten most used preloaded.

## 1. Model

A grooved state of a slot lists, in order:

```
[ source prefix … ] [ groove pass G ] [ passes made after G … ] [ fit run ]
```

- **Source prefix** — the slot's passes before the groove, kept and saved but **not played**. Every rebuild composes
  from it, so the original timing always survives (groove off = a state without G and without the marking).
- **Groove pass G** — one sealed run: the **effective content** of the prefix (§ 3) over the whole stored loop (DEC-283
  frame): notes in scope at grooved positions; notes out of scope and every non-note event (controllers, pitch bend,
  program changes) at their own positions. It keeps `noteId`, velocity and length, so NOTE_EDIT, copy and the piano roll
  treat it as any pass.
- **Passes after G** keep their order (precedence = run order, `Composer::noteOn`: later passes occlude and shorten,
  EDIT-1).
- **Marking** — fit-run marker rows (`tick` 0xFFFFFFFF), read by status with no fixed count (`windowOf` scans every
  trailing marker row): the window (DEC-283), ranges A / B (DEC-284), and the groove rows (§ 6): settings, the template,
  and G's **position** in the run list. Positions, not RunIds (RunIds are reassigned on load — see slice 0). `resize` and
  `rotate` carry every marker row into the rebuilt fit run (`SlotTransport::mark` extended).

## 2. Two views of one state (owner-visible behaviour unchanged; review 2, blocker 2)

- **Playback view** — the runs from G on. Its composer, edit-hide index and checkpoints are keyed by the state's
  identity, as today.
- **Rebuild view** — the prefix only. It is composed under a **distinct view identity** (the state's identity paired
  with a prefix tag; never equal to any manifest identity), so the edit-hide index cache (`EditIndexStore::reserve`,
  keyed by identity) never serves one view's hides to the other — a probe showed a deleted note reappearing when they
  shared one. Each view indexes its own runs in its own pass order.
- Index preparation follows the view: in the playback view `reserve` / `buildStep` see only the runs from G on; in the
  rebuild view only the prefix.

## 3. Extracting the effective content (review 2, blocker 1)

The playback composer cannot supply every effective note: it tracks at most `kMaxOpen` = 48 open instances
(`Composer.h`), and selects voices (Q-OUT-2). A probe with 49 simultaneous distinct-pitch notes and a voice allowance of
64 still lost one to overflow. So the groove builder does **not** use the playback composer's output:

- **An extraction mode of the composer** (the same rules — DEC-244 order, occlusion, shortening, edit hides, held-loop,
  fit filtering — but no voice selection) walks the rebuild view **window by window**, emitting each effective note as
  a closed (start, end, pitch, channel, velocity, noteId) record and each non-note event as it is, in a bounded scratch:
  per window at most the window's rows, plus the notes crossing its end carried to the next window (the existing
  carry/checkpoint state). Open notes are bounded by the extraction scratch (sized to the writer's `kMaxOpenNotesPerWriter`
  = 128, not the 48 voices); more open at once than that is a named failure of the build (the old state stays; § 5).
- Records go straight into G's reservation in tick order; the window walk is resumable, so the builder runs in grains.

## 4. Timing

For a note at stored position `t` (absolute in the stored loop), template `T` (§ 6) on grid `g`, amount `a`:

1. Candidate targets: for the grid lines `L` from `floor(t/g)−1` to `floor(t/g)+2`, `target(L) = L·g + T.offset(L)`.
2. Take the target nearest to `t`; a tie goes to the earlier target; distances and targets wrap around the stored loop.
   (Nearest straight line first was wrong: grid 48, 75 % swing, a note at 73 went to 96 instead of the swung 72.)
3. `t' = t + a × (target − t)`; the note's end moves by the same amount (length kept); wrap at the stored loop's end;
   with range scope a note stays inside its range.
4. Collisions: same-pitch notes on one tick merge — the earliest source note's `noteId` survives (pass order, then
   `seq`); a same-pitch overlap ends the earlier note where the later starts. A later edit names the surviving id; if a
   rebuild splits a merge again both reappear and the edit applies to the surviving id only.
5. Notes whose sounding version comes from an edit pass inside the prefix keep their composed position (deliberate
   placement, no drift).

## 5. Building and publishing

- **Reservation first**: G's rows (the extraction's upper bound: the prefix's rows), one run and one manifest, reserved
  before the build starts (writer-class admission, as `LoadJob`). Peak: prefix + current G + new G + other holders
  (playback windows, cursors, saves, other slots).
- **Temporary vs permanent**: if the reservation fails it waits only while rows are held by things that end (a build, a
  save, a pinned window, history states beyond the current ones). If the requirement exceeds what the slot can hold even
  with everything else released (the prefix plus one G over the content quota), it is **cannot fit**: `GROOVE: TOO BIG`,
  the old state unchanged — not a wait. A waiting or running build is **cancelled** by leaving the GROOVE window with a
  long press, by a new setting (restart), or by any undo / load / edit / record on the slot.
- **Generation-qualified publish**: a build carries (slot, base state identity, settings generation); it publishes only
  if the slot's current state is still that identity and its generation is the newest; otherwise its run is released.
- **Switch-over** at the next beat (DEC-292; first built as the next bar line two bars ahead, which took seconds) — heard as a
  musical step.
- **Prefix rule for rebuilds**: the prefix is always a contiguous run of the oldest passes. A rebuild (a setting change,
  or AUTO after an overdub) takes as its prefix **the old prefix plus the passes after G up to the last overdub — never
  G itself** (G is derived; AUTO always rebuilds from original timing). Edit passes after that overdub stay after the
  new G.
- Undo: a GROOVE window visit is one undo step (replace-in-place, as LOOP_EDIT).

## 6. Templates

- **Shape**: a template is `S` steps over `B` bars of one subdivision (`S / B` per bar ∈ {3, 4, 6, 8, 12, 16, 24}: half
  triplets, quarters, quarter triplets, 8ths, 8th triplets, 16ths, 16th triplets), each step an offset from its straight
  position (signed, in 1/96 of a quarter) and an optional velocity scale. Built-ins: `STRAIGHT`, `MPC 16` 54 / 58 / 62 /
  66 / 71 %, `MPC 8` 58 / 66 %, `SP 16` 54 / 58 / 63 / 67 / 71 %, `TRIPLET 8`, `TRIPLET 16`, `JAZZ RIDE`, `BOSSA`,
  `LAID BACK`, `PUSH`. Classic swing: the second 16th (or 8th) of each pair delayed by `(p − 50)/50 × step` (Roger Linn's
  rule; LM-1 50 / 54 / 58 / 60 / 62 / 66 / 70 / 75 %, SP-1200 50 / 54 / 58 / 63 / 67 / 71 %).
- **Embedded in the state** (encoding pinned): one groove-settings row (template kind, grid, amount, scope, AUTO, G's
  position, step count `S`, bars `B`, CRC of the source file or 0 for a built-in) and `⌈S / 7⌉` template rows of 7 steps
  each — per step a signed 12-bit offset and a 4-bit velocity scale (16 bits), 7 × 16 bits = 14 bytes in a row's
  `noteId` / `seq` / `data1` / `data2` / `origin` fields under status `kFitGrooveTemplateStatus`; the label (≤ 16
  characters of the file name) in two more rows. 64 steps: 1 + 10 + 2 = 13 marker rows; 96 steps: 17. A built-in needs
  only the settings row. So a grooved loop plays, saves, loads, copies and rebuilds with the card's folder changed or
  gone.

## 7. The groove library on the card

- **The folder**: the owner unzips their own copy of a pack into `/GROOVES/` (README: *Optional: groove templates*);
  copied with the card in a computer or by `scripts/upload_grooves.py`. The firmware only reads it; nothing from a pack
  is in the repository or the firmware (the shop's terms forbid redistribution).
- **The pack as checked** (*Grooves From Mars*, 2026-11-10): `MIDI/<machine>/<machine> 16 Swing NN.mid`, `16T`, `8T`,
  `4T`; format 0, PPQ 96; note counts 64 (325 files), 96 (25), 48 (24), 32 (13), 24, 12, and a few irregular (49, 63, 97).
  Swing files carry timing only (velocity 127).
- **Import (review 2, blocker 3)** — association by **ordinal**, not by nearest step: strong swing crosses the midpoint
  (36 of 299 64-note swing files put a note nearer the next straight 16th). A file is accepted when its NoteOns, in time
  order, number `S` with `S / 4` (four bars) a supported per-bar count (§ 6) and strictly increase; step `i` is note `i`,
  its offset `t_i − i × (4 bars / S)`, kept as measured per step (no averaging — the machines' own irregularity is the
  feel). Anything else (49, 63, 97 notes; not increasing; another bar count) is listed as **unsupported** with its reason
  and skipped. `Patterns/` folders and the `Ableton/` tree are not imported.
- **Card ownership**: file-system calls run only in the persistence turn, between store-stream grains (`SdCardIo::
  quiesce` first), one bounded step a turn: list one directory entry, or read one 512-byte block of a groove file. The
  listing is built incrementally after boot (names and sizes only, ≤ 1,024 entries); a file's CRC is computed when it is
  loaded, not while listing. Measured on the device before the window promises anything: the worst step time, and the
  time from choosing `MORE…` to the template being ready; the window shows `…` meanwhile.
- **The preloaded ten**: kept parsed in RAM; "most liked" counted — each application raises its template's count (in
  the engine's settings); the ten most used are preloaded. Seeds before any use: `MPC60 16 Swing 54 / 58 / 62 / 66`,
  `MPC3000 16 Swing 58 / 62`, `SP 1200 16 Swing 58 / 63 / 67`, `TR-909 16 Swing 03`; a missing seed is skipped.

## 8. Controls

- **Long press PLAY/STOP** opens the GROOVE window: `TEMPLATE` (built-ins, the preloaded ten, `MORE…` by machine),
  `AMOUNT`, `SCOPE` (FULL / A / B), `AUTO`. F1 amount, F2 template, F3 scope, F4 AUTO; the minimal setup: encoder push =
  next field, turn = value. A long press closes (and cancels a waiting build); it stays open until closed (DEC-292),
  a progress bar in its title while a change is on its way. The window shows `…` while a build runs, `TOO BIG` when it
  cannot fit.

## 9. Slices

0. **Fit lookups after load** (a fix of its own): fit rows name runs by position in the state's run list (or are
   remapped on save and load); covered for save / load, slot copy, get a loop, the library's current-state reader. A
   probe showed a note past a shortened end returning after a load.
1. **Extraction and views**: the composer's extraction mode (§ 3) and the distinct rebuild-view identity with
   view-scoped index preparation (§ 2) — host tests: 49+ simultaneous notes all extracted, closures across windows, a
   deleted note staying deleted in both views.
2. **The groove pass**: timing (§ 4), built-in templates (§ 6), marker rows read by status and carried through
   resize / rotate, save / load, passes after G, the prefix rule including AUTO; native tests including both reviews'
   probes.
3. **Background build**: reservation, temporary vs cannot-fit, cancellation, generation-qualified publish, bar-line
   switch-over (§ 5); device timing measurements.
4. **The GROOVE window** (§ 8).
5. **The library** (§ 7): listing and import under card ownership, ordinal association and rejection reasons
   (host-tested on synthetic MIDI), embedded templates, counts and the preloaded ten, `MORE…`.
6. Undo-depth sizing (separate investigation: states born per minute in real sessions; the cost of a state — manifest,
   rows kept alive, the preallocated store's `kHistoryDepth × (9 + 2 × runs)` bytes a slot file, DEC-253).
