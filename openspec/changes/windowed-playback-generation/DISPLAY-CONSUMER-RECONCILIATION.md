# Reconciliation — task 3.13's display-consumer item against the runtime change's display rework

**Change:** `windowed-playback-generation` · **Task:** **3.13** (*the preview contract — immutable deferred publication*)
· **Date:** 2026-10-06 · **Branch:** `feature/windowed-playback-generation` · **Type:** reconciliation record.

**Nothing is implemented here.** No `src/`/`include/`/`test/` line changed, no build, no fixture, no device action. The
record's job is to answer the one question 3.13 still owes — *who integrates the display consumer of a preview window* —
with the two requirements cited, the **producer**, the **consumer** and the **integration owner** named, and an
**explicit cross-change dependency** recorded instead of a display path this change is not entitled to add.

**3.13's acceptance item stays OPEN on that dependency.** It is *not* deferred to **3.7b** fallback removal: the item is
an acceptance item of 3.13 (and of fixture **2.12** read at the display), while the fallback's removal is a later ladder
stage and not its integration point.

## 1. Why this record exists

3.13's own text names exactly one remaining item: ***"Still owed:** the display consumer of a preview window — 3.13's only
remaining item"* (`tasks.md`, task 3.13). The fact behind it was recorded when the integration half landed:

> *"**The preview window's *consumption* is not exercised** — nothing in production serves a published window yet (the
> merged fallback still serves NOTE_EDIT preview; the rolling lane is behind `PLAYBACK_WINDOW_ROLLING_ENABLED`, default 0).
> This suite asserts what the lane **publishes**."*
> — [`evidence/2026-10-05-s313-preview-integration/EVIDENCE.md`](evidence/2026-10-05-s313-preview-integration/EVIDENCE.md)

The lane is therefore a **write-only** publisher for the display today, and it is written that way on purpose. The
requester's own gate that used to say so is gone: task **3.15d slice 1** removed the demoted-length refusal from
`Track::servicePreviewWindowForNoteEditSession`, so a short loop now requests its whole-loop interval through the same
lane (the interval is `playbackWindowInterval`'s answer for that loop, not a second rule). What remains owned by the
display is the **consumption** side: the merged fallback still serves the display's NOTE_EDIT preview, and this change
does not wire the display to a published window.

The reconciliation question the owner raised: is that consumer this change's to wire? **No.** The display's representation
source belongs to the runtime change's display rework, and a display read added here would be a variant that the rework
immediately replaces. What this change owes is the dependency record, not a display path.

## 2. The two requirements (cited, not restated)

### This change — `specs/windowed-playback-generation/spec.md`

- **`### Requirement: Preview content is a distinct source and is never committed currency`** (`spec.md:86`) —
  *"the preview window is identified as preview rather than as a committed window"*; **"consumers that require committed
  currency do not accept it as committed"** (`#### Scenario: Preview is identified as preview`, `spec.md:97`); and *"the
  withdrawal is reported as applying to committed playback only"* (`#### Scenario: Committed playback does not depend on
  the preview path`, `spec.md:109`).
- **`### Requirement: A published window is immutable and no consumer read straddles a replacement`** (`spec.md:37`) —
  `#### Scenario: Consumers cannot mutate a window` (`spec.md:54`): *"**WHEN** any playback, **display**, persistence or
  edit consumer reads a window **THEN** it receives read-only access"*. **Display is one of the four consumer kinds this
  requirement names**: the persistence consumer was measured by 3.13/I10 (the save), the playback consumer by 3.7a (the
  serving path), and the edit-session consumer is 3.14's undo/redo handoff.

### The runtime change — `runtime-working-set-ownership/specs/runtime-working-set-ownership/spec.md` + `tasks.md`

- **`### Requirement: A compact visual envelope is retained for every slot, with a capped detail window`**
  (`spec.md:219`) — the envelope for every recorded slot, and a **rendered detail window** inside a byte cap with eviction,
  *"reusing the existing resumable display renderer and validity identity"*. Its tasks are the display rework:
  **S4e.1** (per-bar envelope, read by `DisplayManager` through a `Loop` getter), **S4e.2** (the rendered detail window),
  **S4e.3** (invalidation on mutation), **`S4e.4`** (rewrite `TrackManager::prewarmSelectedDisplayVisualCache`, and
  *"record the two caller-less display paths (`resolveDisplayNotesCommitted`, `resolveDisplayNotesLiveCapture`) as either
  reused or deleted **rather than leaving a third variant**"*), **S4e.5** (fixtures), **S4e.6** (a *released* state
  distinct from *clean*), **S4e.7** (cap + eviction owner), **S4e.8**, **S4e.9**, **S4e gates**
  (`runtime-working-set-ownership/tasks.md` § S4e, heading `:2906`, S4e.1 `:2922`, **S4e.4 `:2930`**, gates `:2969`).
- **`### Requirement: Display compose reuses owned frame storage`** (`spec.md:751`) — the composed frame's lifetime and
  scratch reuse on the display's own path.
- **`ARCHITECTURE-REVIEW.md` § P6 — S4e visual tier and S2.8 cap accounting** — the owner line is *"Loop envelope/cache
  validity, **DisplayManager consumption**, existing idle renderer"*, and its gate is *"the relevant existing display
  native suite identified in the packet **before editing**"*.

### The consumer attribution is already the runtime change's own

`S4C0-PUBLICATION-INVENTORY.md:26` attributes the publication unit **by consumer group**, and the display group is named
with its files and what it takes: `DisplayManager/DisplayNoteResolve*.cpp`, `DisplayColdHelpers.cpp`,
`DisplayNoteWindowGather.cpp` (`tryGatherPublishedSnapshotEventsInWindow[WithCapture]`) — *"the window the display
resolves/visual-caches"*. The display is the piano-roll consumer and **does not feed** `playMidiEvents`
([`docs/Authority/Architecture/Display.md`](../../../docs/Authority/Architecture/Display.md): *"Representation + Display
interval → Visible pixels"*, owner `DisplayManager`). So the display is a first-class consumer of the window authority
(DEC-108) that the runtime change is already reworking.

## 3. Producer, consumer, integration owner

| Role | Who | Where it stands today |
|---|---|---|
| **Producer** (this change) | the preview window publication — the window authority owner (`Track::stepPlaybackViewMaintenance`) publishing through `Track::requestPreviewWindow` (`include/Track.h:848`), carrying **`PlaybackWindowOrigin::Preview` in both identities and in equality** (`include/RuntimePlaybackView.h:157,170,204`), cancelled by name through `Track::cancelPreviewWindowForSlot` (`include/Track.h:855`) with the named `PreviewCancelOutcome`, and read at the **served** window through `Track::playbackWindowEventAtForSlot` (`include/Track.h:881`), `Track::servedPlaybackWindowIsPreviewForSlot` (`:857`) and `Track::previewWindowHandoffOwedForSlot` (`:859`) | landed; production callers delivered by 3.7a/I3; the **read** (`playbackWindowEventAtForSlot`) states *"Nothing in production consumes it yet"* — the display is what would give it one |
| **Consumer** | the **display** (`DisplayManager`) — the piano-roll consumer named by the spec's consumer list and by `S4C0`. Its NOTE_EDIT representation today comes from `EditManager::projectedNoteEditDisplayNotes` (`src/EditManager/NoteEditDisplayProjection.cpp:29` ← `noteEditSessionProjectionEvents()`), and the *playback* NOTE_EDIT preview is the merged fallback that reads `editManager.sessionMidiEvents()` (`src/Track/TrackPlaybackWindowBuild.cpp:221,274`) — the path the contract's own gap row calls out: *"Removing the live fallback leaves NOTE_EDIT preview unspecified"* (`design.md:253`) | no preview-window read exists on the display path; the fallback still serves NOTE_EDIT preview |
| **Integration owner** | **`runtime-working-set-ownership` S4e — the visual tier** (the runtime change's display rework): the envelope + capped detail window (S4e.1/S4e.2/S4e.3/S4e.6/S4e.7) and the **read-path consolidation S4e.4**, under `ARCHITECTURE-REVIEW.md` P6, whose owner line is `DisplayManager` consumption | tasks open; the display suite and cap recipe are its own gate (`recipe R`, `S7.4d` visual/cap evidence) |

**Why the integration belongs to the runtime change, stated rather than assumed.** The display's *representation* is the
runtime change's to change (the envelope and the capped detail window are its requirements, and `DisplayManager`
consumption is written into its P6 ownership), and **S4e.4 already forbids a third variant of the display's read paths**.
A preview-window read wired by this change would be exactly such a variant — it would land on the display compose/gather
path that S4e.1/S4e.2/S4e.4 rewrite. So the correct artefact here is a dependency, and the correct owner there is the
slice that owns the rework.

**What this change supplies to that owner (cited, not re-decided):** the preview source contract and its identity
(`origin == Preview`, part of equality, so *"a consumer that requires committed currency refuses it without inspecting
anything else"* — `include/Track.h:846-847`), the session lifecycle (source freezing as a revision, cancellation by name,
the exit handoff as a **boundary**), the committed-currency predicate the refusal can be built on
(`Track::committedPlaybackWindowServesSlot`, `include/Track.h:822`; 3.7a/I3), and the served-window read
(`playbackWindowEventAtForSlot`) in emission order. **What this change does not decide, and must not:** the display's
representation shape (`DisplayNote` lists, envelope, capped detail window), its read-path count, and its cache lifecycle —
S4e's, per its own requirement and its own task text.

## 4. What 3.13 must NOT do (the forbidden temporary path)

- **Do not wire the display to the playback window lane now**, and do not add a display-shaped read of the window
  (`DisplayManager::*` or the note-edit projection) as a stopgap. That is the *"third variant"* `S4e.4` forbids, and the
  path `S4e.1`/`S4e.2` rework — a temporary display path the rework immediately replaces.
- **Do not defer the item to 3.7b** fallback removal, and do not fold it into "the serving path's remaining clauses"
  vaguely. It is an acceptance item with an owner and a dependency, both named above.
- **Do not re-open the display's representation decision here.** A short loop's preview window now exists (3.15d slice
  1), but *how the display renders it* — including whether a whole-loop interval is the rendered detail shape — is the
  display rework's question (S4e.2/S4e.4), not the lane's.

## 5. What stays open, and the explicit dependency (both directions)

| Item | State | Dependency |
|---|---|---|
| **3.13 — the display consumer of a preview window** | **OPEN** | `runtime-working-set-ownership` **S4e** (S4e.1 envelope, S4e.2 rendered detail window, **S4e.4** read-path consolidation); the display suite and the cap recipe are its gate. Acceptance criterion unchanged: the display obtains the NOTE_EDIT preview through the **preview window's** identity and discipline — it is identified as preview rather than as committed, no mixed or stale preview is drawn, and it is never accepted as committed currency (fixture 2.12's clauses, read at the display) |
| **RWO S4e — the display rework** | tasks open | must honour this change's producer contract: a window's origin is read from the identity (preview ≠ committed in equality), a window read is read-only, the preview lifecycle's cancellation and exit handoff decide what is served, and the live mutable fallback is **not** the preview's serving source under the rework |

The reciprocal note is recorded in `runtime-working-set-ownership/tasks.md` § S4e, so the integration owner reads the
dependency where the work sits.

## 6. Findings recorded, not smoothed over

1. **`include/Track.h` comment interleaving (pre-existing, doc-only).** The doc comment for
   `playbackWindowEventAtForSlot` (`:868-872`) is separated from its own last line (`:880`, *"actually produced instead of
   inferring it from the identity"*) by the `servicePreviewWindowForNoteEditSession` comment block (`:873-878`) and that
   declaration (`:879`), so a reader can attribute the trailing line to the preview function. The declarations and the
   meaning are correct, so this is a **formatting defect**, not a behaviour defect. It is **recorded
   here rather than fixed in this increment**, because this increment is documentation-only and the change's own evidence
   convention reports `git diff -- src include` empty for a documentation slice; fixing it (reordering the two comment
   blocks) belongs with the next increment that touches the header.
2. **This record adds no consumer.** 3.13's display-consumer item is unchanged in substance; what changes is that it now
   has an **owner** and an **explicit cross-change dependency** instead of an open-ended "still owed".

## 7. Verification of this record

| Check | Result |
|---|---|
| `git diff --stat -- src include test` | **empty** — no production or test line changed by this increment |
| `openspec validate windowed-playback-generation --strict` | **valid** |
| `openspec validate runtime-working-set-ownership --strict` | **valid** (the reciprocal note is text in an existing task section) |
| `git diff --check` | clean |
| Build / device | **NOT RUN** — nothing device-visible is claimed; no upload |
