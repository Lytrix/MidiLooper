# First record-pass note display vs loop-end Off

**Status:** RC1 implementing — [`192140`](../../captures/session_20260909_192140.log) close commit  
**Kind:** bugfix  
**Evidence:** [`session_20260909_192140.log`](../../captures/session_20260909_192140.log) (unpark); [`session_20260909_123953.log`](../../captures/session_20260909_123953.log) (select remap)  
**Does not reopen:** compose Stage 1/2, wrap-head RC1–RC3 (`wrap_head_last_off_pairing_bugfix.md` is closed), hydrate 4b–5, control-plane RC6 paint extra

## Debugging boundary

```text
compose paint / overlap resolve
    ← closed; do not reopen
STIM-NE-OD-COMPOSE-02 host promotion
    ← host-only; not this owner
Record-pass first 48 store pair (SEVT / LCR srcspan)
    ← trust unless a new capture shows a stored Off at loopEndTick
NOTE_EDIT select remap + wrap-head focus span
    ← current investigation
```

## What this capture does not show

The first **recordPass** note is not stored, and is not opened, with Off at `loopEndTick`.

| Source | Pitch 48 first record note |
|---|---|
| `SEVT` | `N,81` `F,135` |
| `DIAG,lcr,srcspan` | `id=1,p=48,s=81,e=135,g=linear` |
| `DNTE` at NOTE_EDIT open | `48,81,81,54` (storage 81, display 81, length 54) |
| `CLN,record,wrap_synth` | `0` |

`loopLength` after record stop is **2304**. Inclusive loop-end is **2303**. None of those rows place that Off there.

## What this capture does show

At 1042.218 s, after in-edit live 48s, select remaps off the seed:

| Line | Value |
|---|---|
| `Note selection changed` | `7 -> 3` |
| `Select fader: selected note 3 at tick 0` | tick **0** |
| `#DBG select_apply` | `selected_tick=0 note_idx=3 … reason=note_changed` |
| `DNTE` same window | `48,2292,2292,12,3` |

Owner of `reason=note_changed`: `ControlSurfaceManager` select apply in `SelectFaderInput.cpp` (`logSelectApplyDecision` + `applyNoteSelectFromFader1Pitchbend`).

Selected idx 3 is a same-pitch live note at **2292** with display length **12** (2292+12 = **2304** = `loopLength`). That is a loop-end Off on the **live** 48, not on the record-pass `81–135` pair.

F2 then uses `focus.last` `start=0` `end=135`:

| Line | Value |
|---|---|
| `Coarse fader using focus.last` | `pitch=48, start=0` |
| `Overlap move bridge` | `start=0, end=135` |
| `POSITION EDIT` | `tick 0 -> 1536` |
| `DNTE` after move | `48,1536,1536,135` |

`end=135` is the first **record-pass** Off. `start=0` is the select tick after `note_changed`, not the stored On at 81. After the move the painted length is **135** (0–135), not the opened length **54**.

## Code that paints one note across the loop

A single piano-roll note spans the loop only when a writer sets **end to `loopLength - 1`**, or **end &lt; start** (wrap draw). Those writers:

| Function | What it writes | First record On@81 |
|---|---|---|
| `NoteUtils::finishCanonicalSpansOpenNotes` | Unpaired On → exclusive end = `loopLength`, `isOpen` | Yes, if that On is still on the reconstruct stack |
| `IntervalProjection::renderProjectedIntervalToDisplayNotes` | `isOpen`: `endTick = loopLength - 1`. `splitHeadTail`: tail to `loopLength - 1` plus head from 0 | Paints whatever spans reconstruct emitted |
| `applyCaptureEventToPreview` wrap-Off branch | `note.endTick = loopLength - 1` plus `resolveWrapHeadSegment` head from 0 | No for Off@135: `isHeadTailWrappedPair` requires On in the last 768 ticks |
| `applyCapturePlayheadTails` / `applyLiveOpenTails` wrap split | `tailEnd = loopLength - 1` plus wrap head from 0 | Growing RECORD sets `allowWrapContinuation` false (`live_record_open_note_playhead_tail_bugfix`) |
| `DisplayManager::drawNoteBar` | `endTick < startTick` draws start→loop end and 0→end | Pixel owner for a wrap-held `DisplayNote` |

`reconstructDisplayNotes` defaults `finishOpenNotes` to **true**. The header states unpaired NoteOns become loop-end tails. `rebuildCapturePreviewFromStore` calls the 3-arg form (`verboseLog=false`), so that default applies.

## Open before a patch

1. Which display row is `note_idx=3` at `selected_tick=0`: wrap head of `2292–2303`, or a distinct tick-0 row that `DNTE` did not dump as start 0?
2. Who writes `focus.last` to `0–135` between that select and F2 — `rebuildNoteEditFocusAtSelect`, `syncNoteEditFocusLinearFromSessionStore`, or wrap-head pairing in `NoteUtils` / `IntervalProjection::renderProjectedIntervalToDisplayNotes` (`endTick = loopLength - 1` on open / `splitHeadTail`)?
3. A frame that paints the **record-pass** first note itself with Off at `loopEndTick` does not exist in [`123953`](../../captures/session_20260909_123953.log). Do not patch as if that store pair were wrong.

## Architecture checkpoint (no patch this slice)

1. Ownership change? Not asked — investigation only.
2. State transition change? Not asked.
3. Unbounded interval owner? Not asked.

Do not patch until those three are **no** for a named owner, or stop for a design session.

## Parked 2026-09-09

Watch HITL [`132708`](../../captures/session_20260909_132708.log) did not
reproduce a first record-pass note Off at `loopEndTick`. Skip until a new
capture shows that row. Do not put this back in Now implementing.

## Unpark — [`192140`](../../captures/session_20260909_192140.log)

After in-edit overdub stop, NOTE_EDIT close writes the wrap-head mover to inclusive loop end.

| Clock | Fact |
|---|---|
| 57.331 s | Select remaps to note 55 at tick 80; focus `noteId=58 presence=0` |
| 57.350 s | `DNTE,48,80,80,0` — painted length 0 |
| 66.437 s | `OVERDUBBING → PLAYING` (in-edit fold) |
| 67.000 s | `ChangeLength targetNoteId=58 start=80 moverBaselineEnd=80 newEnd=2303` |

`2303` is `loopLength - 1`. The first record-pass pair stays `48@125–142`. The invented Off is the close-commit Length row from `focus.last` after `normalizeAll` `closeOpenTails`.

### Architecture checkpoint (RC1)

| Question | Answer |
|---|---|
| Ownership change? | NO — extend `shouldRejectMoverPreCommitRow` / `shouldSkipOverlapDiffToLoopEnd` |
| State transition change? | NO — same NOTE_EDIT close commit |
| Unbounded interval owner? | NO — same pre-commit walk |

### RC1 — mover Length to unpainted loop end

**Invariant:** Close must not emit mover `ChangeLength` to `loopLength` or `loopLength - 1` unless `commitBaseline` or committed display already ends there.

**Owner:** `shouldRejectMoverPreCommitRow` in [`NoteEditFocusPreCommit.cpp`](../../src/EditManager/NoteEditFocusPreCommit.cpp). Inclusive loop-end is the same skip as overlap RC3 (`192755` / `201948`).

**Test:** `test_commit_skips_unpainted_mover_loop_end_length_192140`. HITL `commit_skips_unpainted_mover_loop_end`. Do not edit compose `01`/`02`/`03`/`04`.

## Pre-implementation review

### Ready
- `shouldSkipOverlapDiffToLoopEnd` already skips overlap Length to exclusive `loopLength` when paint is not loop-end
- `shouldRejectMoverPreCommitRow` already filters mover Length / NoteRange; it did not treat inclusive `loopLength - 1`
- `buildCommitRowsFromCurrentState` / `buildPreCommitEditPasses` already receive committed display notes. Parked Visible append uses the same unpainted loop-end Length skip as overlap RC3 (`shouldSkipOverlapDiffToLoopEnd`).

### Resolved (user / code)
| Topic | Decision |
|---|---|
| Owner | Extend mover pre-commit reject — do not change `normalizeAll` closeOpenTails |
| Inclusive vs exclusive | Both are loop-end Off (`2303` and `2304` on a 2304 loop) |
| Intentional lengthen | Still emits when committed display paints loop-end |

### Open before coding
1. None — implementer pin is inclusive loop-end on the mover Length row only

### Proceed?
- YES
