# 2.8 — consolidated red-proof accounting (1.2)

**Rule.** No transferred residual is checked off by declaration. For each residual this file names
the claim, the **discriminating RED** (a mutation or fixture that fails only if the claim is false),
the exact failure(s), the **byte-identical restore**, and the GREEN gate that was run afterwards.
2.2 and 2.4 were measured in the earlier slices and are cited from
[`PROGRESS.md`](PROGRESS.md); 2.9, 4.1 and 4.2 were re-measured for this accounting on 2026-10-24.

Final clean gates: **storage 63/63**, **RWO 241/241**, full native **3,443 / 7 skipped / 3,436**,
both firmware builds at the approved pins.

## Summary

| residual | discriminating RED | RED result | restore | GREEN |
|---|---|---|---|---|
| 2.2 derived deadline | `handoffLeadTicks` replaced by fixed `loopLengthTicks / 8` | **28** lead-dependent tests red (3.7h positioning, 3.10 envelope, demoted-slot length sweep) | byte-identical | focused **240/240** at the time |
| 2.4 commit-edge capacity | `prewarmPlaybackForSlot(targetSlot, true)` on the commit edge | **28** tests fail, incl. both `test_s4b_7_*` | byte-identical | focused **240/240** at the time |
| 2.9 retained-source hold/release | delete the `runtime->coverageBuildNeedsService()` hold clause in `tryReleaseRetainedPlaybackSource` | **1** fail: `test_retained_source_holds_a_paused_build_across_a_replacement_pass` — “a paused build still needs its source” (1 failed / 240 passed) | `TrackPlaybackWindowBuild.cpp` SHA-256 `2793f5b3…`, `cmp` identical | **241/241** |
| 4.1 content-level undo parity | re-derive the root in `applyPublishedSnapshotSelect` (`make_shared<const LoopSnapshot>(*ref)`) instead of selecting the entry’s object | **1** fail: `test_checkpoint_parity_names_the_lagging_case_and_ignores_the_windows_lag` — “the restored checkpoint is the exact root the entry named, not a re-derived one” (1 failed / 240 passed) | `TrackUndo.cpp` SHA-256 `f708d8b7…`, `cmp` identical | **241/241** |
| 4.2 content-divergence oracle | `checkpointEventsEquivalent` forced to `return true` (parity compare disabled) | **5** fail: divergent root, velocity divergence, serialized-root divergence, same-tick divergence, existing-slot transaction (5 failed / 58 passed); the passes-only control, wrap closure and dense-stream tests stay green | `StorageLoopIo.cpp` SHA-256 `b2cc5076…`, `cmp` identical | **63/63** + device leg below |

The no-op variant for 2.4 (`ensurePlaybackMergedEventsForSlot` inserted inline) is recorded in
`PROGRESS.md`: with no runtime there is no build, so it does not discriminate; the capacity form is the
mutation that does.

## 4.2 — device leg (capstone)

The 4.2 oracle was additionally exercised on the qualification image with a device-written body
(bench track 8 / slot 5 = firmware `t=7,s=4`, 34 events):

- coherent `result=ok, events=34, walk_us=120–125`; uploaded coherent control `ok`;
- one-byte root-velocity patch (tick 320, note 64, 100→90; passes byte-identical) →
  **`result=divergent, events=34, walk_us=61`**, slot **no current** (`content_identity=0`,
  `root_identity=0`), `CAPTURE_CONTENT` empty; original restored and re-verified;
- an existing device body on track 8 slot 4 also reads `divergent, events=26` — not only a fixture;
- walk share ≈10% of `LoadLoopBudget::FocusRestoreUs = 1,200 µs`; no apply-overshoot row.

`sizeof(MidiEvent)` is 32 B native vs 20 B device, so host-written LSV1 bodies are not device-valid;
the leg used the device-written body plus a one-byte patch. Detail:
[`../2026-10-24-4.2-device-leg/EVIDENCE.md`](../2026-10-24-4.2-device-leg/EVIDENCE.md).

## Not claimed

- 2.9’s production **retainer consumer**: the matrix is test-driven by owner decision;
  `retainPlaybackSourceForSlot` still has no production caller (activation follow-up if needed).
- 4.1’s **production parity consumer**: `checkpointParity` remains an oracle; the 4.2 load path is its
  first production consumer.
- **3.7b**, simultaneous-track Record, layered/jam, short-loop Record/overdub, dense content-pipeline
  terms, mixed-oracle A-retirement terms, and `ExecutionBounds.md` row 2 reclassification.
- The activation and shipping parity themselves (1.3–1.5).
