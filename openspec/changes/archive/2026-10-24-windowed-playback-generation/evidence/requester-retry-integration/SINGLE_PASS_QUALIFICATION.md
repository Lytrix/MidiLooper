# Single-pass qualification/fill — execution packet

Task3.10 — live requester/retry/service qualification; DEC-108 — window publication,
existing private builder/source ownership, fixed quanta and original deadlines.
Historical review: RuntimePlaybackViewFill already implements bounded inspection,
canonical keys/event-ID encoding, capacity refusal and cursor-token validation.
Qualification currently reads and discards every event, then rewinds for the same
fill. Reuse the existing typed fill against the reserved unpublished destination.
No new buffer, owner, scheduler, source lease or timing/capacity/RAM1 rebase.

Transition oracle, fixed before edits: freeze reserves builder and typed source;
qualification charges <=64 source inspections/transitions and copies matching rows
into private storage. It neither orders nor completes/publishes the view. After a
complete pass, retain count/topology and pause. Admission still validates original
absolute origin/deadline, source/generation/geometry and sibling promises; launch
remains explicitly unpositioned. Successful admission starts canonical ordering
without rewind/refill. Only remaining work is charged: fill0, existing two sorts,
ranks and conservative promotion/retirement. Denial/cancel invalidates candidate
metadata and drains pins, preserving serving/ready views and newer demand.

A 2,049th matching row must be named overflow, never silently truncated. Cursor
rewind/rebind during copying remains a denial. Empty completion is valid silence;
no completion is synthesized at admission. Existing all-source/borrowed APIs keep
their separate fill phase and accounting.

Tests: real Track retry fixture proves private fill before admission, zero admitted
fill grains, unchanged original deadline and newer demand, final flat-oracle parity.
Dense2048 runtime fixture measures one2,178-unit qualification pass, then1,153 order
turns without source reread; partial cancellation/current isolation and overflow
remain covered. Native focused + full, capture/RAM1, strict validation and diff.
Mutation removes qualification fill and/or restores fill charge; assertions must
fail, with byte-identical restoration. No device upload assumed.

Expected work reduction only: dense lifecycle1226->1191 selected turns, empty6->5.
At H8/gap64 only3 turns/window/holder are supplied; neither qualifies continuous
2-bar refill. Cold-root publication (~28s device result) is a separate dependency,
unchanged here. New combined grain has the existing fill's inspection/copy shape;
device duration and a guaranteed service envelope remain unqualified.
Status: specified, verification pending.

## Verified completion
- Focused ownership123/123 and prewarm197/197 (320/320).
- Dense runtime uses real qualification/admission/order APIs and an independently
  sorted2048-event oracle. Qualification copies all rows in35 turns/2178 units;
  candidate remains incomplete/unpublished. Cursor generation and read count stay
  unchanged across admission and ordering. Orders/ranks1153 turns; shared retirement1;
  freeze+admit2:1191 lifecycle turns, versus1226 before this slice.
- Real Track request test verifies61 private rows after64 inspected units, original
  origin/deadline/epoch and newer pending demand retained, zero admitted fill grains,
  and final content parity. An admission boundary with2500ticks remaining now fits
  the2304-tick remaining-work charge, not the former2816 duplicate-fill charge.
- Partial-fill rewind fails token validation, retires pins and preserves a serving
  sentinel view. Existing2049-overflow refusal and legacy fallback tests pass.
- Eight-track initial empty preparation48->40 selected turns (6->5/holder), while
  the negative two-bar-lead fixture still completes0 with original demands retained.
- Three mutations caught at named assertions: private copied rows discarded;
  admission rewinds the cursor; completed fill charged again. Byte-identical restores.
- Initial focused run failed only old expected6turns/0private-rows assertions;
  updated to measured5/61 with stronger privacy/origin assertions. Not counted green.
- Full `pio test -e native`:2777 total,2770 passed,7 skipped,0 failed,195.369s.
- `pio run -e teensy41-capture-serial`:SUCCESS44.494s; RAM1 variables96192,
  code391512,padding1704,locals34880; floor840 unchanged. Runtime size pins remain
  target7072/host9096; no fields/event buffers added.
- Strict OpenSpec and diff whitespace checks pass. Device/HITL NOT RUN; no upload.
  Board's last verified rootless-hold fix remains5e8a81b1...; this optimization has
  no hardware duration or parity claim. Build/source hashes in the manifest.

## Qualification verdict / next decision
NOT QUALIFIED for continuous two-bar refill. At H8/gap64 the available steady rate
is3 turns/window/holder, while even the empty lifecycle costs5 and copying2048
rows alone requires>=32. Initial prefill and this saved traversal cannot close the
rate deficit. A supported service/work model and hardware grain/opportunity bounds
are needed before activation; no fabricated guarantee or reduced density is used.
The whole-root cold preparation dependency that took~28s on device is unchanged.
No global admission gate flip, fallback removal, DEC supersession or shipped
qualification. Existing-owner implementation only; no new ownership decision.
