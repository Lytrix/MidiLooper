# Remove overdub-wrap derived reconstruction from the clock tick

**Status:** Firmware in tree. Native **1496/1496**. Capture-serial RAM1 locals **6944**. Device recapture [`135510`](../../captures/session_20260831_135510.log) recorded. HITL three-part gate: **B PASS**, **C PASS** vs the 77 ms class, **A not observed** (no occupy at wrap S). Do not check off OpenSpec `3.5` / `3.6`.  
**Date:** 2026-08-31  
**Kind:** bugfix  
**Parent:** [`overdub_wrap_committed_pass_playback_bugfix.md`](overdub_wrap_committed_pass_playback_bugfix.md)  
**Evidence:** [`005527`](../../captures/session_20260828_005527.log) first 12-bar wrap due 58.687 s; same window `track_update[4]=77.618 ms`; only timed child `playback_build` T4/S0 compatibility 8.429 ms at 58.760 s.

**Does not start:** Ranks 1–3, 5–8. Snapshot publication. `T_RT_reserve`. Deferring `commitCapturePass` or companion `sealPendingNoteChangesToEditPasses`. OpenSpec `3.5` / `3.6` checkoff.

---

## Invariant (one sentence)

After `commitOverdubWrapAtSessionStart()`, the wrap clock path may update existing playback/source representations only incrementally from the newly sealed pass. It must not invoke any full-loop reconstruction, merged-event gather, playback-order rebuild, span-cache reconstruction, or LCR preparation.

Success is **no full-loop derived reconstruction on the wrap clock tick**. Timing improvement is evidence of that property, not the property itself.

---

## Rank 4 fact

On [`session_20260828_005527.log`](../../captures/session_20260828_005527.log) the first 12-bar wrap is due at 58.687 s. The same 5 s window reports `track_update[4]=77.618 ms`. The only timed child is `playback_build` T4/S0 compatibility **8.429 ms** at 58.760 s.

That 77 ms is one `Track::playMidiEvents` → `playCommittedLoopMidi` call. After `maybeCommitOverdubWrap` the same tick still ran a full-loop derived stack that the shipped wrap-tick plan already said must not live there.

This slice does **not** split the overdub wrap itself off the clock interval. Musical wrap stays on the clock. What leaves the wrap tick is **derived reconstruction after the wrap**.

---

## Architecture checkpoint

| Question | Answer |
|---|---|
| Ownership change? | NO — sealer stays `commitOverdubWrapAtSessionStart`; occupy still reads `Entry.noteId`; source view stays `Loop` |
| Musical wrap semantics change? | NO — seal, held-on re-append, ledger catch-up stay on the clock |
| State transition change? | YES — user-approved. Full source-view / LCR / merged-order rebuild move from wrap clock to later tick / idle |
| Full derived rebuild on clock? | Removed from the wrap tick |
| New owner / scheduler? | NO |
| Unbounded interval owner? | NO — wrap tick is seal + newly sealed pass material only. Next-tick gather is Rank 6. Idle source-view rebuild stays on the existing idle owner |

Reuse: YES — extend `commitOverdubWrapAtSessionStart` and `catchUpOverdubWrapPlaybackLedger`. Do not add a Manager, helper type, or `cancel*` / `abort*`. Do not put wrap follow-up back onto `handleMidiInput` or clock dispatch.

---

## Incremental vs reconstruction

```text
wrap (incremental wrap admission):
    applyCommittedOverdubPassPlaybackInterval(...)
    append wrap-pass notes to existing source view

not wrap (reconstruction):
    rebuildOverdubSourceView(...)
    ensurePlaybackMergedMidiEventsBuilt(...)
    rebuildPlaybackOrder(...)
```

**O(pass):** the wrap append and wrap-pass ledger apply iterate only the newly sealed pass entries. They do not traverse historical `LoopPasses`, the full source view, or the full loop.

`overdubSourceViewNotes_` remains a derived Loop-owned representation. The O(pass) append is a consumer-readiness bridge so occupy `a=` sees the just-sealed notes. It does not create or mutate authoritative loop content.

---

## What stays vs what leaves

**Stay on the wrap tick**

- `Track::commitOverdubWrapAtSessionStart`: held-on extract, `commitCapturePass(CommitReason::OverdubWrap)`, `beginCapture`, re-append held ons, `noteOverdubWrapCommitted`
- `sealPendingNoteChangesToEditPasses` + `pushOverdubSessionPass`
- `queueOverdubWrapSnapshotJournal` — queue only
- `catchUpOverdubWrapPlaybackLedger`: old-stream `(prev, S]`, then `applyCommittedOverdubPassPlaybackInterval` over newly sealed pass entries only
- Reanchor at S so a later rebuild reconstructs **as of S** and does not replay `(prev, S]`
- Incremental append of newly sealed pass display notes into the existing `overdubSourceViewNotes_`

**Leave the wrap tick**

- `ensurePlaybackMergedMidiEventsBuilt` / `rebuildPlaybackOrder`
- `rebuildOverdubSourceView` / span-cache reconstruct
- `reconcilePlaybackLedgerAfterFullLoopRebuild` / `eraseOpenNotesMissingFromCommittedNoteOns` / `replaceCommittedPlaybackNoteOnIdentities` as a full-stream walk
- `LoopContentResolution::publishPreparedOverdubPass`
- `markAffectedDisplayCacheRanges` / `invalidateCaches` / `invalidateLiveDisplayCache`

**Later `playCommittedLoopMidi`:** existing revision-triggered gather. That is Rank 6, not wrap. Do not attribute it to the wrap-tick bound.

**Idle:** `Track::processDeferredIdleMaintenance` VisualCache turn while OVERDUBBING when `!overdubSourceSpanCacheReady()` → `rebuildOverdubSourceView` + `publishPreparedOverdubPass`. Wrap `appendLastCommittedOverdubPassToSourceView` restamps the existing span cache so that idle rebuild is not taken after a wrap that left span-cache notes.

---

## S correctness

```text
after wrap:
    playback/source state represents through S

after later rebuild:
    representation still represents through S exactly once
```

---

## Observability

Capture-gated children on the wrap tick only (`SESSION_CAPTURE` / `SC_*`, FLASHMEM):

```text
#CAP,...,DIAG,wrap,total=<us>,seal=<us>,pass_apply=<us>,src_append=<us>
```

Negative proof on that same tick: no merged gather, full source rebuild, LCR publish, or playback-order rebuild.

---

## Tests and device gate

Keep:

- `test_wrap_committed_note_at_s_occupies_ledger_same_tick`
- `test_wrap_pass_events_at_s_occupy_without_merged_rebuild`
- `test_wrap_committed_note_at_s_crosses_when_reanchored_at_prev`

Add:

- Wrap tick occupy `n=1 a=1` without `ensurePlaybackMergedMidiEventsBuilt` or full `rebuildOverdubSourceView`
- Wrap-pass notes present in the kept source view after incremental append
- Later full rebuild does not duplicate wrap-pass notes already incrementally appended

HITL three-part gate (firmware authorization requires all three):

- **A. Musical correctness** — at wrap `S`: `n=1`, `a=1`, no duplicate or missing note ([`185831`](../../captures/session_20260818_185831.log) shape)
- **B. Execution attribution** — wrap tick contains only `seal`, `pass_apply`, `src_append`, journal queue; no full reconstruction
- **C. Timing** — wrap interval no longer contains the ~77 ms `playCommittedLoopMidi()` class

Separately record the next-tick Rank 6 gather if it happens.

### Device recapture [`135510`](../../captures/session_20260831_135510.log)

11-bar loop (`8448` ticks). Overdub opened at tick **1192**. One wrap: `Overdub wrap committed @ tick 9640` (`1192 + 8448`).

| Gate | Result | Evidence |
|---|---|---|
| A | **Not observed** | Zero `lcr,part` rows with `hs=1192`. First post-wrap occupy is pitch 48 at `hs=1248` `n=0 a=0` (`57788309`). Later-cycle occupy of first-pass notes is `n=1 a=1` (e.g. pitch 39 `as=3696` `hs=3816` at `64479140`) — not wrap S. |
| B | **PASS** | One wrap child line: `DIAG,wrap,total=41851,seal=36025,pass_apply=676,src_append=5150` at `57675307`. Zero `lcr,src,why=wrap`. Idle source rebuild is later: `why=idle,from=passes` at `57698439`. |
| C | **PASS** vs 77 ms class | Same 5 s window: `track_update[4]=41902` / `tracks=42043` (`61765021`). That is the wrap child total (`41851`), not the [`005527`](../../captures/session_20260828_005527.log) 77.618 ms class. |

Rank 6 (not wrap): `playback_build,compatibility,7547,0,8448,3,4,0,1` at `57711324` (36 ms after the wrap line). `VCACHE,stale` on the wrap interval is `commitCapturePass` → `notifyCommittedContentChanged` → `markPassDerivedStale`, not a wrap-tick full source/LCR/order rebuild.

Remaining wrap-tick owner is **seal = 36025 µs**. That stays on the clock. Do not freeze `T_RT_reserve`.

Do not freeze `T_RT_reserve`. Do not check off OpenSpec `3.5` / `3.6`.
