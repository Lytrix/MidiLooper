# NOTE_EDIT exit during overdub must keep the live piano roll

**Status:** RC1+RC2 device PASS [`223532`](../../captures/session_20260909_223532.log)  
**Kind:** bugfix  
**Evidence:** [`session_20260909_214900.log`](../../captures/session_20260909_214900.log) (bug), [`session_20260909_223532.log`](../../captures/session_20260909_223532.log) (fix)  
**Does not reopen:** DEC-056 stale-parent publish skip, replace-on-retrigger, compose `STIM-NE-OD-COMPOSE-01` / `02` / `03` / `04`, occupy hide/ledger, OpenSpec `3.5` / `3.6`, wrap `004351` dirty→last-snapshot rule. Persist of a parked mover is a sibling RC ([`note_edit_exit_persist_selected_mover_bugfix.md`](note_edit_exit_persist_selected_mover_bugfix.md)), not this display owner.

## Debugging boundary

```text
NOTE_EDIT projection (visual 65, DFRAME 64)
    ← trust; in-edit paint is the live roll
closeNoteEditPass + DEC-056 skip stale-parent publish
    ← keep; do not publish onto root_rev=3
cycleEditSession → LOOP_EDIT + invalidateCaches
resolveDisplayNotesByDrawSource stale-snapshot gather (snap=0, preview=0, frame=56)
    ← RC1
Track::stopPlaying emitDisplayCaptureSnapshot on every track
    → hop-fills liveDisplayNotes with 6144/215 then 50688/352
    ← RC2
OVERDUBBING→STOPPED adopt_partial notes=87 on loop 2304
    ← trust once RC1/RC2 hold the selected compose
```

## Problem

[`214900`](../../captures/session_20260909_214900.log) short-press Edit Mode at `1520.962` while **OVERDUBBING**. Close:

- `NoteEditPass close keep committed editPass=0 rows=3 (no display-diff replace)`
- `NoteEditPass close skip stale-parent publish rows=3`
- snapshot stays `root=0x7033a884`, `root_rev=3`, `pending=1`

Last in-edit frame is `DFRAME,64`. After LOOP_EDIT enter, visual cache is still **65** (`VCACHE` / `lcr,srcspan,count=65`). First LOOP_EDIT overdub paint:

```
draw_src,layers,visual,65,preview,0,sealed,0,snap,0,frame,56,hidden,9
DISP,0,OVERDUBBING,2304,56,65,56,56
```

Ids **59–66** (pitch **49**) go `new,0,0,0`. `DFRAME` stays **56** until stop.

Transport stop at `1529.968` then paints other loops on the roll before the composed 3-bar loop:

| After | DISP | Loop | Notes |
|---|---|---|---|
| first `PLAYING→STOPPED` | `DISP,5` | 3072 | 105 |
| next | `DISP,0` | **6144** | **215** |
| next | `DISP,1` | 12288 | 352 |
| `OVERDUBBING→STOPPED` | `DISP,0` | **2304** | **87** |
| later | `DISP,0` | 3072 / **50688** | 42 / **352** (visual **1429**) |

Stop commit on the overdub loop is `VCACHE,adopt_partial,notes,87` then `lcr,src,why=stop,notes=87`. That is the composed loop. The 56-note view is not it. The 6144 and 50688 frames are other tracks / the leftover 66-bar dump already named in `Track::stopPlaying`.

## Why RC1 draws 56

`shouldGatherPublishedSnapshotForCompleteDrawSource` ([`DisplayWindowUtils.h`](../../include/Utils/DisplayWindowUtils.h)):

```text
if visualCacheReusable → false (use cache)
if publishedSnapshotIsCurrent → true
else return captureActive || overdubSessionActive
```

`EditManager::cycleEditSession` calls `track.invalidateCaches()` after close. That sets `visualCacheDirty`, so `visualCacheReusableForDrawSourceWindow` is false. Snapshot is not current (`snap,0`). Capture and overdub session are still active. Draw-source therefore gathers the **last published** snapshot (`root_rev=3`) → **56** notes.

`capturePreview` is empty (`preview,0`), so the live overlay cannot put ids 59–66 back. DEC-056 correctly refuses to publish the edit rows onto that stale parent.

**Retain the existing unpublished compose; do not reconstruct it from the stale snapshot.** The stale snapshot may still be gathered by the existing machinery. It must lose authority to replace a larger retained visual compose. Comparison is against **this draw-source call’s** stale gather only — not a new global “larger cache wins” rule, and not a change to wrap `004351`.

Do **not** flip DEC-056.

## Why RC2 shows another loop

`DisplayManager` piano roll resolves the selected track (`paintTrackIndex_` / `paintSlot_`). `Track::stopPlaying` still calls `emitDisplayCaptureSnapshot(*this, activeLoopIndex, …)` on **every** playing track. That helper runs `resolveDisplayNotes`, which hop-clears and **fills** the shared `liveDisplayNotes`.

`Track::stopPlaying` already documents this:

```text
Transport stop hops every playing track. Re-arm only the selected track so leftover
loops cannot replace the occupy first-STOPPED frame with a 352/1430-note dump.
```

Dump **arm** (`armStoppedFrameNoteDump`) stays selected-only and unchanged. The bug is the resolve **side effect**, not the hop and not the stopped-frame selection mechanism:

```text
non-selected stop
    → resolveDisplayNotes()
    → shared liveDisplayNotes
    → selected track's next paint consumes foreign vector
```

[`000013`](../../captures/session_20260901_000013.log) / [`display_visual_cache_stop_compose_adopt_bugfix.md`](display_visual_cache_stop_compose_adopt_bugfix.md) adopted the outgoing compose before hop-clear; the incoming foreign resolve still becomes the vector the next paint draws. `214900` `DISP,0,STOPPED,50688,352,1429` is that dump.

## Architecture checkpoint

| Question | RC1 | RC2 |
|---|---|---|
| Ownership change? | NO — retain unpublished compose at the draw-source call site | NO — selected track+slot is the only stop writer to `liveDisplayNotes` |
| State transition change? | NO — LOOP_EDIT + OVERDUBBING already | NO — transport stop already hops tracks |
| Unbounded interval owner? | NO — one call-site retain vs this gather | NO — skip or scratch resolve on non-selected tracks |

## Causal chain (one RC each)

```text
RC1: invalidateCaches → stale snapshot wins over unpublished compose
RC2: stopPlaying → non-selected resolve mutates shared liveDisplayNotes
```

Commit 1: NOTE_EDIT close retains unpublished overdub visual compose.  
Commit 2: non-selected transport-stop resolves cannot mutate selected live display.

## Proposed fix

### RC1 — retain unpublished compose after NOTE_EDIT close while capture is active

**Owners:** `EditManager::cycleEditSession` / `closeNoteEditSession`; `DisplayManager::resolveDisplayNotesByDrawSource`

Requirement:

> Retain the existing unpublished compose; do not reconstruct it from the stale snapshot.

1. Immediately after NOTE_EDIT close, while capture/overdub remains active, `visualCache` is the **authoritative starting display representation**. If close would drop it, adopt the last projected notes onto that cache first so it stays **65** (ids 59–66 remain).
2. The stale published snapshot is only a **fallback representation**. Existing machinery may still gather it. It must not replace a non-empty larger retained compose. The comparison is this draw-source call’s stale gather versus the retained `visualCache` — not a new global “larger cache wins” rule.
3. `capturePreview` / pending / sealed overlays then extend that retained representation normally.
4. Do not change `shouldGatherPublishedSnapshotForCompleteDrawSource` (wrap `004351` dirty→last-snapshot stays). Do not publish onto the stale parent (DEC-056 stays).
5. Retain is off when this gather has uncovered sealed preview (`170537` / wrap `004351`): snapshot+sealed prefix stays authority. `214900` sealed=0 so retain fires.

**Invariant (device / HITL):** After NOTE_EDIT → LOOP_EDIT while OVERDUBBING, first `draw_src,layers` `frame` ≥ last in-edit `DFRAME` (here **64**). The frame is not the stale published snapshot count (**56**) while `visual,65`.

**Native fixture (stronger):** dirty cache, stale published snapshot gather of 56, retained visual compose **65** including ids 59–66 → draw-source retain returns **65** (plus open tails only if overlays add them), not 56 and not 64. A 64-note result fails the fixture even if it satisfies `frame ≥ last DFRAME`.

**Observability:** existing `draw_src,layers` / `DISP`. Fail the session if first LOOP_EDIT OVERDUBBING `frame` == stale snapshot count while `visual > frame`.

### RC2 — selected track+slot is the only stop writer to `liveDisplayNotes`

**Owner:** `Track::stopPlaying` → `DisplayManager::emitDisplayCaptureSnapshot`

Required:

1. On transport stop, only the **selected** track+slot may `resolveDisplayNotes` into `liveDisplayNotes`.
2. Other tracks may emit `SC_DISP` from that loop’s own length / cache counts, or resolve into a scratch vector. They must not hop-fill the selected compose.
3. Keep `armStoppedFrameNoteDump()` selected-only and unchanged.

**Postcondition:**

```text
For every non-selected Track::stopPlaying:
    liveDisplayNotes identity/content is unchanged
```

Not merely “does not become the foreign loop.” Clearing then refilling a different vector fails.

**Invariant:** First selected-slot STOPPED `DISP` after this overdub is the overdub loop (**2304 / 87** here), not **6144 / 215** and not **50688 / 352 / 1429**.

**Native:** stop-hop fixture: selected compose already in `liveDisplayNotes` (2304 / 87); after non-selected `emitDisplayCaptureSnapshot`, same pointer and same contents.

## HITL

Do not edit compose `01` / `02` / `03` / `04`. Add verifier cases on a log that contains this close-while-overdub sequence (`214900`):

- `pending_note_edit_close_keeps_overdub_frame` — first LOOP_EDIT OVERDUBBING `frame` ≥ last in-edit `DFRAME` and is not the stale-snapshot shrink (`visual,65` → `frame,56`). Native fixture asserts **65**.
- `stop_selected_slot_not_foreign_loop` — first selected-slot STOPPED `DISP,0` after `OVERDUBBING→STOPPED` is the overdub `loopLength` (2304), not 6144 / 50688

Device gate is a recapture of this manual exit-during-overdub, or a later dedicated stimulus if compose cannot hit the close-while-overdub window without editing 01–04.

## Device recapture [`223532`](../../captures/session_20260909_223532.log)

Short-press Edit Mode at `453.984` while OVERDUBBING. Close keeps DEC-056 (`skip stale-parent publish`, `root_rev=3`, `pending=1`). Last in-edit `DFRAME` is **64**. First LOOP_EDIT paint:

```
draw_src,layers,visual,65,preview,0,sealed,0,snap,0,frame,65,hidden,0
DISP,0,OVERDUBBING,2304,65,65,65,65
```

`DFRAME` stays **65** through overdub stop (`OVERDUBBING→PLAYING` `2304/65`) and transport hop (`PLAYING→STOPPED` then `DISP,0,STOPPED,2304,65`). No `6144` / `50688` STOPPED frames. RC2 hop was `PLAYING→STOPPED` after overdub already returned to PLAYING; the compose verifier `stop_selected_slot_not_foreign_loop` looks for `OVERDUBBING→STOPPED` and is vacuous on this log.

## Out of scope

| Item | Why |
|---|---|
| Publish NOTE_EDIT rows onto `root_rev=3` | DEC-056 |
| `finishOpenNotes` / reconstruct | Repair after the orphan exists |
| Occupy hide/ledger | Different owner |
| Changing compose 01–04 | Host recipe lock |
| New display Manager or second `liveDisplayNotes` owner | Extend emit/resolve |

## Docs after ship

- [`CURRENT_WORK.md`](../Runtime/CURRENT_WORK.md) / [`PROJECT_STATE.md`](../Runtime/PROJECT_STATE.md)
- [`HITL_TEST_SCENARIOS.md`](../Guides/HITL_TEST_SCENARIOS.md) only if a dedicated preset is added
