# S4f.18 — Record normalization oracle

Status: normalization oracle native-verified; bounded firmware preparation remains open. Baseline 2ce30f2a; unrelated HITL changes preserved.
Authority: SHARED-CAPTURE-FINALIZATION.md and DEC-093 — shared capture finalization.

## Scope / G0 evidence

`prepareRecordStop` derives final length, closes held notes, projects overflow
NoteOns and drops remaining overflow rows. `Loop::sealCapture` then sorts,
assigns missing On identities, repairs tail pairs, applies minimum-length policy
and stages/publishes the pass. The frozen-source reader currently projects ticks
only; the new job closure consumer supplies held Offs only. Neither proves the
remaining normalization semantics. The fixed overdub staging cannot hold the
whole Record envelope. These are VERIFIED from the production call chain.

No new firmware state, owner, allocation, or publication route in this slice.
Extend the existing real-stop fixture before replacing those normalization
operations. G0 PASS for the oracle; production bounded normalization remains open.
Undo/storage owners are linked by the existing harness. No manager/button timing,
audible playback or device deadline claim follows from these content tests.

## G2 oracle fixed before edits

- Overflow: final length 768; a raw On at 780 becomes 12, its raw Off at 820
  is discarded, and tail repair supplies Off at the stop phase 132. Another On
  at 700 with discarded Off at 800 gets terminal Off at 767, not 132. Both
  On identities survive; synthetic Off identity follows existing policy.
- Minimum length: with threshold 48, a 20-tick pair disappears and a 48-tick
  pair survives. With filtering disabled both survive. Different channels/pitches
  isolate pairs. Keep an in-range CC; reject overflow CC. Assert complete row
  inventory, not only pass count or a subset of rows.
- Exercise stopped and playing Record stop entry points with identical source
  and geometry (bar-aligned origin to avoid unrelated launch alignment).
- Long Record: accept and publish more than 2,048 input rows, preserving every
  pair/identity. Derive a fixed test count above the actual staging capacity;
  do not claim that this legacy synchronous path proves bounded preparation.
- Empty after filtering: no fabricated record pass or geometry after real stop.

TIGHTENING: distinguish physical held Offs from synthetic repair Offs; do not
silently replace the legacy inventory with the new helper's output. Test globals
must reset per case. COVERED: no asynchronous binding/slot change introduced;
those remain covered by existing closure/rotation fixtures. No altered oracle
or input envelope is permitted to make future deferred preparation pass.

## Verification recipe

Focused shared-finalization suite, guard-removal mutations for overflow projection,
tail repair and minimum-length removal, restored full native regression, strict
OpenSpec and whitespace validation. Firmware build/device run not required for
host-test-only changes. Registered integration stimulus remains unchanged.
No parent task checkoff, no new decision/exclusion, no shipped deliverable.

## Measured fixture boundary

Both real `stopRecording` and `stopRecordingToStopped` now run. The former
initially hit the harness's fail-closed `reanchorPlaybackProjection` stand-in.
The shared-finalization suite opts into the complete production
`TrackPlaybackHotPath.cpp` through a separate owner-family macro; its reanchor,
ledger clear and silence methods are real. Other suites retain their existing
silence counters and fail-closed stubs. MIDI hardware remains the native boundary;
this still makes no emitted-device-MIDI or playback-deadline claim.

The initial large-take fixture ended inside the content-length grace window,
so 49,920 expected ticks became 49,152 under the real length policy. The final
fixture uses 1,028 pairs (2,056 rows), ends past that grace window and asserts
all original rows/NoteIds at length 49,920. The overflow fixture separately pins
intentional truncation. This is a setup correction, not a changed production policy.

Focused suite: 29/29, including the five new normalization cases. Stopped and
playing inventories agree; seven rows with filtering, nine without it. Synthetic
repair Offs have invalid NoteId under the current policy; physical Offs retain
the accepted On identity. An entirely filtered take resets length and has no pass.

## Remaining bounded implementation obligations

- Projection, pair repair and short-pair removal must produce the same canonical
  and audible content, including their effects on identities and empty captures.
- Record preparation must stream canonical content beyond 2,048 rows without a
  whole-take mirror or a take-sized derived ordering array. Existing fixed
  overdub order/output facilities alone are not an envelope proof.
- Pairing must preserve channel/pitch and the accepted identities; synthetic
  repair behavior must not be mistaken for the pending-held-note receipt path.
- Deferred policy inputs must belong to the admitted Record job; reading changed
  live producer geometry or a successor's state during preparation is invalid.
- These oracles intentionally exercise existing inline normalization. They do
  not close admission, bounded normalization, pass construction, first-bar/onward
  coverage, undo/publication, manager continuations, or hardware acceptance.

Four restored-source mutations fail the intended fixtures: omitting overflow
projection changes inventories; omitting tail repair loses two synthetic Offs;
disabling minimum-length removal retains the short pair and incorrectly creates
a pass for the filtered-empty case; imposing the overdub 2,048-row cap loses the
2,056-row Record take. All production files were restored byte-for-byte before
the final native run. Inventories are independent of physical append order;
canonical ordering is a separate remaining bounded-preparation obligation.

## Final verification / closeout

- Full restored native regression: **2,518 passed / 7 skipped**, 2,525 total,
  1m39.547s. The shared-finalization suite passes 29/29 within that run.
- All four mutants fail assertions, not compilation; production diff is empty.
- Strict runtime OpenSpec validation and whitespace checks pass.
- No firmware build/upload, serial action or SD action: host fixture changes only.
- Runtime state/current work and OpenSpec task progress updated. No shipped
  deliverable, architectural decision change or parent task completion.
