# Overdub wrap source-view display drop and remaining n>a

**Status:** **FROZEN** — closed 2026-08-19. Lane **N closed** (diagnostic). Lane **D** D2-D **shipped** — revision-freshness guard in `committedPlaybackNoteOnIdentityValid` + wrap rebuild telemetry. HITL PASS [`174246`](../../captures/session_20260819_174246.log): 12 wraps `from=span`, DISP monotonic, `identity_invalid` **0**, ledger **95/95** `eq=1`. Symptom pin [`170838`](../../captures/session_20260819_170838.log) (`prep=0`, `from=win`) **parked** — Layer 1 gate readiness only. Parent occupy RC [`overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md`](overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md) **FROZEN** same session.  
**Date:** 2026-08-19  
**Kind:** bugfix  
**Pins:** [`161349`](../../captures/session_20260819_161349.log) — occupy + prepared-span path (`from=span`); [`170838`](../../captures/session_20260819_170838.log) — pre-fix symptom (`prep=0`, `from=win`); [`174246`](../../captures/session_20260819_174246.log) — post-fix verification (`from=span`, display + ledger clean)  
**Parent (Gate 5A):** [`overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md`](overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md)  
**Regression slice (ledger inconsistency):** [`overdub_occupy_missing_open_identity_bugfix.md`](overdub_occupy_missing_open_identity_bugfix.md) — last firmware **`9e1b2b7`**; parent [`overdub_occupy_leftover_identity_bugfix.md`](overdub_occupy_leftover_identity_bugfix.md); committed-only merged **`33b48ca`**  
**Does not reopen:** Gate 5B skip/filter Length by `targetNoteId`; occupy collect patch; leftover identity (`111819` PASS); RC12 (paint `visualCache` during overdub); `fill_buffer(0)` as root cause; one commit mixing lane D and lane N

```text
5A extra covering a>n MET — parent FROZEN
5B Length 6073 551 — withdrawn (no firmware)
remaining n>a — lane N closed (diagnostic)
wrap drops committed notes — lane D D2-D shipped; HITL 174246 PASS
Layer 1 prep=0 / from=win — parked (170838 only)
```

**Final validation:** Native **1384/1384**. HITL [`174246`](../../captures/session_20260819_174246.log). Fix owner: `committedPlaybackNoteOnIdentityValid` revision guard + wrap `srcskip,stale_identity_stream` / `srcdrop` diagnostics in `rebuildOverdubSourceView`.

Lanes share wrap orchestration in `Track::commitOverdubWrapAtSessionStart` but are **not** proven to be one defect.

---

## Invariants (by lane)

| Lane | One sentence |
|------|----------------|
| **D** (RC12 membership) | After overdub wrap, RC12 paint must equal the **resolved source-view result** for all active committed passes: identities **intentionally removed** by sealed Hide/Shorten may be absent; an **otherwise-visible** prior record/overdub identity must **not** disappear merely because the source view was rebuilt. |
| **D2-D** (gate readiness) | Every overdub wrap that rebuilds `overdubSourceViewNotes_` must have an **authoritative committed representation** from which the complete currently-visible source view can be resolved. **`from=win` reconstruction must not substitute for committed content** merely because the device gate is unfinished. **Investigation first:** establish whether the `deviceGateFinished` guard on `publishPreparedOverdubPass` is deliberate, migration-incomplete, or a regression — before redesigning the gate contract. |
| **N** | **`n>a` is not an invariant violation.** Diagnostic only; `n=2 a=1` is a permanent counterexample to `n == a`. |

**Architectural principle (lane D):** a source-view rebuild may change geometry/membership according to committed resolver state, but it must not silently lose unrelated committed content.

```text
resolver → prepared/resolved representation → source-view cache → live display
```

`visualCache` is **not** the overdub paint owner (RC12).

---

## Architecture checkpoint (before any patch)

| Lane | Ownership change? | Transition change? | Notes |
|------|-------------------|---------------------|-------|
| **D** (span copy retains otherwise-visible ids per resolved state) | NO | NO | D2-B path only |
| **D** (move overdub paint to `visualCache`) | YES — design session | NO | |
| **D2-D** (investigate `publishPreparedOverdubPass` guard) | TBD after Q1–Q3 | TBD | Layer 1 only — **parked** ([`170838`](../../captures/session_20260819_170838.log)) |
| **D2-D** (stale merged-stream identity prune) | NO | NO | **Shipped** — revision guard; HITL [`174246`](../../captures/session_20260819_174246.log) |
| **N** (clock untagged Off LIFO) | NO | NO | |
| **N** (occupy writes ledger) | YES — design session | YES | |

---

## Debugging boundary

```text
… → Gate 5A equal-tick apply pairing ← shipped; extra covering a>n = 0
 → Gate 5B Length 6073 551 provenance ← blocked
 → lane N: n>a occupy diagnostic ← closed
 → lane D: span-copy filter (D2-B) ← not reached in 170838
 → lane D: stale-merged identity prune (D2-D) ← shipped (174246)
 → lane D: unprepared rebuild prep=0 / from=win ← parked (170838 symptom only)
```

**Regression attribution (closed on D2-D):** the user-visible wrap wipe **surfaced during the ledger-inconsistency slice** ending **`9e1b2b7`**, not from a new `deviceGateFinished` guard (that guard dates to **`8bda25b`**, 2026-08-15). **Verdict:** Layer 2 — `retainValidOverdubSourceViewIdentities` ran against a **stale** `committedPlaybackMergedForIdentity_` (`builtFromRevision != playbackRevision`) and incorrectly pruned otherwise-visible ids. **Fix:** skip identity filtering when the bound merged stream is not revision-fresh (`committedPlaybackNoteOnIdentityValid` returns true; `srcskip,reason=stale_identity_stream` on wrap rebuild).

**Two-layer model (disambiguated):**

```text
Layer 1 (readiness — parked):  !deviceGateFinished → publish no-op → from=win (170838)
Layer 2 (shipped):             9e1b2b7 identity filter on stale merged stream → prune
                               Fix: revision-freshness guard (174246 PASS)
```

`161349` / `174246` work when gate completed before overdub (`from=span`). `170838` additionally shows Layer 1 (`prep=0`, `from=win`) — a separate gate-readiness defect, not fixed by the Layer 2 patch.

---

## Pin A — [`161349`](../../captures/session_20260819_161349.log) (occupy + prepared span path)

### Shared

- 1-bar loop (`768` ticks). 13 wrap `lcr,src` rebuilds; all `why=wrap,from=span`.
- Wrap `tot=` 21–50 ms (`lcr,src`).
- `DISP OVERDUBBING` frame notes **34–49** (never 0). `DFRAME` min **1** during overdub window.
- Post-stop `DISP STOPPED` frame **67** vs last overdub `DISP` **43** — stop restores fuller committed paint via `resolveDisplayNotesCommitted` / `visualCache`, not RC12 source view.

### Lane N — occupy `n>a` (closed)

| Metric | Value |
|--------|-------|
| Mismatches | **32** (all `n>a`) |
| `led==n` | **32/32** |
| `cu` | **0** on all 32 |
| Extra covering `a>n` | **0** |

| Pattern | Count |
|---------|------:|
| `n=1 a=0` | 15 |
| `n=2 a=1` | 15 |
| `n=2 a=0` | 2 |

**N1 classification**

| Class | Count | Meaning |
|-------|------:|---------|
| `lst == hs` | 10 | Ledger On applied at same phase as hold; no source-view span covers hold |
| `lst < hs` | 4 | Ledger On before hold; covering span ended or absent |
| `lst > hs` (wrap gap) | **1** | L3096 only — `a=0` correct; ledger open in wrap-pair gap |
| `n=2 a=1` (nested) | 15 | DEC-042-style: two open Entries; one DisplayNote covers hold |
| `n=2 a=0` | 2 | Two open Entries; neither span covers hold |

**N2 — L3096 (wrap gap, pinned)**

```
hs=648  n=1  a=0  lid=6280  lst=744  ltick=648  cu=0
mmspan: 6269 120–216 | 6280 744–767 | 6280 0–168
mmevt:  On@744 id=6280 (untagged Off@168 at ph=168)
```

- `displayNotePresentAtHold` at 648: **false** for all three spans (gap between exclusive end 168 and inclusive start 744). **`a=0` is correct.**
- Ledger `n=1`: Entry `6280` with `startTick=744`. Clock `ltick=648` is **after** wrap-head On@744 in loop order (`…744 → 767 → 0 → … → 648`). Note still open — no committed Off yet.
- Untagged Off@168 does **not** close `6280` — `6280` was not open at tick 168.
- **Not** leftover identity (`On@744` exists in merged). **Not** occupy-collect bug. **Not** Gate 5A/5B.

### Lane D on `161349`

Prepared path active during overdub — **no user-visible DISP drop at wrap** (counts 34–49 stable). Does **not** reproduce the wrap-wipe symptom; use pin B for lane D root cause.

---

## Pin B — [`170838`](../../captures/session_20260819_170838.log) (lane D symptom)

**User report:** at each wrap all notes removed; only new overdub notes drawn back; **resolved at stop**.

| Fact | Value |
|------|--------|
| Track / loop | Track 6, 1-bar (`768`), slot 0 loaded from SD (**1429** stored notes) |
| Overdub | 7 wraps; enter ~10.7s, stop ~26.2s |
| Wrap rebuild | **All** `lcr,src` → `why=wrap,from=win,prep=0` (never `from=span`) |
| `publishPreparedOverdubPass` | **No-op** during session — `LoopContentResolution::publishPreparedOverdubPass` returns when `!sDeviceGateFinished`; `deviceGateComplete` only after stop (`lcr,phase,prep` ~30.4s, `lcr,mat` ~31.0s) |
| DISP at wrap | **Drops each wrap** (e.g. 7→2, 8→3, 8→4); stop restores **15–16** vs overdub max **13** |
| Telemetry | `srcbefore` / `srcafter` every wrap; **0** `srcdrop`; **5** `srcdelta` |

**Contrast (`161349` vs `170838`)**

| | `161349` | `170838` |
|---|----------|----------|
| Wrap path | `from=span` | `from=win` |
| `prep=` | gate ready during overdub | **0** all wraps |
| DISP drop at wrap | No (34–49 stable) | **Yes** |
| `srcdrop` | N/A (pre-instrumentation) | **0** (`identity_invalid` on win path after retain — shipped) |
| `srcskip` | N/A | **`stale_identity_stream`** when merged `builtFromRevision != loop.playbackRevision` — shipped |

**`srcdelta` lines (all `reason=missing_prepared_span`)**

| id | Wrap |
|----|------|
| 6381 | 3 |
| 6387, 6390 | 4 |
| 6393, **1** (record) | 5 |

In this capture these are **D2-D** (no prepared checkpoints during session), **not** D2-A sealed Hide.

**Causal chain (D2-D pinned)** — explains wrap-time drop, repetition during overdub, and stop restore **without** making `visualCache` the overdub paint owner:

```text
overdub starts
  deviceGateFinished == false
  publishPreparedOverdubPass() → no-op
  (no prepared spans/checkpoints for newly committed passes)
wrap → commitOverdubWrapAtSessionStart
  rebuildOverdubSourceView()
    tryCopyPreparedSpansToDisplayNotes → miss (prep=0)
    fallback: resolveWindow + reconstructDisplayNotes (from=win)
    window reconstruct sees only gathered window NoteOn identities
  → previously present committed identities can disappear from overdubSourceViewNotes_
  → live overdub paint loses them (DISP count drops)
stop → committed resolver / visualCache → fuller result returns
```

**Strongest proven claim ([`170838`](../../captures/session_20260819_170838.log)):** previously present committed identities **can** disappear from the source view after an unprepared (`prep=0`) wrap rebuild (`srcbefore`/`srcdelta` + DISP shrink). Telemetry does not enumerate the full pre/post identity set — do not overclaim that **all** prior-pass ids are always reconstructed away.

Owners: [`LoopContentResolution::publishPreparedOverdubPass`](../../src/LoopContentResolution.cpp), [`Loop::rebuildOverdubSourceView`](../../src/Loop/LoopCapture.cpp).

---

## Pin C — [`174246`](../../captures/session_20260819_174246.log) (post-fix verification)

**User report:** display updates correctly again after revision-freshness guard.

| Fact | Value |
|------|--------|
| Track / loop | Track 6, 1-bar (`768`), SD-loaded loop |
| Overdub | 12 wraps; prepared path active |
| Wrap rebuild | **All** `lcr,src` → `why=wrap,from=span` (contrast [`170838`](../../captures/session_20260819_170838.log) `from=win,prep=0`) |
| `srcafter` | Monotonic growth — e.g. `before=25,copy=31,after=31` → `before=48,copy=48,after=49`; **no wrap DISP wipe** |
| Identity filter | **`srcskip,reason=stale_identity_stream` × 12** (once per wrap — filter correctly inactive until revision matches) |
| Wrong prune | **`identity_invalid` = 0** (`srcdrop` on win/retain path absent) |
| Span-copy filters | `srcdrop,reason=inWindow` × 384, `truncated` × 12 — expected D2-B path noise on `from=span`, not user-visible loss |
| `srcdelta` | `copy_miss` × 77 — ids in pre-wrap source view absent from prepared checkpoints; telemetry only |
| Ledger | **`DIAG,lcr,mismatch` = 0**; occupy `why=on` **95/95** with `eq=1`, `n==a`, `led==n` |

**Contrast (`170838` vs `174246`)**

| | `170838` (pre-fix) | `174246` (post-fix) |
|---|---|---|
| Wrap path | `from=win` | `from=span` |
| `prep=` | **0** all wraps | gate ready (prepared path) |
| DISP at wrap | **Drops each wrap** | **Stable growth** |
| `identity_invalid` | blind spot pre-ship | **0** |
| `stale_identity_stream` | N/A | **12** (expected) |
| Ledger parity | not primary pin | **clean** |

**Shipped fix (Layer 2):** in `Loop::committedPlaybackNoteOnIdentityValid`, return **true** (do not prune) when `committedPlaybackMergedForIdentity_->builtFromRevision != playbackRevision`. Native: `test_occupy_source_hold_stale_revision_does_not_drop_span` in `test_pending_note_change`.

**Does not prove:** Layer 1 (`prep=0` / `from=win` on [`170838`](../../captures/session_20260819_170838.log)) is fixed — that readiness path is **parked** until a repro with post-fix firmware on the same boot/gate timing.

---

## Lane D — investigation (shared)

### What is proven vs not

| Proven ([`174246`](../../captures/session_20260819_174246.log)) | Not proven |
|--------|------------|
| Display stable across 12 wraps (`from=span`) | D2-B `inWindow` / `inactive` / `hide_companion` causing user-visible loss |
| `identity_invalid` = 0 after revision guard | Layer 1 `prep=0` / `from=win` fixed ([`170838`](../../captures/session_20260819_170838.log)) |
| Ledger parity (`eq=1`, `n==a`, `led==n`) on all occupies | |
| `stale_identity_stream` fires once per wrap until revision matches | |

**D2 question (precise):** which committed/resolved identities are absent from `overdubSourceViewNotes_` after a failing wrap rebuild, and **why**?

**D1 — identity (native, pinned)**

- `test_wrap_rebuild_retains_record_pass_note_id_after_publish` — record + wrap ids survive span rebuild when prepared path active.
- `test_wrap_rebuild_with_hide_companion_drops_record_from_source_view` — sealed Hide → record absent (**D2-A**, expected).
- `test_multi_wrap_source_view_membership_three_classes` — untouched survives; hidden absent; shortened keeps resolved geometry.

**D2 — classify every missing identity**

| Bucket | Meaning | Bug? | `170838` |
|--------|---------|------|----------|
| **D2-A** | Sealed Hide/Shorten — id absent from prepared checkpoints **by resolver design** | No | Not this capture |
| **D2-B** | Span-copy filter: `inWindow`, `inactive`, `hide_companion` | Yes | **Not reached** (`from=win`) |
| **D2-C** | Post-copy discard (`identity_invalid` after retain) | Yes | **0** on [`174246`](../../captures/session_20260819_174246.log) post-fix |
| **D2-D** | Stale merged-stream identity prune (`builtFromRevision != playbackRevision`) | Yes | **Shipped** — [`174246`](../../captures/session_20260819_174246.log) |
| **D2-D′** | `!sDeviceGateFinished` → publish no-op → span copy miss → `from=win` | Yes | **Parked** — [`170838`](../../captures/session_20260819_170838.log) only |

**Disambiguate `missing_prepared_span` in telemetry**

| Log | Meaning |
|-----|---------|
| **`srcdelta`** `reason=missing_prepared_span` | Id in pre-wrap source view, absent after rebuild, **not** in prepared checkpoints — in `170838` this is **D2-D**, not D2-A |
| **`srcdrop`** `reason=identity_invalid` | Id present after copy/reconstruct, stripped by `retainValidOverdubSourceViewIdentities` — **win or span** |
| **`srcskip`** `reason=stale_identity_stream` | Identity filter inactive — merged stream revision ≠ `loop.playbackRevision` |

**D2 span-copy filters** (`tryCopyPreparedSpansToDisplayNotes`) — only when `from=span`

| Filter | Log reason tag |
|--------|----------------|
| `inWindow` | `inWindow` |
| `capturePassIsActive` | `inactive` |
| Disabled-companion skip | `hide_companion` |

**D2 device instrumentation — shipped** (`SESSION_CAPTURE` + `ARDUINO`; stack-only; no RAM1)

| Line | When |
|------|------|
| `srcbefore,why=wrap,count=N` | Before clear |
| `srcskip,reason=stale_identity_stream,built=…,loop=…` | Before retain — merged revision ≠ `playbackRevision` |
| `srcdrop,id=…,reason=identity_invalid` | After retain — copy had id, retain removed it |
| `srcdelta,id=…,reason=…` | After retain — `missing_prepared_span` / `identity_invalid` / `copy_miss` |
| `srcafter,why=wrap,from=…,before=,copy=,after=` | Summary per wrap (`copy` = count before retain) |

**Optional next observability (not implemented):** `srcskip,reason=gate_unfinished` when `publishPreparedOverdubPass` returns early.

**D3 — stop restore (pinned)**

Stop path: `Track::stopOverdubbing` → `closeOverdubSession` → `clearOverdubSourceView()` → `resolveDisplayNotesCommitted` / `visualCache`.

**Wrap paint chain (RC12)**

```text
commitOverdubWrapAtSessionStart
  sealPendingNoteChangesToEditPasses
  publishPreparedOverdubPass        ← no-op when !sDeviceGateFinished (170838)
  rebuildOverdubSourceView          // clear + tryCopyPreparedSpansToDisplayNotes or from=win fallback
  beginCapture
  invalidateLiveDisplayCache()
resolveDisplayNotesLiveCapture
  liveDisplayNotes ← overdubSourceViewNotes_ + capturePreview
```

**Do not** fix by painting `visualCache` during overdub (RC12). **Do not** patch span-copy / `inWindow` from `170838` alone. **Do not** touch `tryCopyPreparedSpansToDisplayNotes` until a **`from=span`** failing capture pins D2-B.

---

## D2-D — stale merged-stream identity prune (shipped)

**Root cause (Layer 2):** `9e1b2b7` added `retainValidOverdubSourceViewIdentities` after `rebuildOverdubSourceView`, bound to `committedPlaybackMergedForIdentity_`. When playback rebuild had advanced `playbackRevision` but the bound merged stream still carried an older `builtFromRevision`, the filter pruned otherwise-visible ids from `overdubSourceViewNotes_`.

**Fix:** revision-freshness guard in `committedPlaybackNoteOnIdentityValid` — identity filtering is valid only when `builtFromRevision == playbackRevision` and the stream is full-loop. Otherwise return true (no prune). Wrap telemetry: `srcskip,reason=stale_identity_stream,built=…,loop=…`.

**HITL PASS:** [`174246`](../../captures/session_20260819_174246.log) — display stable across 12 wraps; ledger parity clean; `identity_invalid` = 0.

### Investigation questions — answers (git + HITL)

| # | Question | Answer |
|---|----------|--------|
| **1** | When was `sDeviceGateFinished` guard added to `publishPreparedOverdubPass()`? | **`8bda25b`** (2026-08-15) — **at function introduction**; not added during ledger slice. |
| **2** | What was `publishPreparedOverdubPass()` supposed to guarantee? | **6D.4:** append each committed overdub pass into an **already-prepared** session delta/checkpoints after idle gate indexed the record pass. Not full incremental LCR. |
| **3** | Enough info to publish new pass without full gate? | **Open** for Layer 1 only. `publishPreparedOverdubPass` indexes **one** new overdub pass into existing `sDeviceGateSession` — requires base prepared index from completed gate. **Not** the `174246` failure mode. |

### Ledger inconsistency commit chain

| Commit | What changed | Relevance |
|--------|--------------|-----------|
| **`33b48ca`** | `mergedMidiEvents` **committed-only** (no live capture fold) | Parent context for identity binding |
| **`a833acb`** | Ledger erase open ids missing from merged after full-loop rebuild | Ledger path |
| **`9e1b2b7`** | `retainValidOverdubSourceViewIdentities` + `committedPlaybackNoteOnIdentityValid` | **Regression source** — fixed by revision guard |
| **`45fc007`** | Length mismatch miss on publish/collect (Stage 1b) | Not this RC |

### Investigation verdict (closed)

| Verdict | Outcome |
|---------|---------|
| **Layer 2 regression in `9e1b2b7`** | **Shipped** — revision-freshness guard; HITL [`174246`](../../captures/session_20260819_174246.log) |
| **Layer 1 exposure (`prep=0`)** | **Parked** — [`170838`](../../captures/session_20260819_170838.log) only; do not conflate with Layer 2 fix |
| **Gate-contract redesign** | **Not needed** for the shipped regression |

Do **not** open gate-contract redesign until Layer 1 reproduces on post-fix firmware.

### D2-D invariant (holds regardless of root cause)

Every overdub wrap that rebuilds `overdubSourceViewNotes_` must resolve from **authoritative committed content**. `from=win` must not substitute for committed content **solely** because preparation was skipped.

**Negative test:** an unfinished gate must not produce a wrap where a previously visible, non-hidden committed identity disappears **only** because `from=win` was selected.

### Investigation split (do not conflate with D2-D design)

```text
170838:  prep=0, from=win, 0 srcdrop  →  D2-D (publish/gate coupling — investigate first)
future:  prep>0, from=span, srcdrop   →  D2-B (span-copy membership — parked)
```

`missing_prepared_span` in **`srcdelta`** is **not** the bug label in `170838` — expected telemetry when preparation did not run.

### Deferred until investigation closes

Do **not** open gate-contract design (block overdub vs incremental publication vs retain stale source view) until questions **1–3** are answered. Prior session options (A/B/C) are **parked** — evidence does not yet justify redesigning the gate contract.

| Deferred option | Why parked |
|-----------------|------------|
| A — Block overdub until gate complete | May be valid; not chosen until investigation shows guard is deliberate |
| B — Incremental authoritative publish | May already be what `8bda25b` intended for **post-gate** wraps; do not design anew prematurely |
| C — Retain previous source view | Reject as paper-over; stays off the table |

**Next deliverable:** D2-D investigation section in this plan (or short sibling note) with answers to **1–3** and a verdict: **regression fix** vs **deliberate contract** vs **needs design session**.

---

## Gates — proceed?

| Gate | Status | Next |
|------|--------|------|
| **N1** | **Closed** | Reopen only if capture shows `a>n` |
| **N2** | **Closed** | — |
| **D1** | **Pinned** | — |
| **D2-A/B/C** | **Parked** | Repro with `prep>0`, `from=span`, `srcdrop` — D2-B only |
| **D2-D** | **Shipped** ([`174246`](../../captures/session_20260819_174246.log)) | Layer 1 `prep=0` parked until repro on post-fix firmware |
| **D3** | **Pinned** | Observability only |
| **Instrumentation** | **Shipped** | `srcskip,stale_identity_stream` + `srcdrop,identity_invalid` |
| **D2-D design (A/B/C)** | **Parked** | Layer 1 gate readiness only — not required for shipped Layer 2 fix |

**Proceed to firmware?** **CLOSED.** Layer 1 (`prep=0` on [`170838`](../../captures/session_20260819_170838.log)) parked until repro on post-fix firmware.

---

## Hard don'ts

- Gate 5B skip/filter Length by identity
- Patch occupy collect to force `n==a`
- Reopen leftover identity RC
- Paint `visualCache` during overdub (RC12 violation)
- Second Shorten on `overdubSourceViewNotes_`
- One commit mixing lane D and lane N
- Treat `170838` `srcdelta missing_prepared_span` as D2-A Hide semantics
- Patch span-copy / `inWindow` from `170838` alone
- Touch `tryCopyPreparedSpansToDisplayNotes` before D2-B pin (`from=span` + `srcdrop`)
- Retain previous source view across wrap when publish fails (option C) without authoritative merge
- Redesign gate contract / incremental publication before bisect closes
- Apply `retainValidOverdubSourceViewIdentities` blindly on `from=win` rebuild without investigating `9e1b2b7` scope

---

## Tests

| Test | Lane | Asserts |
|------|------|---------|
| `test_wrap_rebuild_retains_record_pass_note_id_after_publish` | D1 | Record id 1 + wrap id 10 after span rebuild |
| `test_wrap_rebuild_with_hide_companion_drops_record_from_source_view` | D1/D2-A | Sealed Hide → record absent from source view (expected) |
| `test_multi_wrap_source_view_membership_three_classes` | D1/D2 | Untouched survives wraps; hidden absent; shortened keeps resolved geometry |
| `test_occupy_source_hold_stale_revision_does_not_drop_span` | D2-D | Stale `builtFromRevision` does not prune source-view hold participants |

Run: `pio test -e native -f test_overdub_source_view` and `pio test -e native -f test_pending_note_change`

---

## Related docs

- [`OVERDUB_LEDGER_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_LEDGER_NOTE_EVALUATIONS.md) — `n` vs `a` may differ
- [`OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md) — `from=win, prep=0` reconstruct path
- [`overdub_participant_source_view_span_membership_bugfix.md`](overdub_participant_source_view_span_membership_bugfix.md) — span copy, 034455 flash (FROZEN)
- [`overdub_participant_64bar_source_view_identity_and_note_off_fill_bugfix.md`](overdub_participant_64bar_source_view_identity_and_note_off_fill_bugfix.md) — prior `prep=0` / gate timing pattern
- [`overdub_occupy_missing_open_identity_bugfix.md`](overdub_occupy_missing_open_identity_bugfix.md) — **`9e1b2b7`** source-view identity filter (regression lead)
- [`overdub_occupy_leftover_identity_bugfix.md`](overdub_occupy_leftover_identity_bugfix.md) — **`a833acb`** ledger erase
