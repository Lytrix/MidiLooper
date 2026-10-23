# Slice 3.3 — the window representation is budgeted, and the three rolling views' lifetimes suffice

**Change:** `windowed-playback-generation` · **Task:** 3.3 · **Date:** 2026-10-02
**Branch:** `feature/windowed-playback-generation` · **Contract:** DEC-108 items 1, 3 and 4;
`RuntimePlaybackViewSet` / `RuntimePlaybackViewLifecycle`; RWO `S2C1-PLAYBACK-WINDOW-LEASE.md` and
`S2C23-CLASS-BUDGET.md` (cited, not restated).

**Why this slice is measurement** — 3.3 asks for three things, and none of them is behaviour: (a) **current**,
**ready** and **building** windows are accounted, with reader retention, staging and transition overlap;
(b) the three rolling views' lifetimes are shown to **suffice before any buffer is added**; (c)
`test_playback_prewarm` stays green **unchanged**. What follows is the account, the lifetime argument in code, and
the measured cost of what this change added — which is **zero bytes**.

---

## 1. Five role names, three buffers

`RuntimePlaybackViewLifecycle` names five roles over `RuntimePlaybackViewConfig::kViewCount = 3` buffers
(`include/RuntimePlaybackView.h:320-349`):

| role | what it holds | who reads it | can it be live at the same time as… |
|---|---|---|---|
| `current` | the published interval window — the authority for this slot | consumers (3.4/3.5/3.7); **nothing yet** | `build`, and one of `ready`/`retained` |
| `build` | the **one** buffer being filled; never a consumer's | the owner's grains only | `current`, and one of `ready`/`retained` |
| `ready` | a **completed** successor awaiting adoption | the pair/record lanes today | `current` **and** `build` |
| `retained` | the record-return window | the record-return lane | aliases `current` or `build` — never a fourth |
| `idle` | a **sentinel**, not a buffer (`kNoView`) | nobody | n/a |

The invariant that makes three suffice is in the code, not in this document: **`retained` is always an alias of a
live buffer.** `retainCurrent` sets `retained = current`; `handoff` sets
`views[retiredCurrent].published = (retiredCurrent == retained)` and then hands the retired view to `build`, so a
retention carried across a swap *becomes* the build buffer rather than needing a fourth. `installPreparedCurrent`
keeps the relation too, and the set's `resetMetadataPreserveRetained()` preserves exactly the retained view and
makes it `current` (`RuntimePlaybackViewSet::resetMetadataPreserveRetained`, `RuntimePlaybackView.cpp:968`). The new publisher this change added
(`publishCompletedBuildAsCurrentWindow`) refuses outright when the view it would recycle is the retained one —
`if (retiredCurrent < views.size() && retained == retiredCurrent) return false;` — which is that invariant
enforced at the only new place a buffer changes hands.

**What this change's publisher uses.** A demoted slot's window is installed straight into `current` (a completed
build replaces the window), so the simultaneously live set for the windowed lane is `current + build` — **two of
the three**, plus `retained` only when the record-return lane is carrying one, which is the existing relation, not
a new one. `ready` is untouched by this change: it exists for the successor/one-pass-late lane (3.4/3.5).

## 2. Why retention needs no fourth buffer (and what "reader retention" means here)

DEC-108 item 4 fixes the retained form: **pinned bodies plus reconstruction through the effective-content
resolver** — never "the last window buffer". So the one-pass-late *content* of 3.6 is served by a **source pin**
whose bytes are charged as that term (RWO's class budget), while the *buffer* that held the previous window is
recycled into `build`. That is why no fourth view is added: the two mechanisms are different terms in the budget,
and 3.6 owns the pin's cost.

"Reader retention" in the lifecycle's sense is **per-read atomicity**, not residency: a consumer read is a read of
`current`, the replacement is built in `build`, and the swap is the single assignment pair `handoff` performs — so
no read straddles a replacement. A consumer that wants the *previous* revision after the swap asks the retained
**source** (3.6), which is exactly the design's split between "never torn" and "one pass late".

**Staging and transition overlap.** The two capture stagings are part of the same holder reservation
(`std::array<CaptureAdmissionStaging, 2> captureStagings`) and are *not* window buffers. A demoted slot reserves
**no bridges and no whole-take base** (`prewarm(...)` is called with `fullTakePlaybackViewAdmissible()`, false past
the demotion bound; `beginCandidateBridge`/`selectCandidateBridge` refuse without the reservation), so the
windowed lane has **no bridge overlap to budget** — its transition is the next window plus the closure
reconciliation of 4.5. This change adds no staging, no bridge and no base.

## 3. The accounting (host-derived from the code; the target-ABI figures are RWO's, cited)

The code computes the holder's own total in `RuntimePlaybackViewSet::fixedExternalBytes()`
(`src/Track/RuntimePlaybackView.cpp`):

```
eventBytes      = sizeof(MidiEvent) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint8_t)
viewBytes       = kViewCount(3) · kEventsPerView(2048) · (eventBytes + sizeof(PlaybackPassEventOrigin)(4) + 2)
                + kCaptureStagingEvents(2048) · eventBytes · captureStagings.size()(2)
wholeTakeBytes  = + kBridgeCount(2) · kEventsPerView(2048) · (eventBytes + …) + bases   [only if reserved]
```

On the **target ABI** (`sizeof(MidiEvent) = 20` on cortex-m7, the figure `design.md` and RWO's S2C1 already
verified — *not* 32 as on the host, which would inflate every figure by 34 %):

| term | bytes | KiB |
|---|---|---|
| three views (`3 × 2048 × 35`) | 215 040 | 210.0 |
| two capture stagings (`2 × 2048 × 29`) | 118 784 | 116.0 |
| **windowed holder total** | **333 824** | **326.0** |
| whole-take part (bridges + bases) | **not reserved for a demoted slot** | 0 |

`35 B/event` per view and `29 B/event` per staging are the *same* figures RWO's `S2C1-PLAYBACK-WINDOW-LEASE.md`
derived from these `sizeof`s (its per-holder windowed `P` is stated there as ~331 KiB for the class, from the same
constants plus the class's other terms); this slice neither re-derives nor changes them. What matters for 3.3 is
the *identity* of the terms: the windowed lane's per-holder cost is exactly "three views + two stagings", and the
whole-take reservation is absent — which `test_playback_prewarm` already pins and this change exercises
(`wholeTakeViewsReserved()` is false, `fixedExternalBytes()` is the smaller figure, and a windowed publication
succeeds on that slot anyway).

**What this change added to the account: nothing.** The owner's build state is the pre-existing
`RuntimePlaybackViewBuild viewBuild` member of `LoopPlaybackRuntime`, and its publication reuses the lifecycle
roles. Both are *measured* rather than asserted: the build still passes the pinned
`static_assert(sizeof(LoopPlaybackRuntime) == 6368)` and the pinned `sizeof(Track) == 384` (so no new per-slot or
per-track state at all), and the RAM1 line is unchanged at `variables 96,192`.

## 4. Verification

| Check | Result |
|---|---|
| `pio test -e native -f test_playback_prewarm` | **190/190 — unchanged.** That suite is **cited, not restated**: it already pins the per-view capacity, the whole-take reservation's presence/absence and `fixedExternalBytes()` shrinking when the reservation is dropped. |
| `pio test -e native -f test_runtime_working_set_ownership` | **50/50** — the windowed publication the account describes runs on a demoted slot whose `fullTakePlaybackViewAdmissible()` is false. |
| `pio test -e native -f test_midi_service_drain` | **38/38** |
| `pio test -e native` (full) | **2681 cases: 7 skipped, 2674 succeeded, 0 failed** |
| `pio run -e teensy41-capture-serial` | **SUCCESS** — `variables 96,192` (unchanged) / `code 392,136 + padding 1,080` / locals 34,880, floor 840; the `sizeof(LoopPlaybackRuntime) == 6368` and `sizeof(Track) == 384` pins both hold |
| `git diff --check`, `openspec validate --strict` | clean / valid |

No new fixture is added by this slice on purpose: the capacity arithmetic is already pinned where it lives
(`test_playback_prewarm`), and the lifetime claim is enforced by code in exactly one new place
(`publishCompletedBuildAsCurrentWindow`'s retained-view refusal), which the 3.2 fixture exercises on the path it
belongs to. Re-asserting the formula here would duplicate the pin and hide a change to it.

## 5. Not claimed

- No **device** high-water mark: the figures are the holder's reservation, not resident usage under a live
  workload. RWO's `C`/`D`/`I`/`O` classes and their device maxima are that change's; the `P` term's device
  behaviour stays RWO's `S2.3`/`S4` evidence.
- No `N`, density or envelope claim: 3.3 accounts one holder. The supported `N` remains DEC-108 item 9's target,
  measured only when both a memory and a deadline test pass.
- The pinned source's **retention cost** is 3.6's term; this slice only establishes that it is a source pin rather
  than a fourth window buffer.
