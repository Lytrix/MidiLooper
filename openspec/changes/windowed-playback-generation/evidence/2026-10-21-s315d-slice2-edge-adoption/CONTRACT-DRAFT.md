# 3.15d slice 2 — clock-edge `Preview` adoption during NOTE_EDIT (contract draft)

**Status: draft for owner decision. This is not an authorization, not a G2 packet and not evidence.** No production
line, fixture or budget is changed by this file. Slice 1 is landed
([`../2026-10-21-s315d-preview-interval/EXECUTION.md`](../2026-10-21-s315d-preview-interval/EXECUTION.md)); slice 2 is
the behaviour change named in its slice map and remains undeclared in `CURRENT_WORK.md`.

## What slice 2 has to change

The preview lane is a **write-only publisher** today: `Track::requestPreviewWindow` builds and publishes a
`Preview`-origin window into the slot's view set, and nothing consumes it. Two independent refusals keep it that way:

1. `resolveQualificationPlaybackSourceForTurn` is only reached with `fixedViewEligible = isActive && isPlaying() &&
   !editManager.isNoteEditActive()` (`TrackPlaybackHotPath.cpp`), so during NOTE_EDIT no view can be selected; and
2. `resolveRollingPlaybackWindow` refuses outright when `editManager.isNoteEditActive()`, so the committed rolling
   frame cannot serve either.

The merged NOTE_EDIT fallback (`ensurePlaybackMergedMidiEventsBuilt` with `noteEditPreview = true`) is therefore the only
audible route during a session. Slice 2 replaces (2) — and only for `Preview`-origin identities — with "serve a preview
window when one covers the playhead; otherwise the merged fallback serves".

## Contract, as analysed

- **Adoption seam.** A prepared `Preview` view is adopted at its **own `coverage.startPhase`** (`adoptPreparedPlaybackWindow`,
  the same handoff the committed lane uses), never mid-coverage, so the cursor starts at the interval's first event.
- **Currency.** Only views whose `source.origin == Preview` **and** whose `source.playbackRevision` equals
  `runtime.previewSourceRevision` may serve; a committed view may never be adopted through this branch, and a preview
  view may never be adopted while the lane has yielded (`previewHandoffOwed`).
- **Cadence.** The preview interval is two bars with its start one bar behind the playhead
  (`playbackWindowInterval`), so the requester must ask **one boundary ahead**: at playhead `p`, request
  `playbackWindowInterval(nextBoundary(p) + TICKS_PER_BAR)`, which yields coverage `[nextBoundary, nextBoundary + 2 bars)`
  and a handoff lead of up to one bar. The view then covers exactly the two bars after its start, and the next request
  (made one bar into it) covers the following two. Without this, adoption at `startPhase` could never happen for the
  interval requested at the playhead.
- **Compatibility fallback.** When no preview view covers the phase (lane busy, demand refused, session just opened), the
  merged fallback serves the turn exactly as today. This is a **route mix within a traversal**, which is the hazard
  below.

## Hazards this slice must answer before it lands

1. **Cursor handoff at the merged seam.** `loop.nextEventIndex` is the shared cursor. A view walk leaves it at the view's
   end; the merged store is a different array, so resuming merged with a stale index either skips the rest of the
   traversal or re-walks from zero every turn. The committed lane answers this at expiry by resetting the index once
   ("Fallback's crossing predicate excludes the already-served interval"), and the preview route needs the same
   one-shot answer with its own route memory. This needs a state bit (size pin) or an equivalent no-allocation answer,
   and it is the slice's main correctness risk.
2. **Closures for removed/replaced identities.** A preview composition omits a deleted identity's Off. If that identity
   was sounding from the previous window, the seam must close it — the committed lane's `ActiveNoteLedger` /
   `LedgerBoundarySummary` contract is the existing owner, and `requestPreviewWindow` currently passes an empty
   `EmissionCursor` and `LedgerBoundarySummary`. Whether an empty summary means "unconstrained" or "must be empty" has
   to be settled before adoption is enabled, not discovered on device.
3. **No emission harness exists.** No native suite drives `playCommittedLoopMidi`; `test_windowed_preview_integration`
   asserts *published* windows and `test_runtime_working_set_ownership` drives the rolling frame, not the MIDI edge. The
   design's gate ("adoption RED when the branch is suppressed, ledger continuity, stale-preview asymmetry") needs a
   harness that captures sent MIDI across a boundary, including the merged↔preview seam. **This harness is the slice's
   first task and its prerequisite**, not an afterthought: without it, "adoption works" is only "the resolver returned a
   pointer".
4. **ITCM.** The edge branch is a new test on a hot path with an 80 B allowance; the resolver lives in flash, so the
   branch must be placed there (the same reason the committed selector is `FLASHMEM`), and the RAM1 pins must be
   re-measured, not assumed.
5. **Audible route.** On the qualification image this makes the preview window the NOTE_EDIT emitter; the default image
   is unchanged because the selector is compiled under `PLAYBACK_WINDOW_ROLLING_ENABLED`. Route parity is **3.15f's**,
   and the device leg recorded for slice 1 shows why it needs a decidable interaction first.

## Proposed oracle sketch (not fixed)

- **S1 adoption.** NOTE_EDIT open, a preview published for the next boundary, edge turn at that boundary: the turn emits
  the preview's events and not the merged projection's; suppress the branch → RED.
- **S2 seam.** The preview covers bars 1–2 and the merged fallback serves bar 3: every On has exactly one Off, no
  duplicate On, no missed Off, and the ledger at the seam matches.
- **S3 stale asymmetry.** A view prepared for revision N while the lane's revision is N+1 is never adopted; the
  previously served view keeps serving until the new one is ready (retention), and restoring the stale-serving behaviour
  fails.
- **S4 cadence.** At a boundary the requester asks for the interval whose start is the next boundary (not the playhead's
  own), and the admitted lead is ≤ one bar.
- **S5 currency.** A committed view is never served through the preview branch, and `previewHandoffOwed` blocks adoption.

## Prerequisites, in order

1. Native emission harness for the clock edge (test-only; no firmware change).
2. A scope declaration for slice 2 in `CURRENT_WORK.md` naming this contract, plus a G2 packet carrying the oracle above.
3. The cursor/ledger answer (hazards 1–2) fixed and RED-proved **before** the edge branch is enabled.
4. RAM1 re-measure on both builds, then the native gate; device parity stays 3.15f.
