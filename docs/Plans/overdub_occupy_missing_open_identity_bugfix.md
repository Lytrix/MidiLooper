# Occupy missing open identity (`n=1 a=2` extra covering span)

**Status:** Native **PASS** 1374/1374. RAM1 **425948** / locals **4768**. HITL [`121141`](../../captures/session_20260819_121141.log): **5893-class MET** (no `5893`; identity-without-NoteOn gone). Remaining `n=1 a=2` **FAIL** — extra covering ids have `on=1`. This RC’s root cause does not explain them. **STOP.** Do not widen the identity filter. Successor: [`overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md`](overdub_occupy_source_view_keeps_resolver_geometry_bugfix.md).  
**Date:** 2026-08-19  
**Kind:** bugfix  
**Parent (leftover identity HITL PASS, remaining mismatch):** [`overdub_occupy_leftover_identity_bugfix.md`](overdub_occupy_leftover_identity_bugfix.md)  
**Pin:** [`111819`](../../captures/session_20260819_111819.log) L948–L963 pitch 24 `hs=72` `n=1 a=2`  
**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTickInLoop` / `nextEventIndex` from USB; Off stamping; FIFO; option B (ledger consults derived spans); folding capture into `mergedMidiEvents`; `isPlaybackCatchUpWindow` equal-tick contract; leftover ledger erase (`eraseOpenNotesMissingFromCommittedNoteOns`); duplicate-Off ledger apply; wrap commit at session start 320

---

## Invariant (one sentence)

A source-view / prepared identity counts as present-at-hold only if that `noteId` has a NoteOn in the complete committed playback stream that clock actually walks.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — writer stays `applyPlaybackLedgerEvent` from clock. Occupy stays a reader of `Entry.noteId`. Rematerialize still owns `runtime.mergedMidiEvents`. Identity validity filtering uses the playback stream only as a NoteOn existence predicate over existing span geometry. |
| **State transition change?** | NO — wrap still commits at session start 320. `shouldApply` skip bound unchanged. Loop-head `atLoopStart` walk unchanged. |
| **Reuse** | YES — identity validity filtering: bind the full-loop committed playback stream as an identity-existence predicate and **filter** derived source/prepared membership. Span geometry stays LCR/source-view. Do not reconstruct spans from `mergedMidiEvents`. Do not have occupy write. Do not consult spans from the ledger (option B). |

If implementation starts writing ledger Entries from source-view spans, or runs occupy catch-up when `occupyPhase <= lastTick`, **stop** — that flips a checkpoint answer to YES.

---

## Debugging boundary

```
… → leftover Entry whose On is gone from merged ← trust HITL [`111819`] n=1 a=0 = 0
 → covering source-view span whose On is not a loop-head playback NoteOn ← this RC
 → nested same-start n=2 a=2 pair ← not this FAIL
```

Do not treat leftover 5701 as open. Do not make occupy catch-up run when `occupyPhase == lastTick`. Do not stamp Off identities. Do not clear the ledger on rematerialize.

---

## Pin facts ([`111819`](../../captures/session_20260819_111819.log))

99 `DIAG,lcr,part`. **1** mismatch, `cu=0`. `led == n` **1/1**. `n=0 a=1` **0**. `n=1 a=0` **0**. `eq=1` on 99/99. 11 `DIAG,ledger,erase` — none are 5893 or 5901 (first erase is 5853 at L1497, after this occupy). Two-Off dumps **0**. Overdub wrap S is **320** (`1088, 1856, …` `% 768 == 320`). Loop length **768** = one bar (`Config::TICKS_PER_BAR`).

| n,a | 104654 | 111819 |
|-----|------:|-------:|
| **0,1** | **0** | **0** |
| **1,0** | **19** | **0 MET** |
| n>a | 41 | **0** |
| **1,2** | 0 | **1 FAIL** |

### Adjacent occupies (pitch 24)

| Line | `hs` | n,a | `as,ae` | Meaning |
|------|------|-----|---------|---------|
| L945 | 744 | 1,1 MATCH | 744–767 | Exclusive end of 5893 is 743, so only 5901 covers. Ledger `lid` not dumped on MATCH; next FAIL names 5901 `lst=744`. |
| **L948** | **72** | **1,2 FAIL** | **0–743** | Playhead wrapped `767→0` (~0.25 s later at 120 BPM / 384 ticks per second). 5893 covers again. |
| L1902 | 72 | 1,1 MATCH | 72–167 | 5893 `0–743` is gone from covering. Transient, not a permanent extra span. |

No `Overdub wrap committed` between L945 and L948 (next wrap is tick 8000 @ 45.552 s, after the FAIL). `shouldCommitOverdubWrap(767, 0)` with session start 320 is false (`didPlayheadCrossPhase` does not hit 320). Loop-head `advancePlaybackCursor` with `atLoopStart` **runs**. Source view is not rebuilt on playhead wrap (`rebuildOverdubSourceView` is wrap-commit only). 5893 was already in source view at occupy 744 as a **non-covering** span.

L937 `playback_build` full-loop `0,768,rev=85` before occupy 744. Occupy dump still reports `win=0 wlen=768 rev=85`. L946 `playback_build 6912,1536,rev=1` is a 2-bar windowed gather (long-loop path); it is not this slot’s occupy merged.

### FAIL dump (L948–L963)

| Stream | What |
|--------|------|
| Covering spans | **5893 `0–743` `p=1`**, **5901 `0–168` `p=1`** (`a=2`, `b=2`, `eq=1`) |
| Ledger | only **5901** `lst=744` `led=1` `ltick=72` `cu=0` |
| `mmevt` (first 8) | Off@168, On@312 `5853`, Off@359, On@360 `5860`, Off@407, On@408 `5867`, Off@503, On@504 `5873` — **no On@0**, **no `id=5893`**, **no On@744** |

`as=0,ae=743` on the part line is 5893 (first covering source-view walk). Newest ledger id is 5901.

---

## Root cause

Two cooperating facts. Leftover identity closed the ledger side of rematerialize; this FAIL is the span side.

### 1. No playback NoteOn at 0 for 5893 (proven)

`copyEffectiveCommittedEvents` / `LoopPasses::materializeToEventVector` is tick-sorted (`mergeSortedMidiVectors`). Occupy mismatch `mmevt` walks that vector in order.

`Config::TICKS_PER_BAR` is **768**. `occupyMismatchTickDistance` max on a 768-tick loop is 384, so the one-bar filter drops **nothing**. Cap 8 is the first eight pitch-24 note events in the whole merged stream.

The dump starts at Off@168. There is **no pitch-24 NoteOn or NoteOff with `tick < 168`** in committed playback merged.

5893’s covering geometry is `startTick=0`, `endTick=743` (inclusive display end). A linear playback note `On@0 Off@743` would appear before Off@168. It does not. Clock `didPlaybackEventCross(atLoopStart=true, …, evPhase=0, tickInLoop=0)` would apply `evTick <= 0` **if** that On existed. The On is not in the stream, so loop-head cannot write a 5893 Entry.

5901 `lst=744` is the wrap note from the previous cycle (`744–767` + `0–168`). That Entry is legitimate and preserved across playhead wrap (`runtime.reset(true)` / leftover retain). Occupy 744 `n=1 a=1` agrees.

### 2. Prepared / source-view still lists 5893 as covering (proven)

`a` is `collectOverdubSourceHoldParticipantIds` (source-view `displayNotePresentAtHold`). `b` is `tryCollectPreparedPresentNoteIdsAtTick`. `eq=1` — prepared checkpoints and source view agree on two covering ids.

`displayNotePresentAtHold` is inclusive start, exclusive end. `0–743` covers 72 and does **not** cover 744. That matches L945 MATCH → L948 FAIL with no source-view rebuild in between.

`tryCopyPreparedSpansToDisplayNotes` keeps a span iff its `noteId` is a NoteOn id in **`overdubSourceViewEvents_`** (prepared LCR window from `tryResolvePreparedWindow`, else live `resolveWindow`). That is the 030219 leftover-checkpoint filter. It is **not** the playback `mergedMidiEvents` leftover uses to drop ghost Entries.

Leftover `eraseOpenNotesMissingFromCommittedNoteOns` only **drops** ledger Entries whose On is gone. It does not push missing Ons, and it does not drop prepared / source-view spans. Occupy catch-up is skipped (`cu=0`, `ltick=72`). Occupy cannot repair.

Production occupy (`collectOverdubNoteOnParticipantIds`) already follows the ledger (`n=1`). The extra identity is diagnostic `a`/`b` and any NoteOff overlap walk of covering source-view notes (`accumulatePendingNoteChangesFromSourceNotes`).

### What this is not

| Rejected class | Why |
|----------------|-----|
| Leftover 5701 | Opposite direction (`n>a`); 5701 not a leftover `lid` this session |
| Loop-head skip of On@0 | On@0 is not in merged; wrap-pass catch-up did not steal the walk (`shouldCommitOverdubWrap(767,0)` false) |
| Duplicate Off | Zero two-Off dumps; `led == n` |
| Occupy skip bound | `cu=0` is expected at `ltick == hs`; do not change it |
| Option B | Ledger must not consult derived spans |

The 8-event cap does **not** prove 5893’s NoteOn is absent at ticks `> 504`. Occupy 744 `n=1` proves 5893 was **not open** at 744, so that On is not at 744 (a second same-pitch On at 744 would push a second Entry). Combined with no On@0, 5893’s covering `0–743` is not backed by a loop-head playback NoteOn.

---

## Fix: identity validity filtering

A span is retained only if its `noteId` is present among NoteOn identities in the **complete full-loop committed playback stream**.

Existing span geometry remains authoritative. The playback stream supplies only the **identity-existence predicate**. Do not reconstruct source spans from `mergedMidiEvents`.

```
existing span geometry
        +
full-loop playback NoteOn identity set
        ↓
valid source spans
```

not:

```
mergedMidiEvents
        ↓
reconstruct source spans from scratch
```

1. Construct the identity set **only** when `isFullLoopMergedPlaybackWindow` is true **at the construction site** (`Loop::replaceCommittedPlaybackNoteOnIdentities`). A partial window must not declare an identity nonexistent — filter stays inactive.
2. After that full-loop rebuild, filter derived membership: prune identified `overdubSourceViewNotes` whose id is absent; `collectOverdubSourceHoldParticipantIds` and `tryCollectPreparedPresentNoteIdsAtTick` use the same predicate so `a`/`b`/`eq` stay aligned. Do not mutate LCR checkpoints.
3. Observability: mismatch dump, for each covering span id, log whether a NoteOn with that id exists anywhere in merged (full scan, not first 8). Marker `DIAG,lcr,mmspan,…,on=0|1`.

If HITL still shows `n=1 a=2` after this filter and the extra span’s `noteId` **does** exist as a NoteOn in the full merged stream, this RC’s root cause is invalid — stop and open the next investigation. Do not widen into wrap-head-after-Off.

Native fixture (generic, then pin shape):

| Merged (tick-sorted) | Prepared / source-view spans | Occupy at 72 | After filter |
|----------------------|------------------------------|--------------|--------------|
| On@744 id=B, Off@168 | A `0–743`, B `0–168` | n=1 (B) a=2 | a=1 (B only) — A has no NoteOn in merged |
| On@0 id=A Off@743, On@744 id=B Off@168 | A `0–743`, B `0–168` | n=2 after loop-head | a=2 — A retained |

Do not add A to the ledger from the span. If HITL still shows `n=1 a=2` after this filter, the covering id **has** a NoteOn later in merged — open a new RC; do not widen this one into wrap-head-after-Off.

---

## HITL [`121141`](../../captures/session_20260819_121141.log)

73 `DIAG,lcr,part`. **40** mismatches. All `cu=0`. `led == n` **40/40**. Two-Off dumps **0**. `5893` **0**. `5701` **0**. Wrap committed **25** (first `@ 1232`).

| Gate | 111819 | 121141 |
|------|------:|-------:|
| `n=0 a=1` | **0** | **0 MET** |
| `n=1 a=0` | **0** | **10** |
| `n=1 a=2` | **1** | **2 FAIL** |
| `led == n` | 1/1 | **40/40 MET** |
| n>a | 0 | 37 |
| `eq=1` | 99/99 | **0/73** (`b=0`; `tryCollectPreparedPresentNoteIdsAtTick` miss — `src` lines `prep=0`) |

### This RC (identity without a playback NoteOn)

Pin 5893 is gone. The identity-existence predicate did not leave a covering span with `on=0`.

### Remaining `n=1 a=2` — stop condition hit

Both extras **have** a NoteOn in the full merged stream (`mmspan on=1` and `mmevt` On). This RC’s root cause is **invalid** for these FAILs. Do not widen the identity filter. Do not reconstruct spans from `mergedMidiEvents`. Do not write the ledger from spans.

| Line | Pitch | `hs` | Ledger | Covering (`p=1`) | Extra On in merged | Off before occupy |
|------|------:|-----:|--------|------------------|--------------------|-------------------|
| L2924 | 30 | 144 | 6019 `lst=48` | 6019 `48–192` `on=1`; **6031 `96–192` `on=1`** | On@96 `6031` | Off@144 |
| L3298 | 24 | 312 | 6079 `lst=144` | 6079 `144–360` `on=1`; **6073 `168–360` `on=1`** | On@168 `6073` | Off@264 |

`ltick == hs`, `cu=0` — clock has already walked those Offs. Source-view geometry still lists the extra id as covering.

### Not this FAIL

- `n=1 a=0` / other `n>a`: extra-open; e.g. L3344 lid **6117** span `192–288` `p=0` `on=1` at occupy **480**. On exists; span does not cover. Not a missing-On identity.
- Nested `n=2 a=2` (4 parts) — cardinality, not this gate.
- `eq=0`: prepared collect returned false this session (`b=0`). Not `a` vs `b` filter divergence (`ao=0`, `bo=0`).

---

## Tests

- Native **PASS** 1374/1374: drop span A when merged has no NoteOn for A; retain A+B when both Ons exist; partial window does not prune.
- Do not change `isPlaybackCatchUpWindow(88,88)==true`.
- Firmware `teensy41-capture-serial` RAM1 **425948** / locals **4768**. HITL [`121141`](../../captures/session_20260819_121141.log): 5893-class MET; remaining `n=1 a=2` has `on=1`.

---

## Pre-implementation review

### Ready
- Pin dump L948–L963, adjacent occupies L945 / L1902, wrap S=320, leftover erase ids.
- `materializeToEventVector` tick-sort, `occupyMismatchTickDistance` vs `TICKS_PER_BAR=768`, loop-head `didPlaybackEventCross`, `tryCopyPreparedSpansToDisplayNotes` inWindow source.
- Production occupy is ledger; `a` is source-view covering.

### Resolved (user / code)
| Topic | Decision |
|-------|----------|
| Occupy | Still a reader; skip bound unchanged |
| Option B / Off stamp / FIFO | No |
| Whole ledger clear | No |
| `isPlaybackCatchUpWindow` | Unchanged |
| Windowed gather | Do not prune spans against a partial window |
| Filter | Identity-existence predicate on existing span geometry. Not reconstruct from merged. |
| Identity set construction | `isFullLoopMergedPlaybackWindow` at the collect site. Partial window → filter inactive. |
| Push missing Ons onto ledger | No — that is occupy/clock writing from spans |

### Open before coding
None — user approved identity validity filtering (2026-08-19). Nested `n=2 a=2` is not this FAIL.

### Proceed?
YES.
