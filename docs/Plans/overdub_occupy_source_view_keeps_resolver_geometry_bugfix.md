# Occupy source view keeps resolver geometry

**Status:** Gates 1–4 pinned. Coordinate conversion **pinned**. **Investigation approved.**  
**Gate 5A** (EditApply pairing): **shipped** — `findNoteOffForOnIndex` equal-tick Off-before-On. Native `test_edit_apply` PASS. HITL not run.  
**Gate 5B** (Length `6073` 551 provenance): effect proven, source **not** proven — **no implementation permitted**.  
Do not land 5B with 5A. Native Gate 4: Off@168 disappeared only when Length targeted `6073` (pre-5A). After 5A, On-then-Off dump order no longer steals Off@168.  
**Date:** 2026-08-19  
**Kind:** bugfix  
**Parent (5893-class MET, remaining one open ledger identity vs two source-view covering identities (`n=1 a=2`) with `on=1`):** [`overdub_occupy_missing_open_identity_bugfix.md`](overdub_occupy_missing_open_identity_bugfix.md)  
**Pin:** [`121141`](../../captures/session_20260819_121141.log) L2324 `6079 144–168` → L2827 / L3298 `6079 144–360`  
**Evaluation catalogs:** [`OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md) · [`OVERDUB_LEDGER_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_LEDGER_NOTE_EVALUATIONS.md)  
**Does not reopen:** occupy catching up when `occupyPhase <= lastTick`; `playMidiEvents` from occupy; advancing `lastTickInLoop` / `nextEventIndex` from USB; Off stamping; FIFO; option B; identity-existence filter; `isPlaybackCatchUpWindow`; `ActiveNoteLedger`; hot-path; `NoteGeometryResolver` / LCR ownership; a second Shorten algorithm

---

## Invariant (one sentence)

Resolver geometry is authoritative for `overdubSourceViewNotes_`: after rebuild, A (`6079`) still exists with Length-replay DisplayNote `startTick=144`, `endTick=167`. Rebuild must not reintroduce A's pre-resolution Off@360 pairing (`144–360`).

The fix is **not** “make B the only note.” A must remain. Do not write `144–168` as the DisplayNote/`ae=` fields of a Length-167 handoff — that is Off@168 pairing (L2324), a different convention.

**Names:** CAP `n=` = **open ledger identities** (`collectOverdubNoteOnParticipantIds`). CAP `a=` = **source-view covering identities** (`collectOverdubSourceHoldParticipantIds`). They may differ.

**Companion rule (commit at Off):** a later same-pitch NoteOn can constrain the preceding note's end, but the resulting geometry is not independently final until the later note has its own terminating Off. An open B is **not committed as a sounding/resolved note span**; it remains live capture state and is carried across the overdub wrap. **Gate 3:** B's committed terminating Off on this pin is Off@264 (On@168 `6073`). Wrap-pair `168–360` is not that Off.

---

## Coordinate conventions (pinned — do not infer)

`endTick` is **not** one coordinate. Three representations, proven from code and native (`test_pending_shorten_long_source_on_overlap`, `test_pending_shorten_applies_to_display_notes`, `test_seal_pending_shorten_to_edit_pass_after_overdub_publish`: causing 120 → payload **119**).

**Length payload convention:** for incoming NoteOn at `causingStart=168`, `computeShortenedEndTick` stores **167**. That value is the edit-pass / MIDI Off-tick representation. It is **not** the resolver exclusive-end 168.

**Conversion (every step named):**

```
causingStart = 168
    ↓  computeShortenedEndTick = causingStart - 1
Pending Shorten.endTick = 167
    ↓  sealPendingNoteChangesToEditPasses copies change.endTick
Length EditPass.endTick = 167   targetNoteId = A
    ↓  applyChangeLengthById shorten → shortenNoteEndById
gathered NoteOff.tick = 167
    ↓  exclusiveEndForLoopOff(off) = off + 1
canonical exclusive interval = [144, 168)
    ↓  displayInclusiveEndTick(exclusiveEnd) = exclusiveEnd - 1
DisplayNote.endTick = 167     (Off tick, same as payload)
    ↓  overlay of pending copies change.endTick onto DisplayNote with no +1
occupy as=/ae= dumps DisplayNote fields → ae=167
    ↓  displayNotePresentAtHold: start <= s < DisplayNote.endTick
covering = 144 <= s < 167
```

Pending overlay (`applyPendingHideAndShortenToNotes`) writes `DisplayNote.endTick = 167` directly. Reconstruct after Length replay produces the same `167` via Off@167 → exclusive 168 → inclusive 167. Native paint asserts `endTick == 119`, not 120.

| Name | Value for this pin | Do not call it |
|---|---|---|
| Resolver exclusive end (B.start) | 168 | Length payload / `ae=` |
| Length / pending / Off event / `DisplayNote.endTick` / occupy `ae=` | **167** | exclusive end 168 |
| L2324 cache `ae=168` | Off@168 pairing (Gate 1) | Length-167 handoff |
| Occupy covering after Length 167 | `144 <= s < 167` | `s < 168` |

Canonical `[144, 168)` exists only inside reconstruct (`exclusiveEndForLoopOff`). Occupy and occupy dumps never see that 168; they see DisplayNote `endTick=167`.

**Tick 167 is the disagreement.** Canonical exclusive `[144, 168)` includes 167. Occupy covering `144 <= s < 167` does **not**. Do not write DisplayNote/`ae=` as `[144,168)`.

**Handoff test (point 3) asserts identity and DisplayNote fields:**

```
A: noteId = 6079, startTick = 144, endTick = 167
B: noteId = 6073, startTick = 168, endTick = 264
```

It must not pass by asserting `ae=168`, or by producing some shortened pitch-24 note without those ids.

---

## Architecture checkpoint

| Question | Answer |
|----------|--------|
| **Ownership change?** | NO — `resolveConstrainedGeometry` remains the sole **overlap-geometry decision authority** (DEC-031). That does **not** guarantee the event/EditPass carrying the decision survives replay. `rebuildOverdubSourceView` still owns filling `overdubSourceViewNotes_`. Occupy still reads source-view covering identities. Clock still writes open ledger identities. |
| **State transition change?** | NO — wrap still seals then rebuilds. |
| **Reuse** | **5A shipped:** `findNoteOffForOnIndex` walks vector order, Off before On at an equal tick (`midiEventChronologicalLess`). Not a full tick sort (that nested capture On/Off pairs and failed `test_pre_commit_order_overlap_changes_before_move_and_pitch`). **5B:** no owner to extend — provenance unproven; do not skip/filter Length by `targetNoteId`. Do **not** add a source-view store. Do not call `resolveConstrainedGeometry` again inside rebuild. Do not patch `collectOverdubSourceHoldParticipantIds`. |

If implementation recreates Shorten from raw MIDI, or makes occupy write, **stop**.

---

## Locked architecture (owner chain)

```
MIDI / passes
    ↓
existing resolver
resolveConstrainedGeometry   (commits when the later note's Off arrives)
    ↓
resolved geometry
A Length 167 → DisplayNote 144–167 (canonical exclusive [144, 168) only inside reconstruct)
B DisplayNote 168–264     ← Gate 3: committed Off@264
    ↓
source-view cache
    ↓
occupy reads source-view covering identities
```

Ledger stays independent. Open ledger identities and source-view covering identities may differ — they answer different questions.

```
clock: On@144 A, On@168 B → ledger {A,B}
       Off@264 closes B   → ledger {A} at 312

source view:
A DisplayNote 144–167     ← Length-167 handoff (occupy ae=167, covering s < 167)
B DisplayNote 168–264     ← Gate 3 committed Off tick 264; covering s < 264

hold 150: open ledger identities = {6079, 6073}   source-view covering identities = {6079}
hold 312: open ledger identities = {6079} (Off@264 closed B; Off@360 not yet applied)
          committed B does not cover 312; wrap-pair 168–360 is not B's end
```

Hold 150 is settled (two open ledger identities, one source-view covering identity). **Hold 312 occupy reading (Gate 3):** `n=1` is A still open in the ledger. Intended `a=` with Length-167 A (`ae=167`, covering `s < 167`) and no wrap-pair add is **0**. The FAIL `a=2` is bug covering `6079 144–360` plus wrap-pair `6073 168–360`. Do not encode “312 = B only.”

---

## Debugging boundary

```
… → identity-without-NoteOn ← trust HITL 121141 5893-class MET
 → wrap L2772 gather applyActiveEdits deletes committed Off@168
      → reconstruct pairs 6079 to Off@360
      ← this RC (Gate 2 named the gather step; companion row not dumped)
 → extra-open (more open ledger identities than source-view covering identities) ← not this FAIL
 → same-tick wrap continuity ← follow-up, not this RC
```

---

## Proven pin ([`121141`](../../captures/session_20260819_121141.log))

```
resolved before rebuild (L2324–L2325):
6079 A = 144–168
6073 B = 168–264

              ↓ rebuildOverdubSourceView()
              ↓ clears view
              ↓ raw LIFO reconstructDisplayNotes
              ↓ appendOverdubPassWrapPairedNotes (geometry match)

wrong (L3298 hs=312, one open ledger identity vs two source-view covering identities):
6079 A = 144–360     ← covers 312 (bug)
6073 B = 168–264
6073 B = 168–360     ← also covers 312
```

Two rebuild-path problems:

1. **A is replaced by raw LIFO geometry** — this is why A covers 312.
2. **B can be duplicated** because append matches pitch+start+end, not `noteId`.

`from=win`, `prep=0` on every wrap `DIAG,lcr,src` during the session. Device-gate phase lines start at L5404, **after** the occupy FAIL.

| Candidate | Verdict |
|-----------|---------|
| `rebuildOverdubSourceView` / `reconstructDisplayNotes` | **Pinned** — clears, then merged LIFO. L2324 → L2827 after wrap rebuilds L2502 / L2616 / L2772. |
| `appendOverdubPassWrapPairedNotes` | Not the A replacement. Adds B `168–360` beside `168–264`. |
| `ensureOverdubSourceNotesForHold` | Excluded — no `why=hold`; 768-tick loop never takes JIT. |

Root-cause framing: **the resolver decides overlap geometry. A later rebuild reconstructs from events that no longer carry that decision.** Do not patch occupy, the ledger, or LCR around this. Do not treat resolver ownership as persistence of its result.

Causal chain at the pin (A `144–360` is **downstream** of Off@168 disappearing):

```
resolver / committed events
        ↓
applyActiveEdits / applyNoteEditPassSequence   ← Gate 4 names this row
        ↓
Off@168 disappears
        ↓
LIFO reconstruct
        ↓
A 6079 pairs with Off@360
        ↓
A becomes 144–360
        ↓
wrap-pair adds B 168–360
        ↓
occupy sees a=2 at 312
```

---

## Overlap commit lifecycle (blocks the handoff)

**The overlap Shorten is committed when the later note's NoteOff arrives, not when its NoteOn arrives.** Until B has a terminating Off, A's shortened end is not independently final.

### Proven — resolver runs at Off only

`Loop::accumulatePendingNoteChangesFromSourceNotes` → `resolveConstrainedGeometry` is reached only from `Loop::accumulatePendingNoteChangesForIncomingNote`, which has exactly two firmware call sites, both in [`TrackCaptureInput.cpp`](../../src/Track/TrackCaptureInput.cpp) and both on a NoteOff:

| Call site | Trigger | `[startTick, endTick)` passed |
|---|---|---|
| `Track::appendCaptureEvent` overdub branch | live `type == midi::NoteOff` | `prior.tick` (B's On found by reverse capture scan) → `newEvt.tick` |
| `Track::finalizePendingNotes` | synthesized Off via `appendCaptureNoteOffAtPhase` at stop | `prior.tick` → stop `phaseTick` |

Consequences:

- While B is open there is **no pending Shorten for A at all**. A keeps its original geometry. There is no intermediate "A shortened, B end unknown" pending state.
- `Track::commitOverdubWrapAtSessionStart` does **not** call `finalizePendingNotes`. It calls `Loop::extractOpenCaptureNoteOns` and re-appends the held Ons into the next capture pass. So a wrap that happens while B is open carries B's On forward **without** an Off and **without** a resolver run.
- Overdub stop does call `finalizePendingNotes`, so the synthesized Off is what commits the overlap for a never-released B.

### Withdrawn from the earlier handoff

The earlier line "B's per-pass reconstruct (`168–360`) is authoritative for B" is **withdrawn**. `reconstructDisplayNotes(..., overdubPassWrapPairing = true)` is documented in [`NoteUtils.h`](../../include/Utils/NoteUtils.h) as a **pairing heuristic** ("keeps a wrap-held pair when later same-pitch body notes are already completed"), not resolver output. An open B pairing to A's original Off@360 is exactly the fallback case — reconstruct output can be **ahead of** the resolver's committed state.

### Tension at the pin — **resolved (Gate 3)**

At L3298 the committed stream contains Off@264 and Off@360. Clock `ltick=312` `cu=0`: `didPlaybackEventCross` is `(prev, tick]`, so Off@264 is already applied and Off@360 is not. Untagged Off@264 LIFO-closes newest pitch-24 Entry `6073`; ledger `n=1` `lid=6079`. Merged reconstruct of B is `168–264` (`p=0` at 312: `312 < 264` is false). Wrap-pair `168–360` (`p=1`) is `appendOverdubPassWrapPairedNotes`, first dumped at L3298, absent at L2827 (would have covered `hs=552` and `a` would not be 0).

Committed B is `[168,264)`. That does **not** cover hold 312. The old “occupy 312 = B only” reading assumed B ended at 360. That reading is withdrawn.

### Settled semantics (user decision, matches current code)

1. **An open B is not committed as a sounding/resolved note span.** It remains live capture state and is carried across the overdub wrap. It must not enter the committed overdub pass, must not reach `ActiveNoteLedger`, and must not be played until its Off is recorded. Today `Track::commitOverdubWrapAtSessionStart` calls `Loop::extractOpenCaptureNoteOns` **before** `Loop::commitCapturePass` and re-appends the held Ons after `beginCapture`. No change needed.
2. **A synthesized NoteOff at record/overdub stop is a valid close.** When no performer Off arrived, `Track::finalizePendingNotes` → `appendCaptureNoteOffAtPhase` writes the Off at the stop phase tick, and that Off is what commits the overlap. No change needed.
3. **Restoring a note that has no NoteOff is a guardrail, not the normal path.** `shouldRestoreCommittedOverlapOnOverdubStop` returns a constant `false` under DEC-031 G2, so `Loop::removeOpenCaptureNoteOn` is only reachable for a session without a source view. Keep it as a guardrail; do not build the fix on it.

Consequence: there is **no** state where B is committed with a manufactured endpoint. While B is open there is no B span, no B ledger entry, and no Shorten for A — A legitimately keeps its original end until B closes.

---

## Catalog findings that change Gate 1 (not a new RC)

Pinned from [`OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md`](../Guides/OVERDUB_OVERLAP_RESOLVE_NOTE_EVALUATIONS.md). These do **not** reopen ledger ownership, candidate lookup, or the classifier.

### 1. Source-view covering identities / pin `as=`/`ae=` are the source-view cache, not pending overlay

Firmware wrap/stop **never** calls `applyPendingNoteChangesToOverdubSourceView`. That merge exists for native tests. Display paint overlays pending via `applyPendingNoteChangesToDisplayNotes` only.

`collectOverdubSourceHoldParticipantIds` and the occupy `as=`/`ae=` dump walk `overdubSourceViewNotes_` with **no** pending apply.

Therefore L2324 `6079 144–168` is a **cache** row produced by reconstruct pairing to committed Off@168. It is **not** proof that pending Shorten or sealed Length 167 was in `overdubSourceViewNotes_`. Do not design a fix that “re-applies pending onto the cache at occupy time.”

### 2. Sealed Length payload is Off tick `causingStart − 1`, not B.start

Native (`test_pending_shorten_long_source_on_overlap` / `test_pending_shorten_applies_to_display_notes`): incoming B `[120, 160)` → pending `endTick = 119` → sealed Length `endTick = 119` → gathered Off@119. `computeShortenedEndTick` is that `-1`.

If B On is 168, the Length row to hunt in Gate 1 is **`endTick = 167`**, not 168. Canonical exclusive `[144, 168)` exists only inside reconstruct. Occupy covering after Length 167 is `144 <= s < 167`. Do not call DisplayNote `[144,168)`. The three-point fixture must assert `targetNoteId` plus Off-tick payload 167. Do not “correct” the `-1`.

How pin `ae=168` relates to pending/seal 167: **answered in Gate 1** — `ae=168` is Off@168 in the committed dump, not Length 167. Off@167 is absent from L2324 pitch-24 `mmevt`.

### 3. Add is overlay, not persistence

B’s On/Off are already in capture before the resolver runs. `sealPendingNoteChangesToEditPasses` ignores Add. Gate 3 looks at committed capture/pass events for B, not a companion Create row.

### 4. Rebuild is destructive to the **source-view cache** unless it re-applies sealed overlay

`rebuildOverdubSourceView` `clear()`s then `reconstructDisplayNotes`. That can drop resolved geometry from the cache while the companion EditPass still holds it. Rebuild consumes sealed rows; it does not create Length-167. Gate 4 named the Off@168 killer as Length of `6073`. **5A** is pairing so that row (and Length `6079` 167) apply to the correct Off. **5B** is why Length `6073` 551 exists — not a skip in apply. Do not pass a wrapped circular interval into `resolveConstrainedGeometry`. Do not call the resolver again inside rebuild.

**Not a potential fix for this RC:** changing inclusive classification to half-open; persisting Add as EditPass; JIT as a second geometry owner; applying pending onto the cache from occupy.

---

## Investigation gates (ordered; firmware blocked)

Items 1–3 are **not independent**. Gates 1–4 are pinned. **Gate 5A shipped** (equal-tick Off-before-On in `findNoteOffForOnIndex`). **5B** Length `6073` 551 provenance — no implementation.

### Gate 1 — Prove the post-close state transition (blocker) — **PINNED**

```
B NoteOff arrives
    ↓
resolveConstrainedGeometry          (only if accumulate ran)
    ↓
PendingNoteChange Shorten           (endTick = Off tick 167 for B On@168)
    ↓
sealPendingNoteChangesToEditPasses  (wrap / stop) — clears pending
    ↓
companion Length EditPass           ← durable resolver store (if Shorten was pending)
    ↓
rebuildOverdubSourceView            (from=win, prep=0 on this pin)
    ↓
gatherActiveResolvedEvents(passes)
    per layer: applyActiveEdits (all Active Note editPasses)
    mergeSortedMidiVectors
    reconstructDisplayNotes (wrapPairing false)
    appendOverdubPassWrapPairedNotes (per-pass, wrapPairing true, same edits)
    ↓
overdubSourceViewNotes_ cache       ← occupy source-view covering identities
```

Firmware wrap/stop never applies pending onto the cache. Display paint overlay is not occupy's source.

#### Which store held L2324 `6079 144–168`?

**The source-view cache**, produced by reconstruct of the rebuild gather — **not** sealed Length 167.

| Candidate | Verdict at L2324 |
|---|---|
| Pending Shorten | **No.** Wrap seals then `clearPendingNoteChanges` before rebuild (`commitOverdubWrapAtSessionStart`). Occupy does not overlay pending. |
| Companion Length `endTick=167` | **Not in gather output.** Pitch-24 `mmevt` is On@144, On@168, Off@168, Off@264, Off@360. Off@167 would sort between On@144 and On@168; it is absent. Native Length replay writes Off@119 for causing 120 (`test_seal_pending_shorten_to_edit_pass_after_overdub_publish`). |
| Source-view cache `ae=168` | **Yes.** `mmspan` `s=144,e=168,id=6079`. `DisplayNote.endTick` is the Off tick. Matches committed Off@168. |
| Display paint overlay | **Not occupy.** `collectOverdubSourceHoldParticipantIds` walks the cache only. |

`resolvedEventLess` orders `midi::NoteOff (0x80)` before `midi::NoteOn (0x90)` at equal tick. Reconstruct LIFO: On@144, then Off@168 closes 6079 as `144–168`; On@168 then Off@264 closes 6073 as `168–264`. That is the L2324 cache **without** Length 167.

Do not treat L2324 `144–168` as proof the resolver ran or that a Length row was sealed. Covering language `[144,168)` and Off tick 167 are different; this pin's cache is Off@168.

Wrap `DIAG,lcr,src` L2271 (`from=win,prep=0`) still precedes L2324 `144–168`. First loss: wraps L2502 / L2616 / L2772 → L2827 `144–360`. At L2827 Off@168 is gone from `mmevt`; Off@360 remains.

Later L3555 `144–311` with Off@311 in `mmevt` is Length-style Off (`causingStart 312 − 1`). Gather **can** show a shortened Off tick later in the session. It did **not** at L2324 or at the L3298 FAIL.

Stop `#CAP,DIAG,overlap_hold` `shorten=0` is the **last pending snapshot** (wraps already sealed and cleared). It does not prove no Length was ever sealed.

Capture does not dump companion rows. Absence of Off@167 in the L2324 pitch-24 merged dump is the pin evidence that Length 167 was not applied to that gather.

#### Rebuild gather consume (exact)

`Loop::rebuildOverdubSourceView` when `tryResolvePreparedWindow` misses (`prep=0`):

1. `LoopContentResolution::resolveWindow(passes, …)` → `gatherActiveResolvedEvents`
2. Record pass layer + each active overdub pass layer: `appendCapturePassLayer` then `applyActiveEdits` (`applyNoteEditPassSequence` on **that layer only**)
3. `mergeSortedMidiVectors`
4. `NoteUtils::reconstructDisplayNotes(events, loopLen, false, false)`
5. `appendOverdubPassWrapPairedNotes` (geometry match, not `noteId`; can duplicate B)

121141 wrap lines: `from=win`, `prep=0`. Device-gate phase starts at L5404, after the occupy FAIL.

### Gate 2 — Identify why the rebuild gather loses/replaces that row — **PINNED**

Gate 1 named the L2324 row: cache reconstruct of committed Off@168, not Length 167. Gate 2 is why **Off@168 is gone** by L2827 and 6079 reconstructs as `144–360` (pairs to Off@360). Do not treat this as “Length 167 was dropped from the cache.”

#### First observed loss

No occupy `mmevt` between L2324 and L2827. Three wraps, all `from=win` `prep=0`:

| Wrap | `DIAG,lcr,src` | ev | notes |
|---|---|---|---|
| L2271 (before L2324 `144–168`) | still has Off@168 at next occupy | 74 | 38 |
| L2502 | no occupy dump | 78 | 41 |
| L2616 | L2634 MATCH `n=2 a=2` `as=504 ae=599` at `hs=552` — no `mmevt` | 84 | 46 |
| L2772 | first shrinking wrap | **82** | **41** |

L2827 is the first occupy dump after L2772. Pitch-24 `mmevt`: Off@24, On@144 `6079`, On@168 `6073`, Off@264, Off@360, On@408, On@504, Off@551. Off@168 would sort between On@168 and Off@264; it is **gone**. Dump cap is 8; the 1-bar filter cannot hide it (`occupyMismatchTickDistance` max on a 768-tick loop is 384). Playback `playback_build` after that wrap is full-loop `0,768`.

L2634 (after L2616, before L2772) still has covering `504–599`. L2827 has `6040 504–551` and `6033 408–551` plus Off@551 (`552 − 1`). Length 551 for causing 552 is first visible at the same dump as Off@168’s absence. That Shorten is **not** the LIFO owner of Off@168 on the combined stream (Off@168 closes 6073, Off@600 closes 6040, Off@743 closes 6033).

#### Deleted, not moved — **refined by Gate 4**

L2827 dumps **one** Off@360 and **one** Off@551. Gate 2 treated that as deletion because a move to 551 would duplicate Off@551. Occupy dumps the first **8** pitch-24 events only (`TICKS_PER_BAR=768` never filters on this loop). Native Length `6073` → 551 moves Off@168 to 551; a second Off@551 from `6040`/`6033` is the 9th event and is **capped**. Off@168 was **moved**, not deleted.

#### Named step

```
commitOverdubWrapAtSessionStart  (wrap L2772)
  → sealPendingNoteChangesToEditPasses
  → rebuildOverdubSourceView (from=win, prep=0)
       gatherActiveResolvedEvents
         per layer: applyActiveEdits → applyNoteEditPassSequence
         mergeSortedMidiVectors          (tick-only; equal tick = earlier pass first)
       reconstructDisplayNotes           (vector order; does not call sortMidiEventsChronologically)
       appendOverdubPassWrapPairedNotes  (geometry match; can add B 168–360, cannot replace 144–168 with 144–360)
```

`uniqueIdentifiedResolvedEvents` is TickIndex / prepared only (`from=prep`). Not this pin (`from=win`). Q16 min-length is skipped on `CommitReason::OverdubWrap`. Wrap `CLN` `wrap_synth=0` on the wrap before L2324; later wraps in this window emit no `CLN` lines. No `Overdub undone` between L2324 and L2827 — the pass was not disabled.

Playback `mmevt` is committed-only `copyEffectiveCommittedEventsInRange` (concat active chunks, tick-sort, **then** `applyNoteEditPassSequence` on the combined list). Source-view gather applies edits **per layer** then merges. Both outputs lack Off@168 at L2827, so the layer that stored Off@168 also had an On that `findNoteOffForOnIndex` paired to it when `applyNoteEditPassSequence` ran. Off@168 was not a per-layer orphan that only combined-stream pairing could see.

Chunks are immutable. The only function on this path that removes an untagged Off from gather output is `applyNoteEditPassSequence` (`applyDeleteNoteById` Hide/Delete or lengthen contained-delete; `shortenNoteEndById` would **move** the tick). Capture does not dump companion rows. **Which sealed row** at L2772 deleted Off@168 is not named. Do not treat Length 551 on 6040/6033 as that row.

After that gather, `reconstructDisplayNotes` LIFO-pairs On@144 `6079` to Off@360 → cache `144–360`. `appendOverdubPassWrapPairedNotes` is not the A replacement (pitch+start+end match; `144–168` vs `144–360` are different geometries). L3298 `6073 168–360` beside `168–264` is wrap-pair add of B.

#### Excluded

| Candidate | Verdict |
|---|---|
| Length 167 dropped from cache | **No** — Gate 1: Length 167 was never in the L2324 gather |
| `uniqueIdentifiedResolvedEvents` | **No** — not on `from=win` |
| Q16 min-length / `wrap_synth` | **No** — skipped / 0 |
| Playback window clip | **No** — `0,768` |
| `appendOverdubPassWrapPairedNotes` replacing A | **No** — appends missing geometry only |
| Pass disable / undo | **No** — still OVERDUBBING; no undo in this window |

Until Gate 3 names B’s committed Off after this gather, do not write a hold-312 occupy oracle.

### Gate 3 — B's committed geometry (consequence of Gates 1–2) — **PINNED**

After wrap L2772 gather, B's **committed** On/Off pair is On@168 `6073` + Off@264.

| Representation | L2324 | L2827 | L3298 `hs=312` |
|---|---|---|---|
| Merged reconstruct B | `168–264` | `168–264` | `168–264` `p=0` |
| Wrap-pair B | not dumped | **absent** (would cover 552; `a=0`) | `168–360` `p=1` (first dump) |
| `mmevt` Off@264 | present | present | present |
| `mmevt` Off@360 | present (A's original) | present | present |
| Ledger | — | — | `n=1` `lid=6079` `lst=144` `ltick=312` `cu=0` |

**Resolver input** when Off@264 was captured: `accumulatePendingNoteChangesForIncomingNote` `[prior.tick=168, newEvt.tick=264)`. That is the closed incoming B. Add pending is overlay only; B's durability is the capture pass (On@168 + Off@264).

**Clock, not reconstruct, names which Off closed B at the FAIL.** `didPlaybackEventCross`: `prevTickInLoop < evTick && evTick <= tickInLoop`. At occupy 312, Off@264 is already applied; Off@360 is not (`360 > 312`). Untagged Off LIFO pops newest pitch-24 Entry: with Off@168 gone (Gate 2), stack is `6079` then `6073`; Off@264 removes `6073`. `6079` stays open until Off@360. Matches `lid=6079`.

**Wrap-pair `168–360` is not B's committed end.** `appendOverdubPassWrapPairedNotes` reconstructs each overdub pass with `overdubPassWrapPairing=true` and appends pitch+start+end not already present. `168–360` ≠ `168–264`, so both rows exist. First appear after wraps L2959 / L3146 (L3146 notes 40→44). Merged reconstruct never replaces `168–264` with `168–360` while Off@264 remains.

Later L3555 `168–311` / Off@311 is a **later** Length (`312 − 1`) on the same lane after the FAIL. Not B's committed Off at L3298.

**Hold 312 occupy reading**

| Source-view at 312 | Occupy |
|---|---|
| A DisplayNote `144–167` (Length 167) and wrap-pair not added | `n=1 a=0` — A open in ledger; covering is `s < 167`; B `168–264` covering is `s < 264` |
| A DisplayNote `144–168` (L2324 Off@168 pairing) and wrap-pair not added | still `n=1 a=0` — covering is `s < 168`; 312 is still outside |
| A `144–360` (Gate 2) and wrap-pair `168–360` added | `n=1 a=2` — L3298 FAIL |
| “312 = B only” (`a=1` `6073`) | **Withdrawn** — committed B does not cover 312 |

Do not write a native hold-312 fixture that expects B covering 312. Firmware still blocked.

### Gate 4 — Which EditPass made committed `Off@168` disappear — **PINNED**

Do **not** investigate “which edit caused A to become `144–360`.” That pairing is downstream.

**Exact question:**

> Which EditPass operation caused the committed `Off@168` event to disappear from the per-layer / combined gather output?

`resolveConstrainedGeometry` remains the sole **overlap-geometry decision authority**. Gate 4 separately determines whether the event/EditPass representation carrying that decision survives replay.

**Pairing disagreement (native, L2324 dump order On@168 then Off@168):**

| Consumer | Equal-tick 168 order | Who owns Off@168 |
|---|---|---|
| `reconstructDisplayNotes` / `resolvedEventLess` | Off before On | `6079` (A) → cache `144–168` (Gate 1) |
| `findNoteOffForOnIndex` in `applyNoteEditPassSequence` | vector On then Off | `6073` (B) |

Playback `copyEffectiveCommittedEventsInRange` tick-sorts then applies edits on the **combined** list. That output also lacks Off@168 at L2827, so the killer must work on this tick-sorted combined pairing — not only an unsorted per-layer steal.

**Native input→output** (`test_121141_gate4_off168_disappears_from_length_of_6073`, `test_121141_gate4_per_layer_6040_cannot_steal_off168_from_6073_layer`, `test_121141_gate4_tick_sorted_early_layer_length_6040_leaves_off168`):

| Layer | EditPass row | targetNoteId | kind | operation | Off@168 in | Off@168 out |
|---|---|---|---|---|---|---|
| tick-sorted combined / later layer with On@168 `6073` | Length | **6073** | Length | `applyChangeLengthById` LIFO-pairs Off@168 to `6073` and **moves** it | yes | **no** |
| same | Length `endTick=167` | 6073 | Length shorten | Off@168 → **167** | yes | no (Off@167 appears) |
| same | Length `endTick=264` | 6073 | Length lengthen | Off@168 → 264 | yes | no (**two** Off@264) |
| same | Length `endTick=551` | 6073 | Length lengthen | Off@168 → **551**; Off@264 and Off@360 stay; Ons stay | yes | no |
| same | Delete | 6073 | Hide/Delete | removes On@168 **and** Off@168 | yes | no |
| same | Length 167 | **6079** | Length shorten | moves Off@264, **not** Off@168 | yes | **still yes** |
| same | Length 551 | **6040** or **6033** | Length shorten | moves that note’s later Off | yes | **still yes** |
| tick-sorted early layer (Off@168 before On@504) | Length 551 | 6040 | Length shorten | cannot own Off@168 (Off tick is before that On) | yes | **still yes** |

Then:

```
Off@168 removed because:
    Length EditPass
    targeting 6073
    performed applyChangeLengthById (findNoteOffForOnIndex pairs Off@168 to On@168)
    during applyNoteEditPassSequence on the tick-sorted combined stream
    (same pairing as the later layer that contains 6073)
```

**Payload vs L2827 dump** (`TICKS_PER_BAR=768`, so the occupy 1-bar filter never hides on this loop; only the **8-event cap** truncates):

| Length `6073` payload | Native result | L2827 |
|---|---|---|
| 167 | Off@167 between On@144 and On@168 | Off@167 absent → **excluded** |
| 264 | two Off@264 | one Off@264 in the first 8 → **excluded** |
| 551 | Off@168 gone, one Off@551, Off@264/360 stay, both Ons stay | matches; a second Off@551 from `6040`/`6033` Length would be the 9th event and **capped** |

Delete `6073` is excluded: L2827 still has On@168 `6073`.

121141 still does not dump companion rows. The **operation class** is named. The device EditPass index is not. Do not treat wrap-pair `168–360` overlapping hold 552 as proven seal identity for `Length(6073, 551)` — it is consistent with causing 552 (`551 = 552 − 1`) but not dumped.

| If Gate 4 shows | Then |
|---|---|
| Length targeting `6073` moves Off@168 (this pin) | **5A:** EditPass replay pairing, not a missing Length-167 store for A. `Length(6079, 167)` does **not** remove Off@168. **5B:** Length `6073` 551 effect is separate; provenance not proven |
| Unrelated Hide/Delete | Excluded — would drop On@168 |
| `Length(6079, 167)` survives and rebuild still LIFO-pairs raw MIDI | Handoff/representation — **not** this Off@168 loss; Point 3 still requires that sealed row |

Device EditPass index is still not dumped. Gate 5A/5B design against the **named operation class** (Length of `6073`, dump-consistent payload 551) and the proven equal-tick pairing disagreement. It does not invent a source-view store.

### Gate 5 — Split (firmware blocked)

**Slice 1 is implementation-ready in isolation. Slice 2 is not.** Do not treat “Gate 5” as one inseparable patch.

```
Gate 5A — EditApply pairing
    Proven cause
    Existing owner
    Native proof
    Shipped: findNoteOffForOnIndex equal-tick Off-before-On
    Not a full tick sort

Gate 5B — Length(6073, 551) provenance / effect
    Effect proven
    Source/provenance NOT proven
    No implementation permitted
```

**Exact question:**

> Which existing-owner changes allow the sealed Length(`6079`, 167) to survive into rebuild, while preventing EditApply from consuming Off@168 under the wrong identity, so that rebuild reconstructs A as `144–167` and B as `168–264`?

Rebuild **cannot manufacture** Length-167. Point 3 (`144–167`) is only possible if Point 2 sealed `Length(6079, 167)` exists and EditApply applies it to Off@168 owned by A, not by B.

```
resolver produces Shorten(6079, 167)
        ↓
seal produces Length(6079, 167)
        ↓
EditApply must preserve/use that row correctly
        ↓
reconstruct sees the resulting event geometry
        ↓
A = 144–167
B = 168–264
```

Not: which new cache representation. Not: patch occupy collect. Not: call `resolveConstrainedGeometry` again inside `rebuildOverdubSourceView`. Not: make `rebuildOverdubSourceView` create resolver geometry.

**Native** (`test_121141_gate5_off_before_on_length_6073_551_leaves_off168_reconstruct_b_168_360`, `test_121141_gate5_off_before_on_length_6079_167_shortens_a_off168`):

| Input order | Length row | Off@168 | Off@264 | Reconstruct A | Reconstruct B | Covering at 312 |
|---|---|---|---|---|---|---|
| L2324 On then Off, **pre-5A** | `6073` 551 | moved to 551 | stays | `144–360` (Gate 2 device) | — | A covers |
| L2324 On then Off, **5A shipped** | `6073` 551 | stays | moved to 551 | `144–168` | `168–360` | A no; **B yes** |
| L2324 On then Off, **5A shipped** | `6079` 167 | → 167 | stays | `144–167` | `168–264` | **neither** |
| Off before On | `6073` 551 | stays | moved to 551 | `144–168` | `168–360` (leftover Off@360) | A no; **B yes** |
| Off before On | `6079` 167 | → 167 | stays | `144–167` | `168–264` | **neither** |

**5A** is **necessary** (A stops pairing to Off@360) and **not sufficient** for intended hold-312 `a=0` while Length `6073` 551 still applies. After Off-before-On, that Length lengthens B’s committed Off@264 to 551; reconstruct then pairs `6073` to leftover Off@360 → DisplayNote `168–360`, which covers 312. Gate 3 committed B is Off@264 (`covering s < 264`); 312 is not in that interval.

Off-before-On plus sealed Length `6079` 167 produces the invariant DisplayNotes (`6079` `144–167`, `6073` `168–264`) and neither covers 312. Gate 1: that Length row is **absent** from L2324 pitch-24 `mmevt` (no Off@167). Gate 4 On-then-Off order cannot apply it to Off@168 (it would move Off@264). After 5A, that row **would** own Off@168 — only if Point 2 sealed it.

Do not treat wrap-pair `168–360` as proven seal identity for Length `6073` 551. Gate 4 named the operation class vs L2827; the device row index is not dumped.

#### Gate 5A — equal-tick apply pairing (**shipped**)

`findNoteOffForOnIndex` walks **vector order**, except at an **equal tick** it processes Off before On (`NoteUtils::midiEventChronologicalLess`, same keys as `sortMidiEventsChronologically` / reconstruct / playback clock). Tick-sorted gather is On then Off at 168; before 5A, Length of `6073` owned Off@168. After 5A, Off@168 belongs to `6079` even when the vector is On then Off (`test_121141_gate5a_on_then_off_length_pairing`).

Do **not** sort the whole stream by tick. Capture stores sequential pairs (`On, Off, On, Off`). A full tick sort nests them (`On, On, Off, Off`) and Length of the later note steals the earlier Off — native `test_pre_commit_order_overlap_changes_before_move_and_pitch` failed under that walk and passed once equal-tick-only was restored.

Do not mutate the vector: `applyChangeLengthById` lengthen already ends with tick-only `stable_sort`, which restores On-before-Off for later rows. Encoding the equal-tick rule in the pairing primitive covers those rows.

This slice is **global**: NOTE_EDIT Length/Delete also call `findNoteOffForOnIndex`. Nested-pitch fixtures have no equal-tick On/Off pair; wrap 195941 Off@96 then On@2592 is unchanged (different ticks, vector already Off first).

5A does not finish hold-312 `a=0` if Length `6073` 551 still applies, and it does not produce A `144–167` unless Point 2 sealed `Length(6079, 167)`. When that row is present, 5A applies it to Off@168 on On-then-Off dump order.

#### Gate 5B — Length `6073` 551 provenance (no implementation)

Effect proven: after 5A pairing, Length `6073` 551 still applies; B reconstructs `168–360` and `displayNotePresentAtHold(168, 360, 312, 768)` is true. Intended occupy at 312 is `a=0`.

Source/provenance **not** proven. Do not treat the observed consequence as the reason the row exists. **Forbidden:** skip/filter Length by `targetNoteId` or payload (for example `if (targetNoteId == 6073) skipLength();`) without a named pending/EditPass owner showing that row’s source. Do not design 5B as “wrap-pair intake.”

#### What Gate 5 does not pick

- A new source-view representation
- A second Shorten pass on `overdubSourceViewNotes_`
- Patching `collectOverdubSourceHoldParticipantIds`
- Calling `resolveConstrainedGeometry` inside rebuild
- Making rebuild create Length-167 without a sealed `Length(6079, 167)` row
- A 5B skip/filter on Length `6073`

#### 5A vs 5B outcomes (not a menu to implement 5B)

| If | Proven native outcome | Hold-312 `a=0` | A `endTick=167` invariant |
|---|---|---|---|
| 5A only, Length `6073` 551 still applies | A stays `144–168`; B becomes `168–360` | **no** (B covers) | **no** unless Point 2 also sealed `Length(6079, 167)` |
| 5A + Point 2 `Length(6079, 167)` present, Length `6073` 551 absent | A `144–167`, B `168–264` | **yes** | **yes** |
| 5A + Length `6073` 551 stopped, Point 2 still absent | A `144–168`, B `168–264` | **yes** (A covering `s < 168`) | **no** |

The middle and last rows describe **what 5B would have to prove**, not an approved patch. Occupy `a=0` at 312 does not by itself require 167 vs 168 for A. Point 3 `endTick=167` **does** require sealed `Length(6079, 167)`.

Do not add a second Shorten pass on `overdubSourceViewNotes_`. Do not recreate overlap geometry by pairing raw MIDI again inside `rebuildOverdubSourceView`.

### Gate 6 — Same-tick wrap continuity (follow-up, not this RC)

See [Follow-up](#follow-up--same-tick-onoff-at-the-loop-wrap). An agent must **not** treat this as in-scope while fixing the source-view handoff.

---

## Tests

**Gate 4 native (historical, pre-5A):** Length of `6073` on L2324 On-then-Off order removed Off@168. That matrix is the cause pin, not the post-5A oracle.

**Gate 5A native (shipped):** `test_121141_gate5a_on_then_off_length_pairing` — dump order no longer needed. Length `6079` 167 on On-then-Off reconstructs A `144–167` / B `168–264`. Length `6073` 551 leaves Off@168 and moves Off@264. `test_pre_commit_order_overlap_changes_before_move_and_pitch` remains green (equal-tick-only walk; full tick sort failed that fixture).

**Gate 5B effect native (no firmware):** `test_121141_gate5_off_before_on_length_6073_551_leaves_off168_reconstruct_b_168_360`. After 5A pairing, Length `6073` 551 reconstructs B `168–360` (covers 312).

**Hard diagnostic assertion** (three-point proof, when firmware is approved). Identity is mandatory. Do not assert only “some shortened pitch-24 note.” Gate 4 showed Off@168 loss is Length of `6073`, not Length of `6079`. Point 3 cannot pass merely because some pitch-24 note got shortened. Rebuild cannot produce `endTick=167` unless Point 2 sealed `Length(6079, 167)`.

```
Point 1 — resolver
    targetNoteId = A (6079)
    pending kind = Shorten
    payload endTick = 167

Point 2 — seal
    same targetNoteId = 6079
    EditPass survives
    payload endTick = 167

Point 3 — rebuild
    source view contains A noteId = 6079, startTick = 144, endTick = 167
    source view contains B noteId = 6073, startTick = 168, endTick = 264
```

| If this fails | The bug is |
|---|---|
| Point 1 never appears | Resolver / commit trigger (B still open, or Off never reached accumulate) |
| Point 1 passes, Point 2 fails | Seal / EditPass persistence — not `rebuildOverdubSourceView` |
| Points 1+2 pass, Point 3 fails | Rebuild handoff / a later Length consuming the wrong Off (5A pairing shipped; remaining is 5B or missing Point 2 row) |
| Point 3 passes, HITL still fails | Remaining occupy / source-view covering-identity semantics — not this three-point |
| Point 1 holds, cache still unshortened, Point 2 holds | Pending is not applied to the cache; rebuild must consume the sealed row |

121141 does not dump companion rows, so this native fixture must show `targetNoteId` + `endTick` after `sealPendingNoteChangesToEditPasses`.

What is already fixed regardless of B's end:

- A must **remain present** in the source view with its resolved (shortened) geometry — the fix is never "drop A".
- Identity-without-NoteOn fixtures stay green. Do not change `isPlaybackCatchUpWindow(88,88)==true`.
- HITL after flash: one open ledger identity vs two source-view covering identities **0**; zero open ledger identities vs one source-view covering identity **0**; `led` equals open-ledger-identity count; nested two-and-two not a fail. Open ledger identities and source-view covering identities differing by design is allowed.

**Hold 312 occupy reading is named (Gate 3):** `n=1` is A open until Off@360. Intended covering with Length-167 A (`ae=167`, `s < 167`) and no wrap-pair is `a=0`. Do not expect B covering 312. HITL after flash still wants one-vs-two covering **0** on this FAIL shape; that is A `144–360` plus wrap-pair `168–360`, not “B should cover 312.”

---

## Follow-up — same-tick On/Off at the loop wrap

**Not this RC.** Keep strictly after the current source-view handoff.

**Requirement (user):** when a NoteOn and NoteOff land on the **same tick** after a wrap, hide all overlapping notes contained strictly inside the joined span and resolve the result as **one continuous note**.

**Scope:** the **loop wrap** only (`loopLengthTicks`). Overdub wrap commit at session start `S` is out of scope.

What already exists:

| Piece | Behavior |
|---|---|
| `NoteUtils::isWrappedLoopNotePair(onTick, offTick, loopLength)` | Tail On + head Off is one note. Requires `offTick < onTick` **strictly**, plus gap `> loopLength / 2` |
| `LoopContentResolution::notePresentAt` | For a wrapped pair, present when `tick >= startTick || tick < endTick` |
| `resolveConstrainedGeometry` → `CompleteCover` → Hide | Existing "hide overlapping note" owner — containment is `targetStart >= causingStart && targetEnd <= causingEnd` |
| Linearization | `projectNoteBaselineForEditAnalysis` → `projectEditLinearSpan` |

**Gap:** `isWrappedLoopNotePair` returns false when `offTick == onTick`.

**Must not reopen:** clock equal-phase Off before On in `rebuildPlaybackOrder` (HITL **PASS** [`001021`](../../captures/session_20260819_001021.log)). Wrap continuity is geometry recognition, not clock event ordering.

**Must not:** widen `isWrappedLoopNotePair` as a first move. It is shared by `notePresentAt`, `checkNoWrappedPairStorage`, `CaptureIncrementalSanity`, `TrackDeferredMaintenance` stored-pair verification, and the canonical resolution fixture.

---

## Out of scope

- A second Shorten algorithm on `overdubSourceViewNotes_`
- Patching `collectOverdubSourceHoldParticipantIds` to drop A
- `ActiveNoteLedger` / option B / Off stamp / FIFO
- Identity-existence filter / `isPlaybackCatchUpWindow`
- Occupy catch-up / hot-path (`TrackPlaybackHotPath.cpp`)
- `NoteGeometryResolver` / LCR ownership, including forcing device-gate prepared during overdub as this RC
- Same-start nested On@48 (6019/6025)
- Prepared `eq=1` / `b=0` miss this session
- Same-tick wrap continuity (follow-up above)
- Hold-312 occupy oracle that expects B covering 312 (“312 = B only”)
- Changing inclusive `classifyEditSessionInteraction` to half-open consume overlap
- Persisting `PendingNoteChangeKind::Add` as an EditPass
- Applying pending onto the source-view cache from occupy / `collectOverdubSourceHoldParticipantIds`
- **Gate 5B:** skip/filter Length by `targetNoteId` or payload; treat wrap-pair as proven seal identity for Length `6073` 551
- Full tick-sort of `findNoteOffForOnIndex` walk (nests capture On/Off pairs)

---

## Pre-implementation review

### Ready

- Pin L2324 → L2827 / L3298, wrap `from=win,prep=0`, Off@168 gone by L2833.
- Losing function: `Loop::rebuildOverdubSourceView` merged `reconstructDisplayNotes` after `clear()`.
- **Gate 1:** L2324 `144–168` is cache reconstruct pairing to committed Off@168, not sealed Length 167. Gather consume: `gatherActiveResolvedEvents` + per-layer `applyActiveEdits` + reconstruct + `appendOverdubPassWrapPairedNotes`.
- **Gate 2:** Off@168 gone from wrap L2772 gather `applyActiveEdits` output. Reconstruct then pairs 6079 to Off@360. Gate 4: moved by Length of `6073`, not deleted; second Off@551 is dump-capped.
- **Gate 3:** B's committed pair is On@168 `6073` + Off@264. Wrap-pair `168–360` first dumped at L3298. Hold 312 intended `a=0` if A is Length-167 (`s < 167`); “312 = B only” withdrawn.
- **Gate 4 pinned:** Length `targetNoteId=6073` moved Off@168 **before 5A**. `Length(6079, 167)` did not. Apply On-then-Off vs reconstruct Off-then-On disagreed at tick 168.
- **Gate 5A shipped:** `findNoteOffForOnIndex` equal-tick Off-before-On. Not a full tick sort. On-then-Off Length `6079` 167 reconstructs A `144–167`.
- **Gate 5B:** effect proven (B `168–360` covers 312 after 5A + Length `6073` 551). Provenance not proven. **No implementation.**
- **Coordinate conversion pinned:** Length/Off/`DisplayNote.endTick`/`ae=` = 167; canonical `[144,168)` only inside reconstruct; occupy covering `s < 167`.
- **5B / HITL still open.** 5A native shipped; device HITL not run.
- Existing overlay: `applyPendingHideAndShortenToNotes` on **display paint** (`applyPendingNoteChangesToDisplayNotes`). Firmware wrap/stop does **not** call `applyPendingNoteChangesToOverdubSourceView`.
- Evaluation catalogs written (overlap-resolve + ledger).

### Resolved (user / code)

| Topic | Decision |
|-------|----------|
| Invariant | Preserve A's Length-167 DisplayNote `144–167` (`noteId` 6079). Rebuild must not restore A's pre-resolution `144–360`. Do not equate that DisplayNote with exclusive `[144,168)` or L2324 `ae=168` |
| Overlap commit time | At the later note's **NoteOff** — proven, two call sites |
| Open ledger identities vs source-view covering identities | Allowed to differ |
| Second Shorten | No |
| Prepared spans this pin | Not the handoff (`prep=0`) |
| Occupy collect | Do not patch |
| Per-pass wrap reconstruct as B's authority | **Withdrawn** — pairing fallback, can run ahead of resolver commit |
| Open B | Not a committed sounding/resolved span; remains live capture and is carried across wrap |
| Synthesized Off at record/overdub stop | **Valid close** when no performer Off arrived |
| Restore of a note without an Off | **Guardrail only** — do not build the fix on it |
| Gate order | Gate 1 blocker; Gate 2 named the gather loss; Gate 3 is B's committed end; Gate 4 names Length `6073` |
| Hold-312 fixture | Named: do **not** expect B covering 312. Intended `n=1 a=0` if A is shortened and wrap-pair is absent |
| Same-tick wrap | Follow-up, not this RC |
| Coordinate conversion | **Pinned** — Length/Off/`DisplayNote.endTick`/`ae=` = 167. Canonical exclusive `[144,168)` only inside reconstruct. Occupy covering `s < 167`. Tick 167 is the disagreement |
| Geometry vs event survival | `resolveConstrainedGeometry` remains the sole overlap-geometry **decision** authority. That does not guarantee the event/EditPass carrying the decision survives replay (this bug) |
| Single-owner audit question | **Superseded.** Gate 4 is: which EditPass made committed `Off@168` disappear from **per-layer** gather output. Do not investigate “which edit caused A `144–360`” |
| Gate 4 | **Pinned** — Length `6073` moves Off@168 (`168` → dump-consistent `551`). `Length(6079, 167)` / `6040` / `6033` do not. Delete `6073` excluded (On@168 remains). Device row index not dumped |
| Gate 5 | **Split.** **5A shipped** — equal-tick Off-before-On in `findNoteOffForOnIndex` (`midiEventChronologicalLess`); not a full tick sort. **5B** Length `6073` 551: effect proven, provenance **not** proven, **no implementation**. Rebuild cannot manufacture Length-167; Point 3 requires sealed `Length(6079, 167)` |
| B's end at the pin (264 vs 360) | **Pinned** — committed Off@264. L3298 `168–360` is wrap-pair add |
| Gate 3 | **Pinned** — On@168 + Off@264; wrap-pair is not B's end |
| Occupy `as=`/`ae=` | Source-view **cache**, no pending overlay. L2324 `ae=168` is Off@168 pairing, not Length payload 167 |
| Sealed Length `endTick` | Off tick `causingStart − 1` (native 120 → 119). **Not applied** in L2324 pitch-24 `mmevt` (no Off@167) |
| Add pending | Overlay only; B persists via capture pass, not companion Create |
| Gate 1 | **Pinned** — L2324 `144–168` is Off@168 pairing, not Length 167 |
| Gate 2 | **Pinned** — Off@168 gone after wrap L2772 `applyActiveEdits`; reconstruct pairs 6079 to Off@360. Wrap-pair is not the A replacement. Gate 4 names the move |

### Open after 5A

1. **5B remains investigation.** Device EditPass index is still not dumped. Do not skip/filter Length by identity.
2. Native three-point fixture with identity remains a later firmware-gate artifact. Point 3 requires Point 2. Hold-312 fixture must not expect B covering 312.
3. HITL after 5A flash: A `144–360` should stop if Length `6079` 167 is sealed; hold-312 `a=2` can remain from Length `6073` 551 / wrap-pair (5B).

### Proceed?

**5A: done (native).** **5B: NO.** Do not implement a Length skip. HITL is a device gate, not a 5B patch.
