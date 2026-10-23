# G2 execution packet — F-7: the demoted target's served stream against `effective`

**Task:** 4.3 (F-7, content parity across a window boundary) under 6.4 · **Branch:** `feature/unified-rolling-playback-path`
· **Base:** `e625603d` · **Type:** host tooling + one device run (content pipeline). No firmware line is planned.

## Scope (pinned)

In scope:

1. Give the content pipeline a way to reach the **demoted target** (track 8 / slot 4, the registered windowed profile's
   authorized target) without a destructive gesture: `--select-track N` (`SELECT_TRACK`, non-destructive) and
   `--dump-slot N` (`!CAPTURE_CONTENT_SLOT`, which names the slot on the selected track) on the skill's
   `dump_and_capture.py`, plus `--dump-only` for the read-only residency check.
2. Read residency from the dump the run itself prints (`CAPTURE_CONTENT,begin,…,length=N`), and:
   * **resident** → capture ≥3 loop cycles and read per-cycle `inventory_equal` from `content-review.json`;
   * **not resident** → record the finding and stop; re-recording is the registered windowed profile's own run, a
     separate authorization step.
3. Record the F-7 verdict (or its measured non-verdict) in the change's evidence and task note.

Out of scope: F-8's length sweep, any firmware change, any oracle change (`verify_playback_content.py` is used as
shipped — its per-cycle `inventory_equal` **is** F-7's claim), any clear or record of a slot other than the
authorized target.

## Transition oracle (what decides)

`verify_playback_content.py` ≥ shipped. **PASS** requires `inventory_equal: true` on every compared cycle, where each
cycle is the captured MIDI-out inventory between two anchor occurrences compared against the dump's
`effective` section (`LoopPasses::materializeToEventVector`). The claim F-7 makes — "the stream served by successive
windows concatenates to the whole-loop stream" — is exactly "each cycle's inventory equals `effective`", because on a
demoted slot the served stream **is** the window lane's output.

A cycle is only count-able if the capture holds ≥2 anchor occurrences (1 compared cycle) and the dump is complete
(`CAPTURE_CONTENT,end` present, declared counts matching parsed rows). `INCONCLUSIVE_CAPTURE` is not a pass.

## Controllable fixtures (no device needed)

* `dump_and_capture.py` selected/dump-slot argv → the emitted command text and the track-select note (unit test on the
  pure helpers; no port opened).
* `verify_playback_content.py` already has its verdict logic exercised through `hitl.retention_probe` fixtures; this
  slice does not change it.

## Verification evidence (to fill)

| Gate | Expectation |
|---|---|
| host tests (new + affected suites) | green, counts recorded |
| dump identity | `CAPTURE_CONTENT,begin,track=8,slot=3,length=…` — the declared target, not another slot |
| capture | ≥3 anchor occurrences; ≥1 compared cycle |
| `content-review.json` | `cycles[].inventory_equal` read per cycle; verdict recorded verbatim |
| durability | `docs/Runtime/*`, `tasks.md` 4.3, `DELIVERABLE_TRACKING` only if it ships |

## Completion record

**Run 2026-10-19 — F-7 PASS on the demoted target.** `content-review.json`: `PASS`, 3 cycles of 1 152 events each,
`inventory_equal` **and** `exact_order` true on all three, `cycle_cut: event_count` (the take is fully periodic and has
no unique signature), `storage_matches_effective: true`. The window moved during the capture: 80 `playback_build`
rows for the target, window length 1 536 ticks **stepping 768 (1 bar)**, 24 distinct starts (`0…17 664`), **79
advances** — 25 window positions per 25-bar cycle (the span overlaps its neighbour by one bar), so ≈25 boundaries per
cycle. Residency: `track=8,slot=3,length=19200`, record = effective = 1 152 rows, still present after a cold boot.
Gates: new suites 4 OK + 4 OK; pipeline defaults unchanged. No firmware line, no upload, no clear, no record.
