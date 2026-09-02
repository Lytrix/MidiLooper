# Overdub occupy — prepared source view missing committed notes

**Status:** Occupy-paint path closed — `not_finished` + visual fallback; leftover LCR is not the owner. Successor: [DEC-052](../DECISION_LOG.md#dec-052--occupy-consumes-the-draw-source-predicate) / [`overdub_occupy_snapshot_source_refinement.md`](overdub_occupy_snapshot_source_refinement.md)  
**Date:** 2026-09-02  
**Kind:** bugfix  
**Evidence:** [`113651`](../../captures/session_20260902_113651.log) (same chain in [`113328`](../../captures/session_20260902_113328.log))  
**Does not authorize:** OVERDUBBING reconstruct, Stage 4, Gate 5A order change, ledger-only occupy when `a=0`, changing `resolveOccupancyAtNoteOn` / `collectOverdubSourceHoldParticipantIds` / ledger membership, flipping `overdubPassWrapPairing` before the pin attributes the miss

---

## Debugging boundary

```
OD1 stop adopt_partial visual=22 / DISP 22
    ← trust (paint)
seal_fx_publish_prepared_overdub ok
    ← pin: checkpoint span set
tryCopyPreparedSpansToDisplayNotes
    ← pin: copied span set
OD2 open overdubSourceViewNotes_  (lcr,src from=prepared notes=8)
    ← occupy source
resolveOccupancyAtNoteOn selected.empty() when a=0
    ← downstream consequence
```

Occupy candidate selection is downstream of the prepared-source defect. Do **not** teach resolve to Shorten/Hide from ledger `n=1` alone. That would make ledger membership authoritative for paint geometry and is a refused architectural regression.

Do **not** choose a firmware fix until the pin attributes the miss to one owner (A / B / C below).

---

## `a=0` (precise)

`#CAP DIAG,lcr,part` fields:

| Field | Owner | Meaning |
|---|---|---|
| `n=` | `collectOverdubNoteOnParticipantIds` | open ledger identities on this pitch |
| `a=` | `collectOverdubSourceHoldParticipantIds` | source-view covering identities (present-at-hold) |

On this capture, `n=1 a=0` means: **a ledger candidate exists, but no source-view participant geometry was selected.**

It does **not** mean “there is no overlap” and does **not** mean “the note is inactive.” The capture shows the opposite: a relevant ledger candidate is present; the prepared source representation does not supply its geometry.

`eq=1` on those rows is ledger vs prepared-present agreement on emptiness of `a`/`b`, not proof that occupy should no-op.

---

## Problem

[`113651`](../../captures/session_20260902_113651.log): after OD1, visual has **22** notes (`VCACHE,adopt_partial,notes,22`, `DISP,…,3072,22,…`). OD2 opens source view `from=prepared,notes=8`. `resolveOccupancyAtNoteOn` only writes Shorten/Hide for source-view rows it can select. When `a=0` and the ledger id is not in those rows, it returns without a transform. Committed OD1 notes stay full length and overlap the incoming occupy notes.

`8 vs 22` is a **count observation**, not the invariant. The same `noteId` can have multiple geometries around wrap (`19` below). Cardinality alone can look “satisfied” while an identity or a required wrap span is missing.

### Proven occupy misses (OD2 `lcr,part` / `lcr,nopart`)

| Pitch | Merged NoteOns | Source-view rows at miss | Holds |
|---|---|---|---|
| 48 | 4 (`112,846,1603,2372`) | **0** | every lap-1 48 hold `n=0/1 a=0` |
| 52 | 4 (`497,1232,1986,2776`) | wrap remnants only: `19@2713→21`, `19@2776→84`, `14@2774→3071` | `hs=826/1402/2214` `n=1 a=0`; wrap `hs=3001` `a=2` |
| 56 | 4 (`112,846,1603,2372`) | 3 linear (`846→1013`, `1618→1795`, `2388→2561`); missing On@112 | `hs=553/826` `a=0`; one hit `hs=2404` `as=2388` (Shorten, not same-start Hide) |

OD1 stop `overlap_hold shorten=0 hide=0` (correct). OD2 stop `shorten=2 hide=4`: the Hide/Shorten rows that fire bind to **lap-1 occupy notes** (`as=826→1019`, `1019→1402`, …), not the missing OD1 source phrase.

### Identity coverage vs geometric coverage

`noteId=19` appears twice in the source-view dump:

```
19 @ 2713 → 21   wrap
19 @ 2776 → 84   wrap
```

The pin must establish these independently:

1. **Identity coverage** — every committed overdub-pass NoteOn identity required by `playback.mergedEvents` for the same revision is represented in the prepared source view.
2. **Geometry coverage** — that representation contains the spans occupy selection needs, including wrap/pair geometry where applicable.

Do not treat “8 logical identities present” as success if their geometric expansion is incomplete.

### 761-tick notes

This capture’s occupy dumps do **not** contain a 761-tick span. Dumped long leftovers: wrap `19@2713→21` (380), unpaired tail `14@2774→3071` (297), occupy leftovers `26@1402→1829` (427) and `29@2214→2616` (402). After undo/redo, `VCACHE,slice_clean` jumps `31→22→78→86→31` with `SESSION_CAPTURE_SNAPSHOT_IDLE` off (no per-note `DNTE`).

**No geometry in the relevant dump, no new owner hypothesis.** Do not open a second RC from 761.

---

## Why resolve does not run (downstream)

`Loop::resolveOccupancyAtNoteOn` selects from `overdubSourceViewNotes_` via ledger ids, then fills from `collectOverdubSourceHoldParticipantIds`. Empty selection returns. `Track::snapshotOverlapHoldCandidates` can put a ledger id in `overlapNoteIds` (`n=1`) while source view has no covering row (`a=0`). Geometry is not written.

Observed control flow (not a chosen fix): `rebuildOverdubSourceSpanCache` accepts the prepared set as authoritative when `tryCopyPreparedSpansToDisplayNotes` succeeds, so the visual/passes fallback paths are not consulted.

That fact does **not** decide the fix. After the pin, the implementation may be either:

* make the prepared representation complete, **or**
* reject an incomplete prepared representation so an existing fallback supplies the adopted cache.

Do not decide which yet. Do not add a silent fallback heuristic.

`publishPreparedOverdubPass` reconstructs the sealed overdub pass with `reconstructDisplayNotes(events, loopLen, false, false)` (`finishOpenNotes=false`, `overdubPassWrapPairing` default false). Visual wrap append uses `overdubPassWrapPairing=true`. That call-site difference is a **hypothesis for the pin**, not a license to flip the flag, and not a license to change occupy selection.

---

## Architecture checkpoint (pin only)

| Question | Answer |
|----------|--------|
| Ownership change? | **NO** for instrumentation and the native fixture. Choosing C (prepared is intentionally partial) is a design session, not a silent fallback. |
| State transition change? | **NO** |
| Unbounded interval owner? | **NO** if dumps stay on the existing publish / copy / source-view rebuild |

Stop and reopen a design session if the pin requires occupy to Shorten/Hide from ledger ids that are absent from source view.

**Protected:** do not change `resolveOccupancyAtNoteOn`, `collectOverdubSourceHoldParticipantIds`, the ledger, or the meaning of `a=0` on this evidence. Ledger membership is overlap bookkeeping. Prepared source-view geometry is occupy paint input. Do not conflate them.

---

## Invariant

When `preparedWindowReady` succeeds at overdub open, the prepared source view must represent every committed overdub-pass NoteOn identity required by `playback.mergedEvents` for the same revision, with all required source-view geometry (including wrap geometry).

---

## Pin instrumentation (this slice)

SESSION_CAPTURE dumps (no occupy behavior change):

| Boundary | Line |
|---|---|
| pass NoteOns | `DIAG,lcr,srcon,at=publish,id=,p=,t=` + `srcspan,at=ons,count=` |
| reconstruct at publish | `DIAG,lcr,srcspan,at=publish,id=,p=,s=,e=,g=` |
| checkpoint after append | `DIAG,lcr,srcspan,at=checkpoint,...` |
| copy (+ `srcdrop` exclusions) | `DIAG,lcr,srcspan,at=copy,...` |
| source view after collapse/retain | `DIAG,lcr,srcspan,at=view,...` |

`g=` is `wrap` when `end < start`, else `linear`. Native fixture: `test_reconstruct_113651_od1_identity_coverage` — expected OD1 NoteIds ⊆ reconstruct identities; wrap-pairing flag is diagnostic.

Early-return dumps ([`132720`](../../captures/session_20260902_132720.log) had Track `publish_prepared_overdub,ok` with no `srcspan at=publish`, and OD2 open `from=visual` with no `at=copy`):

`DIAG,lcr,srcskip,at=publish|copy,reason=…,fin=,kept=,pass=,rev=,prep=,live=,sess=,spans=`

Publish reasons: `not_finished` / `not_kept` / `invalid_pass` / `length_zero` / `length_mismatch` / `already_indexed` / `missing_entry`.  
Copy reasons: `not_finished` / `not_kept` / `rev_mismatch` / `empty_spans` / `checkpoint_len0` / `length_mismatch` / `empty_window_ids`.

Recapture `overdub_occupy_paint` on `teensy41-capture-serial`. Attribute A/B/C from the three boundaries **and** these skip reasons. Do not flip `overdubPassWrapPairing` from this slice.

### Recapture [`134815`](../../captures/session_20260902_134815.log)

Clean `teensy41-capture-serial` RAM1 locals **6848** (PSTR dump literals + one shared empty overlap set). First post-upload run [`134710`](../../captures/session_20260902_134710.log) aborted: empty record stop went `STOPPED_RECORDING → STOPPED` (no PLAYING).

All four `srcskip` rows on [`134815`](../../captures/session_20260902_134815.log): `reason=not_finished`, `fin=0`, `kept=0`, `prep=0`, `spans=0`.

| When | Skip | Next line |
|---|---|---|
| OD1 open | `at=copy` | `from=visual,notes=4,prep=0` |
| OD1 stop | `at=publish` `pass=3` `rev=3` | Track `seal_fx_publish_prepared_overdub,…,ok` |
| OD2 open | `at=copy` `rev=3` | `from=visual,notes=19,prep=0` |
| OD2 stop | `at=publish` `pass=26` `rev=8` | Track `…,ok` |

No `srcspan at=publish` / `at=checkpoint` / `at=copy`. Reconstruct and copy never ran. Track `ok` means the matching `OverdubPass` was found and `publishPreparedOverdubPass` was called (`published=true` in `finalizeCommitSideEffects`), not that `sDeviceGateFinished` was true or that a checkpoint was written.

A is not choosable on this recipe (reconstruct did not run). B is not choosable (copy returned at `not_finished`). C as written (intentionally partial prepared accepted as ready) did not occur: `preparedWindowReady` stayed false and open used visual. Occupy FAIL remains `shorten=1` / OD2 `snap=1`; OD1 `shorten=0 hide=0`. Do not patch occupy from this.

`fin=0` on this recipe is the STOPPED-only device gate, not a reconstruct/copy miss.

`Track::processDeferredContentResolutionDeviceGate` runs only when `!isPlaying() && !isRecording() && !isOverdubbing() && !isStoppedRecording()` ([`loop_content_resolution_stage9_handoff.md`](loop_content_resolution_stage9_handoff.md)). `maybeQueueContentResolutionDeviceGate` also requires `hasCommittedPasses()` and `!visualCacheDirty`.

[`134815`](../../captures/session_20260902_134815.log): the only STOPPED window is after the empty first record (`seal skipped_empty`). The seed record goes `STOPPED_RECORDING → PLAYING` and stays PLAYING/OVERDUBBING until transport stop. `srcskip` `sess=0 prep=0 spans=0` — begin never ran. `publishPreparedOverdubPass` / `tryCopyPreparedSpansToDisplayNotes` therefore return `not_finished`. Open uses visual (4 then 19).

[`113651`](../../captures/session_20260902_113651.log) is a different path: OD1 open already has `prep=3072` (`from=prior,notes=0`); OD2 open is `from=prepared,notes=8`. That capture can restamp a finished session. Occupy-paint HITL cannot.

Do not run LCR slices while PLAYING (rejected as option B on that handoff). Do not flip `overdubPassWrapPairing`. The occupy-paint FAIL (`shorten=1`, visual 19) is the visual fallback path, not the `from=prepared notes=8` path. Occupy-paint source selection is closed here as `not_finished` + visual; committed occupy now follows the paint draw-source predicate ([DEC-052](../DECISION_LOG.md#dec-052--occupy-consumes-the-draw-source-predicate)). [`113651`](../../captures/session_20260902_113651.log) prepared-8 remains leftover LCR on a restamped finished session, not occupy-paint owner.

---

## Next pin

Instrument **three boundaries**. Each dump row is `noteId / pitch / start / end / wrap-or-linear` (not count + pitch only).

```
publishPreparedOverdubPass
    ↓  checkpoint span set
tryCopyPreparedSpansToDisplayNotes
    ↓  copied span set (log exclusions: inactive / inWindow / …)
overdubSourceViewNotes_
```

| Finding | Likely owner |
|---|---|
| checkpoint already missing OD1 identities | `publishPreparedOverdubPass` reconstruction |
| checkpoint complete, copy drops identities | `tryCopyPreparedSpansToDisplayNotes` / window filtering |
| source view complete before occupy but occupy still misses | reopen toward selection (not authorized on current evidence) |

A third checkpoint failure mode is in-scope: **the checkpoint itself contains the wrong representation** (identities present, wrap/linear geometry wrong — e.g. duplicate `19` spans). That is still the publish/reconstruct owner, not occupy selection.

### Native fixture

From [`113651`](../../captures/session_20260902_113651.log) OD1 `MI,U` 48/52/56/60 (16 on/off pairs), build the **expected committed OD1 NoteId set** from the same-revision playback merged NoteOns (or the pass events’ stamped ids). Do not assert “8 versus 22” or compare reconstruct vectors by size.

Required assertions:

```
expected committed OD1 NoteIds
    ⊆
prepared source-view NoteIds
```

plus geometry assertions for the wrap cases (identity present is not enough if wrap/pair geometry is missing).

Then compare, as a diagnostic — not as the pass criterion:

```
reconstruct(..., finishOpenNotes=false, overdubPassWrapPairing=false)
reconstruct(..., finishOpenNotes=false, overdubPassWrapPairing=true)
```

Question: **which reconstruction contains the identities and geometry the prepared source-view contract requires?**

Do **not** conclude “therefore change `false` to `true`” from a larger vector. `overdubPassWrapPairing=true` is only a candidate fix if it supplies the required identities/geometry **and** its semantics are appropriate for this prepared checkpoint (one overdub pass’s events — never record or merged record+overdub). Unrelated reconstruct call sites stay unchanged until that is shown.

Do not re-enable OVERDUBBING reconstruct or Stage 4 to paper over the gap.

---

## Pin acceptance — then choose one

The pin succeeds only when all three boundaries are explained:

1. What identities and geometries `publishPreparedOverdubPass` produces.
2. What identities and geometries remain after `tryCopyPreparedSpansToDisplayNotes`.
3. Why the resulting `overdubSourceViewNotes_` does or does not satisfy the prepared-source invariant.

Then choose **one**:

| Choice | Meaning |
|---|---|
| **A** | Fix reconstruction flags in `publishPreparedOverdubPass` (only after wrap-pairing semantics are shown appropriate for this checkpoint). |
| **B** | Fix prepared-span copy / filtering in `tryCopyPreparedSpansToDisplayNotes`. |
| **C** | Prove the prepared representation is **intentionally** partial and therefore must not be accepted as `preparedWindowReady`. Design session — do not silently add a fallback heuristic. |

If the `true` wrap-pairing argument produces the missing wrap/identity coverage, A becomes a plausible minimal fix. It is still not authorized until the pin records that the flag is valid for this prepared checkpoint and does not alter unrelated reconstruction cases.

If publish is complete and copy yields 8, A is the wrong fix; move to B.
