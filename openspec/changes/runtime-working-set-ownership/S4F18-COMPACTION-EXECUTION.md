# S4f.18 — constant-work canonical compaction retirement

Status: verified retirement correction; Record normalization integration remains open. Baseline 608d0d91. Unrelated working changes preserved.
Authority: shared capture finalization contract; DEC-043 — execution bounds;
DEC-093 — shared capture finalization.

G0 VERIFIED: `LoopPlaybackRuntime::stepFrozenCaptureCanonicalRewrite` delegates
to `LoopEventStore::stepRewriteFromOrderedSource`, which copies a bounded row
quantum and then walks retained chunks before retiring surplus references. Its
surplus branch currently calls vector erase at the retained-prefix boundary,
shifting the complete remaining suffix. One retired reference is therefore not
one bounded directory operation. `releaseChunkReference` only decrements one
counter; the erase is the hidden variable-cost operation. Published/raw source
ownership is outside this helper; this slice changes no source admission.
The current finalizer calls `stepFrozenCaptureCanonicalCopy`, not the in-place
rewrite API. Repository call search finds the rewrite wrapper exercised by native
fixtures only. This corrects a reusable implementation boundary, not an observed
active-finalizer/device stall; no production route is switched.

G1: extend the existing store helper, retaining the prefix and popping surplus
references from the back. No fields, new owner, queue, allocation, new sorting
policy or publication route. Final rows and retained chunk order are identical.
Temporary suffix-release order is internal to the unpublished rewrite. Existing
owner/pool generation checks remain authoritative.

G2 PASS: each trim call either visits one retained chunk, clips the final retained
chunk, pops/releases one tail reference, or completes. No surviving directory
entry moves during retirement. Cursor target boundary stays fixed while the tail
shrinks. Completion alone publishes event count/last tick and dirties the bar
index, as before. Stale owner/pool and zero quantum must preserve references.
No change to when retired zero-reference chunks are physically reclaimed.

Fixed oracle before edits: real store has 4,097 rows across 17 chunks. Rewrite to
0/1/256/257/2,056 rows; each retirement leaves exactly the original directory
prefix, drops at most one reference and leaves all retained references intact.
Every final row matches the supplied ordered source. Zero-quantum and stale-owner
calls deny without releasing another chunk. Empty completion is idempotent.
A watchdog bounds copy turns plus twice the original chunk count and terminal
steps. Baseline must fail the prefix-retention assertion. Mutation restoring
middle erase must fail; mutation releasing the wrong ID while popping the tail
must fail reference assertions.

This corrects the existing in-place rewrite implementation prerequisite; it does not implement
Record ordering, the canonical filter writer or shared admission. Full native,
capture-serial build (RAM1 padding >=840), strict OpenSpec and diff checks are
required. No layout growth, new serial behavior, upload or HITL acceptance claim.

## Focused evidence

The unchanged implementation fails `test_rewrite_retires_only_directory_tail`
at the first retired-prefix assertion (expected chunk 0, observed chunk 1):
`/tmp/compact-baseline.log`. The correction passes all **196** capture-range and
playback-prewarm cases (`/tmp/compact-focused.log`). A second source mutation
kept tail-pop but released the first surplus ID; the fixture failed its retained
reference assertion (expected 1, observed 0). Log `/tmp/compact-wrong-reference.log`.
Original corrected source restored before broad checks. No fields or layouts
changed; source admission, row rewrite, completion and persistence policy remain
unchanged. Physical reclamation remains with the existing owner.

Session closeout: implementation correction within DEC-043 — execution bounds
and DEC-093 — shared capture finalization. No new decision, exclusion or shipped
deliverable. Record ordering, bounded minimum-length canonical compaction and
production shared admission remain open; parent task is not checked off.

## Final verification

- `pio test -e native`: **2,543 passed / 7 skipped**, 2,550 total;
  `/tmp/compact-final-native.log`.
- `pio run -e teensy41-capture-serial`: PASS; FLASH code **1,044,488**,
  RAM1 variables 96,160 / code 392,376 / padding **840** / locals 34,912;
  RAM2 155,232; EXTRAM 9,312. `/tmp/compact-final-build.log`. No upload.
- No field/layout changes. Strict OpenSpec validation and whitespace checks pass.
  Parent progress stays **50/176**. No device or completed-normalization claim.
