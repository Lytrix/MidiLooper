# Handoff — display source retention (084714 wrap/undo handoff)

**Status:** slices 1–2 landed and natively verified 2026-10-27 (fixtures 1–7 green, enforced RED green,
RAM1 line unchanged); the tier rule is implemented without a fixture; no device evidence.
**Branch:** `feature/unified-rolling-playback-path` (heavily dirty — 77+ modified paths; preserve all).
**Installed image:** `7c04edd8…` (owner reports it made display *worse* — do not defend it).
**Authority trail:** [084714 evidence](../openspec/changes/immediate-record-overdub-handoff/evidence/2026-10-27-084714-DISPLAY.md)
is the live contract; task **4.19** holds it. Implementation record:
[RETAINED-DISPLAY-SOURCE](../openspec/changes/immediate-record-overdub-handoff/evidence/2026-10-27-RETAINED-DISPLAY-SOURCE.md).
This file is the entry point, not a second source of truth.

---

## 1. User-visible problem (the acceptance target)

After a Record pass and further overdubs, the piano roll loses recorded content:

- recorded notes missing in later windows (bars 16–24/32),
- overdub notes disappearing in the last bars,
- after a loop wrap the Record base is not drawn at all until the transport has been stopped ~30 s.

Playback was separately fixed and the owner confirmed it. **Do not touch playback.**

Device evidence (`captures/session_20261027_084714.log`): display keeps content key `4088,12288` while the
requested/drawn interval moves to `6912,12288`; wrap keeps the old key; snapshot publishes at 194.487 s and
the frame goes `88 → 463` rows at 194.537 s — 31.7 s after transport stop. Capture was shedding and carries
no per-note paint inventory, so it proves *display publication*, not stored-note deletion.

## 2. Root cause (VERIFIED)

`BoundedDisplaySession` publishes one result keyed by `sameRequest` (owner, loopId, length, `playbackRevision`,
pass count, committed rows). A wrap **appends a pass** and advances `playbackRevision`, so the previous
complete result stops being qualified: the provider falls through, the display holds a frame covering only
its old interval, and the next `begin()` → `reset()` **clears** the notes (`LoopContentResolution.cpp:2679`).
The last correct generation is rejected, then destroyed. No quantum, wakeup or cache tweak can fix that.

## 3. Settled contract (implement exactly)

**Retention identity is a bounded exact predicate over authoritative pass metadata at reuse time — not a
cached stamp, not a digest.** Zero new `Loop` state.

Required equalities: `owner`, `loopId`, `loopLengthTicks`, `keyEventRows`, edit-pass count, last edit id,
`canonicalStorageGeneration`, and `retainedPassCount <= live.passCount`.

Then, walking **only the retained prefix** in resolve order: `index`, `PassId`, `CapturePassState`, and exact
`committedChunkIds` identity (not a row count). For edit passes: `index`, `EditPassId`, `EditPassType`,
`EditPassState`.

Passes appended *after* the prefix are deliberately not compared — that is what lets a wrap keep serving.
State changes *inside* the prefix refuse it. Any mismatch is a **named refusal**; the caller keeps its honest
hole. Never relabel old rows as current.

**Roles:** one candidate (unchanged) + one retained envelope, global (producer is single/global).
`resetCandidate()` runs on re-key and `!passesMatch`; `clearRetained()` only on explicit
`boundedDisplayCancel()`, invalid request, replacement by a newer promotion, or over-cap refusal.

**Promotion:** at the existing publish edge, `std::move` the candidate notes into the envelope. No
allocation on the publish turn, no `shrink_to_fit`.

**Pricing (conservative, owner-chosen):** retained cost is the capacity `std::move` actually keeps, i.e.
`16·E` (`admitWorkspace` reserves `totalEventRows`), **not** `16·R`. Steady peak while rebuilding is
`16·E_retained + B_cand`. Over `kRetainedDisplaySourceNoteCapacity = 4096` notes → **named refusal**
(`retained_capacity`), previous envelope kept; with none, behaviour equals today's.

**Appended-row availability (F6):** a wrap's new pass is not in the envelope; its closed rows reach paint via
`sealedCapturePreviewNotes_`, released against the *cache*, not the envelope. Retained serve therefore also
requires that the tier still covers the appended pass (`!sealedCapturePreviewIsCovered()` while
`passCount > retainedPassCount`); otherwise fall back to the cache/gather path, which does contain them. The
exact accessor is fixed at implementation and proven by fixture 5-of-the-old-list (see §5 note).

**Resident store:** derived display source, external/PSRAM-first; cardinality 1; cap 4096 notes / 65,536 B;
previous-version eviction.

## 4. Decisions already made — do not relitigate

| Rejected | Why (evidence) |
|---|---|
| New `passStateRevision` field on `Loop` | `sizeof(Loop)` is pinned at **1,848 B** (`TrackPlaybackRuntime.cpp:3057`, eight shells/Track). `+8 B` failed the build. |
| Storing per-pass state in a fixed array | `[16]` would truncate or refuse at real pass counts; predicate is cardinality-free instead. |
| 64-bit digest identity | Exact walk is already O(P); a stale-content guard must not carry a probability term. |
| `shrink_to_fit` on promotion | Allocates on the publish turn; price the real capacity instead. |
| "Max, not sum" peak claim | Wrong: retained capacity overlaps the new candidate. |
| Enumerating restore/hydrate call sites | Same hidden-invalidator class that caused the bug; rely on exact metadata + `canonicalStorageGeneration`. |
| Forbidding undo→redo requalification | `setCapturePassState` does **not** bump `canonicalStorageGeneration` (`LoopCapture.cpp:176`), so forbidding needs new state. If all exact fields match, the retained rows *are* the deterministic output of current passes — serving them is provably identical to rebuilding. Proven by equality against an independent rebuild, not asserted. |

## 5. Fixtures first (owner-specified order)

Written and green (2026-10-27); each is a permanent control in the default suite. Serving fixtures were RED before
the provider branch; the refusal fixtures are red-proved by mutation (see the implementation record).

1. **Capture undo invalidates** — same id/chunks/count, state flips Disabled → must not serve.
2. **Capture redo requalifies** — state returns Active; served rows must equal an **independently rebuilt**
   frame note-for-note by `NoteId` + geometry. Difference withdraws the property.
3. **Edit undo invalidates** — same edit id/count, state flips → refused.
4. **Wrap append preserves the prefix** — appended pass after the prefix does not invalidate.
5. **Capacity boundary** — at the declared max, retention is accepted and priced by actual capacity.
6. **Over-cap** — one beyond the cap is a named refusal; existing qualified retained content stays intact.
7. **Original immediate-wrap RED stays enforced**, unweakened:
   `test_display_source_progresses_with_pending_successor_stop_and_across_wrap`
   (`test_live_capture_display_boundary.cpp`, fails today at "first wrapped frame must retain Record content
   while its replacement source is not ready").

**Appended-row/tier case (former F6 item): landed as fixture 4b.** The wrap commit appends the pass, the envelope
serves while the sealed tier is still owed (receipt `covered=0,sealed=1,passes=3,retained=2`) and does not claim the
appended rows, and a production capture undo→redo inside the prefix releases that tier — after which the envelope's
own predicate still qualifies while the provider stands it down. Red-proved both ways (rule removed, rule inverted). Destructive set: geometry change, clear and a **slot/focus change (fixture 4c)** have owner-level fixtures — 4c also
catches an over-serve across slots (`served=1,rows=2,src=retained` under the request-blind mutation); load/hydrate
reaches the same `resetPassTimeline` destruction the clear fixture exercises (the production slot-reset entry is not
linkable in this include-based harness) and competing audible track keeps its existing suite — attributed rather than
restated.

**Test traps already hit:** the earlier oracle drained 4096 grains *after* wrap and passed while the
immediate frame was broken — never assert only post-drain output. Sparse fixtures must pin final geometry
with a trailing anchor or stop-normalization shortens the take. Give the display fixture rolling enabled
(`PLAYBACK_WINDOW_ROLLING_ENABLED=1`) and reset prepared/continuation sessions in `setUp`.

## 6. Implementation slices

1. **Storage + identity** — landed: `BoundedDisplaySession` candidate prefix identity, `resetCandidate()` split out
   of `reset()`, `promoteRetained()`, `admitDisplayProjectionReserve(16·capacity)`, accessors +
   `kRetainedDisplaySourceNoteCapacity` (`include/LoopContentResolution.h`).
2. **Provider wiring (the fix)** — landed: retained evaluated **after** `BoundedContinuation`, **before**
   `PreparedWindow`, reusing the same `noteIntersectsWindow` filter and reserve; `QualifiedDisplaySource::
   RetainedEnvelope` is the served role.
3. **Appended-row/tier rule** — landed with fixture 4b (see §5); red-proved by removing and inverting the rule.
4. **Destructive/stale interleavings** — clear + geometry change landed; the rest attributed.
5. **Build + measured budget** — landed: `variables 96,192` / `padding 3,224` / locals 34,880, no re-base.

## 7. Verification and the numbers that must not move

```
pio test -e native -f test_live_capture_display_boundary -f test_frozen_record_reader_survival   # exit 0
IMMEDIATE_VISUAL_CASE=bound pio test -e native -f test_immediate_visual_cache_probe              # 7/7
pio test -e native                                                                               # no new failures
pio run -e teensy41-capture-serial                                                               # SUCCESS
```

Baseline to preserve exactly: `variables 96,192`, `ITCM 393,216 (code 389,992 + padding 3,224)`,
`locals 34,880`, padding floor 840. `scripts/check_ram1_budget.py` fails on **any** `variables` change, so
"unchanged" is measured. Avoid new string literals in these TUs; if a receipt token is needed, put it where
an existing `.rodata.str*` carve-out already exists. Do not re-base the budget for observability.

Pre-existing, not yours to fix: terminal `SIGHUP` from the harness at suite end, and unrelated failures in
the dirty tree — attribute by file, do not "fix" them.

## 8. State of the tree

- Unshipped, partial **wakeup correction** in `TrackDeferredMaintenance.cpp`: focus arbitration moved before
  the pending-Stop early return; that branch spends its existing VisualCache slot on one source child
  (private if owed, else the committed display continuation). It fixes *availability only*. Keep it — the
  pending-Stop regression test depends on it — but it is not the fix.
- Enforced RED test above is in the default suite. Leave it enforced.
- The retained envelope and its provider branch **exist** (slices 1–2) and are natively verified; the tier rule is
  implemented without a fixture. No upload, no device stimulus, no commit.

## 9. Needs the owner, not the agent

- Upload + registered content stimulus for device acceptance (task 4.4) — ask first, never assume.
- Any decision that would grow `Loop`, re-base `ram1_budget.json`, or add a scheduling class.
- Exact accessor for the F6 tier rule if the reachable predicate proves ambiguous.
