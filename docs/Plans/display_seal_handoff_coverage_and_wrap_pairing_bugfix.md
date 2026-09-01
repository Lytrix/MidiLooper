# Display seal handoff — coverage over-claim and wrap pairing

**Status:** RC1 committed (`c74b8e21`). RC2 pin-first fixture is now implementing — see [`overdub_wrap_companion_and_pairing_bugfix.md`](overdub_wrap_companion_and_pairing_bugfix.md).
**Date:** 2026-09-01
**Kind:** bugfix
**Parent:** [`display_seal_handoff_capture_retention_bugfix.md`](display_seal_handoff_capture_retention_bugfix.md)
**Evidence:** [`153517`](../../captures/session_20260901_153517.log)

**Approved:** RC1 and RC2 as separate commits. Retention predicate stays unchanged.

**Debugging boundary:**

```text
seal retention lifetime (150650)
    ← predicate is not wrong; builtFromRevision over-claimed
visualCache.builtFromRevision over-claim after OverlayRejected fallback
    ← RC1, wrap flash
published snapshot note 88 wrap-held 2304→864
    ← RC2, bar 4 tail to loop end
```

## Isolation

```text
RC1  false cache coverage → premature retention release → stale visual flash
RC2  wrong journal pairing → wrong authoritative snapshot geometry → bar-4 lengthen
```

RC1 must not change snapshot geometry. RC2 must not rely on capture retention.

## Coverage contract

Every path that stamps `builtFromRevision = R` must establish that the resulting cache
content includes the committed representation for R. A fallback that rejects the current
overlay does not establish that coverage.

Wrong: the cache rebuild happened while `playbackRevision` happened to be R.
Right: the completed gather actually consumed committed content at R.

`slice_keep` keeps older notes and must not stamp R.

## Stage 1 native assertions (`153517`)

```text
snapshot = 8
playback = 10
fallback = OverlayRejected
builtFromRevision != 10
sealedCapturePreviewIsCovered() == false
retained rows != empty
```

## Stage 2 pin-first fixture

```text
pass N: On(id A, 2304) Off(id A, 2784) → id A closed
wrap / beginCapture
pass N+1: On(id B, 768) Off(id B, 864) → id B independent
journal drain → neither event closes the other
```

Assert the matching key. Do not assume pitch or position until the fixture proves it.
No painter workaround.

## Residuals

Phase-0 occupancy `hs=0` n=0; effective `p64 s0 len3072` if it survives RC2.
