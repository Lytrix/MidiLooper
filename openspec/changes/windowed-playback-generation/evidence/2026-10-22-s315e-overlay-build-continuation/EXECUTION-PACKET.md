# 3.15e — migration slice 1 execution packet (overlay build continuation)

**Change:** `windowed-playback-generation` · **Task:** 3.15e (single-reader migration for the overdub overlay) ·
**Branch:** `feature/unified-rolling-playback-path` · **Base:** `694b7f72` · **Design:** the
[2026-10-22 addendum](../../ARCHITECTURE-REASSESSMENT.md#2026-10-22-addendum--the-live-overdub-overlay-moves-onto-the-single-window-reader-315e-dec-116-extension).

## Identity and scope

- **Slice claim:** a window already filled and ordered from the committed base is **continued, in the same build
  identity, from one append-stable live overlay source**, with per-row provenance that separates the two halves. One
  coverage interval, one ordered index, one source identity. **Nothing selects this window yet**; the
  `ActiveCaptureOverdub` echo and its edge sort stay authoritative (the 3.15d slice-1 pattern).
- **Production entry:** none (build only). Files: `RuntimePlaybackViewFill` (continuation entry, provenance, named
  refusal), `RuntimePlaybackViewBuild` (attach, ordering continuation, staleness invalidation), `RuntimePlaybackView.h`
  (the named sentinel constants).
- **Explicit non-goals:** no request/admission (slice 2), no edge adoption and no echo removal (slice 3), no ledger
  emission change (the sentinel is metadata for slice 3), no pass-handoff fixture (slice 4), no device claim (3.15f).
- **Constraints from the owner:** one build identity; an explicitly named overlay-origin sentinel; capacity accounted over
  base **plus** overlay with a named refusal and no partial window; monotonic append; pass commit invalidates the
  overlay identity; the build exposes the live capture generation it carries.

## Oracle fixed before the code

| Case | Expected |
|---|---|
| base + overlay | one canonical `(tick, note, on/off)` sequence: `Off@10(note64, overlay)`, `On@10(note60, base)`, `On@20(62)`, `Off@30(62)`, `Off@40(60, base)` — the equal-tick tie is the canonical class rule (Off < On), not source order |
| provenance | base rows keep the baseline's pass index (`0`), overlay rows carry `kLiveOverlayPassOrigin` with their own ordinal and `kLiveOverlayCanonicalRank`; `passOriginsReady` true, `canonicalRanksReady` false |
| identity | `view.source` carries the base's revision/generation **and** the live capture generation; `overlayCaptureGeneration()` equals it |
| append | one more overlay note extends the same build; 7 rows, every earlier row exactly once, new pair in canonical position |
| invalidation | after a pass commit (`beginCapture`) `nextGrain()` is `Denied`, `continuationRefusal()` is `ContinuationStale`, `view.complete`/`sourceFilled` are cleared, `extendLiveCapture` refuses, and a **fresh** build from the committed source yields the 2 base rows only |
| combined capacity | base 2 + overlay 3 in a 4-row window: `CombinedCapacityExceeded`, `storage.eventCount == 0`, `complete`/`passOriginsReady` false |
| production | the 3.15e emission oracle and the whole native suite stay green (the echo is untouched) |

## Verification recipe

`pio test -e native -f test_playback_prewarm` (new cases + the suite), `pio test -e native`, both firmware builds (RAM1
pins must be byte-identical: no new buffer, no new field on the target ABI), four mutations, `openspec validate --strict`,
`git diff --check`. Device: NOT RUN (3.15f).
