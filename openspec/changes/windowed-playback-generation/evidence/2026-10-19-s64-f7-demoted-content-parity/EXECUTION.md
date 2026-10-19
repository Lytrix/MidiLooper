# F-7 attempt — the demoted target is resident and plays, and the shipped verifier cannot slice its cycles

**Date:** 2026-10-19 · **Base:** `e625603d` · **Branch:** `feature/unified-rolling-playback-path` · **Task:** 4.3 (F-7) under 6.4
· **Type:** host tooling + one device read and one device capture. **No firmware line changed**, no upload.
**Verdict: F-7 stays NOT RUN.** The reason is measured and named, not inferred.

## What this run established

### 1. The demoted target is resident, and it survives a cold boot

Read-only dump of the registered leg's own authorized target, reached **without a destructive gesture**
(`--select-track 8 --dump-slot 4 --dump-only`; the track button is a `SELECT_TRACK`, the slot is named in the dump
request, and the loop button — whose short press is a record toggle — is never pressed):

```
[4261.922] [INFO] Button press: Track 8 (short)
CAPTURE_CONTENT,begin,track=8,slot=3,length=19200
CAPTURE_CONTENT,rows,record,0,1152
CAPTURE_CONTENT,rows,effective,0,1152
CAPTURE_CONTENT,end
```

`slot=3` is 0-based slot 4. `record == effective == 1152` rows, so `storage_matches_effective` holds on the demoted
target. The playback run re-took the same dump **after a boot** (its log carries rows from ~74 ms device uptime), and
the take was still there: the restore brings the demoted loop back intact.

Evidence: `captures/i5-s64-f7-residency-01/` (dump at line 44 010) and `captures/i5-s64-f7-playback-01/`.

### 2. The demoted target plays, and it plays repeatedly

A 170.0 s transport capture (`--capture-seconds 170`) of the same target:

| Reading | Value |
|---|---|
| MIDI rows in the capture | 7 198 |
| note_on / note_off on the target channel (ch 8) | **1 307 / 1 306** |
| CC74 on ch 8 | 1 331 |
| `effective` note_ons per loop | 384 |
| observed ratio | **≈ 3.4 loop cycles** |

So the loop is served and repeats; this is not a silent or stalled playback. (The last note-on has no matching
note-off because the capture ends mid-loop.)

### 3. Why there is no verdict

`verify_playback_content.py` returned:

```
{"result": "INCONCLUSIVE_CAPTURE",
 "reason": "no unique effective event recurs in the playback window",
 "effective_rows_all": 1152}
```

It slices cycles by anchoring on a signature that occurs **exactly once** in `effective`
(`exp_counts[sig] == 1 and act_counts[sig] >= 2`). This fixture has **no unique signature at all**: `effective` is
1 152 rows = 384 note_on + 384 note_off + 384 CC74, forming **160 distinct signatures that each occur 3 or 32 times**
(an ascending 12-pitch pattern repeated 32×, and a CC74 value sweep 0–127 repeated 3×). The anchor rule is therefore
unsatisfiable by construction, and the tool correctly declines rather than guessing a phase.

**This is a fixture/verifier mismatch, not a parity failure.** Nothing in the capture contradicts parity; the capture
simply cannot be cut into cycles by the shipped rule, so no per-cycle `inventory_equal` exists to read.

## What landed (host tooling only)

| File | Change |
|---|---|
| `.agents/skills/hitl-content-verification/scripts/dump_and_capture.py` | `--select-track N` (short-presses the track button through the action layer's `slots.press_track`), `--dump-slot N` (sends `!CAPTURE_CONTENT_SLOT n-1`, the firmware's 0-based index), `--select-settle-s`, `--press-ms`, and `--dump-only` (write the dump, skip playback). Defaults are unchanged, so the skill's existing recipe is byte-identical in behaviour. |
| `scripts/test_dump_and_capture_target.py` | 4 tests: the unnamed request, the 1-based → 0-based conversion, the canonical track note (67 on channel 16 for track 8), and that no loop-button note is ever pressed. |

Host suites: `scripts.test_dump_and_capture_target` **4 OK**. The extension is additive: `--dump-slot` absent keeps
`!CAPTURE_CONTENT` and the active slot exactly as before.

## The two routes to a verdict (neither taken)

1. **Slice by event count instead of by a unique signature.** The loop's `effective` list is complete and ordered, so
   cycles can be cut as consecutive chunks of `len(expected_full)` events and each chunk compared as a multiset. This
   is the same comparison — only the cut changes — and it works on a fully periodic fixture. It is a change to a shared
   verifier, so it wants its own tests and a considered failure mode (a dropped event shifts every later chunk, which
   fails loudly rather than quietly).
2. **Give the fixture an anchor**: one distinctive event (unique pitch/velocity) somewhere in the demoted take. That
   means writing to the authorized target (an overdub), which is a separate authorization step, and it changes the
   content under test.

Route 1 is preferred: it fixes the tool for the general case (periodic looper content is normal) and needs no device
run. Route 2 would unblock a verdict in one short session but leaves the verifier unable to read periodic content.

## What must not be claimed

* **F-7 is not passed.** No `content-review.json` with per-cycle `inventory_equal` exists for the demoted target.
* **F-8 is not advanced into a verdict** by this: content residency after restore is *one* datum, and F-8's required
  sweep over loop lengths still owes the demoted lengths reached and read per length.
* `oracle_complete` stays `False`; 6.4 stays open.
