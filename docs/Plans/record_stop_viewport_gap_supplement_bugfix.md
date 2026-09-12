# Record-stop viewport gap — low-bar supplement (GAP-8c)

**Status:** **SHIPPED (2026-09-12)** — native + RAM1 verified; device recapture vs [`202721`](../../captures/session_20260912_202721.log) pending  
**Kind:** bugfix (display-only, publication-gap UX)  
**Parent:** [`record_stop_snapshot_publication_gap_display_bugfix.md`](record_stop_snapshot_publication_gap_display_bugfix.md) — stage 8c  
**Related shipped:** GAP-8 (`supplementDrawSourceFromUnpublishedSnapshotForUncoveredBars`), GAP-8b (`isPlaybackSessionExitPublicationGap` stale-root predicate)  
**Not this RC:** GAP-8b overdub-stop stale published root (`200023` `267→256`); viewport reposition policy (deferred alternative)

---

## Symptom (device)

After **record stop → PLAYING**, bars **2–4** (and often 0–1) on the OLED briefly show **no notes** until the session-exit snapshot publishes (~0.5–0.8 s in recent captures).

**Anchor:** [`session_20260912_202721.log`](../../captures/session_20260912_202721.log) — 20-bar record, `loopLength=15360`.

| Check | Evidence |
|-------|----------|
| Viewport jump | Last RECORDING `DISP` `win_start=3432` (bar ~4.5) → first PLAYING `win_start=0` |
| Handoff retained | `frame=281` unchanged across stop |
| Low bars missing from window | `windowNoteCount` **257 → 217** (−64 ≈ four bars of density) |
| Publication gap | PLAYING `54998223` → `kind=publish` `55762949` ≈ **765 ms** |
| Post-publish OK | `DISP` `win_notes=257` after publish |

Bars **0–3** were never in the RECORDING paint window; bar **4** is only partially covered (handoff starts at tick 3432; bar 4 is 3072–3840). User-visible report focuses on **bars 2–4**.

---

## Debugging boundary

```text
capture / commit / seal              ← trust (content is in passes)
  → session-exit publication gap     ← unchanged (DEC-045 idle publish)
  → refreshViewportAfterRecordStop   ← exposes bars 0–3 not in RECORDING handoff (trigger)
  → draw-source gap handoff retain   ← GAP-8 / GAP-8b (working)
  → supplement uncovered bars        ← GAP-8c (this slice)
  → paint / OLED                     ← symptom
```

Do **not** reopen RC1/RC2 convergence, sync publish on stop, or overdub-stop handoff (GAP-8b).

---

## Root cause (GAP-8c)

Two compounding gaps in the existing GAP-8 supplement path:

### RC-GAP8c-1 — Supplement source timing

`supplementDrawSourceFromUnpublishedSnapshotForUncoveredBars` only reads `Loop::unpublishedSnapshot()`.

After record stop, `unpublishedCandidate` is built incrementally on the idle snapshot path. First PLAYING paint in [`202721`](../../captures/session_20260912_202721.log) occurs ~32 ms after PLAYING; first construction `kind=finish` ~160 ms later; publish ~765 ms later. **Committed passes already hold the full record** at stop — supplement does not use them when unpublished is null or incomplete.

### RC-GAP8c-2 — “Bar covered” is too coarse

Supplement skips a bar when **any** display note intersects that bar (`previewNoteIntersectsBar`). After viewport reposition, bar **4** can be marked covered by notes starting at tick 3432 while ticks **3072–3431** (left portion of the bar) remain empty on the roll.

---

## Architecture gate

| Question | Answer |
|----------|--------|
| **Owner** | `supplementDrawSourceFromUnpublishedSnapshotForUncoveredBars` in `DisplayNoteWindowGather.cpp` (extend); predicates in `DisplayWindowUtils.h` if needed |
| **Invariant** | During `isPlaybackSessionExitPublicationGap()`, every bar in the active paint window that intersects committed record content must show that content on the first PLAYING frame after record stop, without waiting for `kind=publish` |
| **Ownership change?** | **NO** — extend existing supplement owner; no new Manager |
| **Transition change?** | **NO** |
| **Execution bound change?** | **NO** — per-bar supplement only for bars already judged uncovered; bounded by paint window bar count (≤ 16) |
| **Behavior-preserving outside gap?** | **YES** — supplement runs only when `shouldHoldSessionExitPublicationGapHandoff` already fired |
| **Reuse** | **YES** — extend GAP-8 supplement; passes bar fill via existing `LoopContentResolution::resolveDisplayNotes` / per-bar gather pattern used in `gatherCommittedDrawSourceBars` |

**Deferred alternative (out of scope v1):** change `refreshViewportAfterRecordStop` to defer `win_start=0` until publish, or clamp reposition so bars 0–3 stay off-screen until snapshot lands. Higher UX risk; only if supplement dual-source is insufficient on device.

---

## Approved implementation (narrow)

### 1. Dual-source supplement (primary)

In `supplementDrawSourceFromUnpublishedSnapshotForUncoveredBars` (or a package-private helper it calls), for each paint-window bar that is **not adequately covered**:

1. **Prefer** `unpublishedSnapshot()` membership (existing GAP-8 path).
2. **Fallback** when bar still uncovered and `loop.hasCommittedPasses()`: append display rows for that bar from **committed passes** using the same per-bar projection path as draw-source gather (no full-loop materialize; window bars only).

Order matters: unpublished remains authoritative when present; passes fill the early-gap hole before construction finishes.

### 2. Adequate bar coverage predicate (secondary)

Replace boolean “any intersection” with **adequate coverage** for supplement decisions:

- A bar is **uncovered** when no committed handoff row spans the bar’s **left edge** at least through one grid cell, **or** when the bar has zero intersecting notes (bars 0–3 case).
- Minimal v1: treat bar as uncovered unless `displayFrameCoversBar` AND at least one note’s `startTick <= barStart + smallMargin` (e.g. within first 48 ticks of the bar) — avoids skipping bar 4 when only the right fragment exists.

Add predicate `displayBarAdequatelyCoveredForGapSupplement` in `DisplayWindowUtils.h` with native tests.

### 3. Do not change

- `refreshViewportAfterRecordStop` window reposition (v1).
- `isPlaybackSessionExitPublicationGap` (GAP-8b).
- Overdub-stop `preservedOverdubStopDisplayNoteCount` trim.
- DEC-045 synchronous publish on stop.

---

## Native fixture (required before device closeout)

**Suite:** `test_loop_take_survival`  
**Name:** `test_gap_handoff_supplements_low_bars_after_record_stop_viewport_reposition`

### Setup (reproduces `202721` geometry)

| Field | Value |
|-------|--------|
| Loop | 20 bars (`loopLength = 20 × TICKS_PER_BAR`) |
| Record | One note per bar, distinct pitch per bar (bars 0–19) |
| Stop | `commitCapturePass(RecordStop)` + `queueSessionExitSnapshot`; **do not** `drainPendingSnapshotPublish` |
| Gap | `isPlaybackSessionExitPublicationGap()` true; `publishedSnapshot()` null |
| Handoff simulation | `DisplayNoteVec` containing **only bars 16–20** (append via `appendSnapshotNotesForBar` on unpublished **or** synthetic rows) — models RECORDING `win_start≈3432` |
| Paint window | `firstBar=0`, `lastBar=15` (post-`refreshViewportAfterRecordStop`) |

### Assertions

1. **Before supplement:** `displayFrameCoversBar(handoff, 2..4)` false (bars 2–4 uncovered).
2. **After supplement (passes fallback with `unpublishedSnapshot()` cleared or pre-first-finish):** bars **2, 3, 4** covered; `handoff` note count increases.
3. **Repeated supplement:** idempotent — no duplicate `(pitch, start)` explosion.
4. **After `drainPendingSnapshotPublish`:** gap false; supplement no-op; frame matches published snapshot gather for bars 2–4.
5. **Negative:** when handoff already includes bar 2, supplement does not duplicate bar 2 rows.

**Host twin:** extend `applyDrawSourceSessionExitGapHandoff` (GAP-8b) to call the production supplement helper once implementation lands.

**Existing tests must stay green:** `test_gap_handoff_supplements_uncovered_bars_from_unpublished_snapshot`, GAP-8b stale-root tests.

---

## Device validation

| Gate | Criterion |
|------|-----------|
| Anchor | [`session_20260912_202721.log`](../../captures/session_20260912_202721.log) — same 20-bar record-stop flow |
| PASS | First PLAYING `DISP` after stop: `windowNoteCount` within ~16 notes/ bar × covered bars (no −64 drop vs frame); bars 2–4 visually populated on OLED |
| PASS | Post-publish `DISP` unchanged vs pre-fix quality (`win_notes≈257`) |
| Build | `pio run -e teensy41-capture-serial` RAM1 ≥ 4000; `pio test -e native` |

Native fixture alone is **not** sufficient proof of OLED fix.

---

## Implementation checklist

1. Add `displayBarAdequatelyCoveredForGapSupplement` + unit tests in `test_display_window_utils`.
2. Extend supplement with passes fallback (per-bar, bounded).
3. Add `test_gap_handoff_supplements_low_bars_after_record_stop_viewport_reposition`.
4. Run full native + RAM1 build.
5. Device recapture; compare first PLAYING `DISP` to anchor table above. **Pending.**
6. Update parent plan stage 8c **SHIPPED**; OpenSpec `tasks.md` §6.6.7; `CURRENT_WORK.md`. **Done.**

### Validation (2026-09-12)

| Gate | Result |
|------|--------|
| Native | `pio test -e native` **1716/1716** (1 skipped) |
| RAM1 | `teensy41-capture-serial` locals **6848** |
| Fixture | `test_gap_handoff_supplements_low_bars_after_record_stop_viewport_reposition` PASS |
| Device OLED | **Pending** — recapture vs [`202721`](../../captures/session_20260912_202721.log) |

**Commit:** one focused commit (GAP-8c only).

---

## Pre-implementation review

### Ready

- Log anchor and numeric proof (`202721`).
- Owner and supplement extension point identified in codebase.
- GAP-8b predicate unchanged; overdub path unaffected.

### Resolved

| Topic | Decision |
|-------|----------|
| Primary fix | Dual-source supplement (unpublished → passes fallback) |
| Partial bar 4 | Adequate-coverage predicate in supplement gate |
| Viewport reposition | **Deferred** — not in v1 |
| Publish sync | **No** — display-only |

### Open (implementer pin-down)

1. Exact passes bar projection helper: reuse `gatherCommittedDrawSourceBars` slice vs inline `LoopContentResolution` per bar — prefer matching `gatherCommittedDrawSourceBars` bar loop without full-window gather.
2. Margin constant for “left edge covered” — start with 48 ticks (one grid cell at 16th) or `Config::TICKS_PER_BAR / 16` if defined.

### Proceed?

**YES** — no ownership/transition blockers.
