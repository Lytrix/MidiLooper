# S4f.18 — bounded minimum-length pair decisions

Status: verified prerequisite; canonical compaction and production integration remain open. Baseline `01c03925`; unrelated HITL working changes preserved.
Authority: SHARED-CAPTURE-FINALIZATION.md, DEC-093 — shared capture finalization.

## Discovery and contract

VERIFIED: `Loop::sealCapture` runs tail repair before
`CaptureIncrementalSanity::removePairsShorterThanNoteMinLength`; OverdubWrap skips
minimum-length removal. That helper sorts a whole-take copy by tick/Off/On/other,
tracks the most recent On per channel/pitch, consumes it on the first subsequent
Off and drops both rows only for a strictly shorter linear span. NoteId does not
select the pair. A head Off with no earlier On survives; equal-tick Off precedes
On. If no pair is removed the helper does not reload/reorder its source store.
The full-copy, ordering, removal bitmap and reload remain synchronous today.

Extend that existing policy namespace with a borrowed fixed MIDI-key decision
workspace and cursor. It consumes an already ordered stream one row per call;
it does not own source rows, sort, mutate the source, compact canonical output or
publish anything. Each short pair reports the two source ordinals and remains
pending until acknowledged. No next input or completion is allowed while pending.
The caller must apply decisions only to this same generation's ordered source.
No per-event bitmap/mirror is introduced. Sorting and the bounded canonical
compactor remain explicit future dependencies; this primitive alone is not a
completed filter and does not switch production Record dispatch.

Workspace has 2,048 lanes of On tick plus source ordinal, independent of take
length (ARM measured 16,384 bytes). Exclusive caller-owned scratch
and generation must outlive discard; no allocation or resident owner field is
added. Future preparation admission must prewarm and budget the role. The cursor
copies geometry, minimum length and enable policy on begin. Reset visits at most
Q lanes; input/decision/acknowledge/finish/discard are constant work. Input rejects
out-of-order rows, invalid note channels/pitches and ordinal overflow before any
policy state changes. Stale generations deny access and leave output untouched.

## G0/G1/G2 and decision review

G0 PASS: entry and existing owner traced above; source/pass ownership, undo,
persistence, playback and display remain with their existing owners. Search terms:
minimum-length, normalization, sorting, DEC-093. No logged decision superseded,
new owner, new queue, schema, input cap or policy exclusion. G1: existing policy
extension under the shared finalization contract. No reassessment trigger.

G2 PASS WITH TIGHTENING: callers must not equate acknowledged decisions with a
fully compacted/publishable pass. Pending pair consumes its Off exactly once;
retry its receipt, not the input. Upstream ordering retains the existing equal-key
semantics; this cursor must not invent a NoteId tie-break. Source/generation and
scratch are exclusive through discard. No current active/selected slot is read.

Covered attacks: delayed completion and stale/reset/reuse; interleaved independent
cursors; short/long loops and >2,048 input rows; disabled/zero policy; retriggers,
channels, velocity-zero Offs and orphan/head Offs; source order and pending output
retries. Slot/track switches, clock/transport, undo, persistence and playback do
not enter this isolated policy primitive; their production integration gates
remain open. No stack workspace or synchronous drain is added to firmware.

## Fixed transition oracle and verification

Reset must visit exactly 2,048 lanes over bounded steps. Ordered input increments
its ordinal exactly once; short Off returns PairReady with exact On/Off ordinals,
repeated reads return identical receipts, further consume/finish deny until ack.
A retrigger replaces the candidate, leaving the earlier On untouched. A long Off
also clears the candidate; a following short Off cannot pair with it. Strict
threshold equality survives. Same-tick Off-before-On cannot close that On.
After finish, no input is accepted; completion is idempotent. Stale generation
must reject read/ack/consume/finish; discard permits explicit restart/reset.

Handwritten ordinal/retained-row expectations plus deterministic differential
comparison to the real synchronous helper. Tests may apply receipts to a host
bitmap solely for oracle comparison; firmware gains no such bitmap. Each drain
has a bounded watchdog. Mutate threshold strictness, retrigger replacement and
pending-decision gating to demonstrate discrimination. Focused policy and real
shared-finalization fixtures, full native, capture-serial build (RAM1 padding
>=840), ARM layout, strict OpenSpec and diff checks. No upload/HITL claim; existing
STIM-BASE-2-ONE-OD-01@v1 remains the eventual integration stimulus. Parent stays open.

## Focused evidence and review

Eight fixtures passed (`/tmp/minimum-focused.log`). Handwritten expectations cover
strict threshold, disabled/zero/empty policy, last-On retriggers regardless of
NoteId, long-Off candidate retirement, channel separation, head Offs, equal-tick
Off-before-On, velocity-zero Offs, invalid lanes, denied order without ordinal
advance, stale receipts, explicit restart and independent cursors. All MIDI lanes
are active together over 4,096 input rows. Sixty-four fixed-seed differential
cases compare the retained event inventory including identity and velocity to
`removePairsShorterThanNoteMinLength` through the real store implementation.
Differential cases use unique ticks after generation to avoid invoking the legacy
unstable sort twice with a different equal-key identity order. Handwritten cases
cover the relevant same-tick decision boundary directly; this is not proof of a
replacement sorter or its equal-key ordering.

Three deliberate source mutations fail assertions: threshold changed to <=,
retrigger kept the first candidate, and input allowed while a pair was pending.
Logs `/tmp/minimum-mutation-{threshold,retrigger,pending}.log`; original source
restored before full native/build. ARM symbol measurement confirms **48-byte
cursor / 16,384-byte workspace**. No Track, Loop, ring or per-slot runtime field.
The new methods are FLASH placed and currently unreferenced by production callers.
No integrated memory envelope or device timing is inferred from this measurement.

Session closeout: pure extension of DEC-093 — shared capture finalization, with no
new owner, queue, schema or exclusions. No new decision entry; no shipped deliverable.
Canonical sorting/compaction and production Record dispatch remain the next work.

## Final verification

- Full native: **2,541 passed / 7 skipped**, 2,548 total, including eight new
  decision fixtures and 31 real shared-finalization fixtures. Command `pio test
  -e native`; log `/tmp/minimum-final-native.log`.
- `pio run -e teensy41-capture-serial`: PASS, unchanged footprint. FLASH code
  1,044,504; RAM1 variables 96,160 / code 392,376 / padding **840** / locals 34,912;
  RAM2 155,232; EXTRAM 9,312. Log `/tmp/minimum-final-build.log`. Linker removal of
  unreferenced methods means this is not the integrated helper's resource budget.
- ARM layout: cursor **48**, workspace **16,384** bytes. No resident field added.
- Strict OpenSpec validation and whitespace checks pass. No upload or HITL run.
  Parent tasks stay unchecked, **50/176** complete. No acceptance criterion changed.
