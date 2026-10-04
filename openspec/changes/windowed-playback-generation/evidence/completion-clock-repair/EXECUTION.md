# Task 3.10 — completion-clock repair

Baseline 25542299, clean tree. User requested root-cause fixes.
Authority: DEC-108 — windowed playback §13(a,c,e), Playback.md,
RuntimeArchitecture.md, ExecutionBounds.md. Historical review searches:
clock revalidation, completion, deadline, DEC-108. Reuse Track, ClockManager,
existing candidate lifecycle and main lane dispatch; no new scheduler or migration.

## Root causes and transition oracle (before edits)
1. A completed build stops offering service, so next-turn judgment may never run.
   Read a fresh completion clock in the completing turn, before publication.
   Tests: finish at D-1 then dispatch after D => no miss; final grain crosses D =>
   one miss without an extra worker call; never infer finish time from later polling.
2. Epoch mismatch logging is not revalidation. Retain completed candidate, block
   publication and successor admission until explicit boundary revalidation proves
   qualified new epoch, matching geometry, generation and source currency. Do not
   return to an earlier epoch to simulate recovery. Keep old admitted deadline and
   interval fault history unchanged. No drop of valid serving content.
3. Move deadline sweep to unconditional per-track lane dispatch, independent of
   worker eligibility. Pending candidates remain visible to that sweep, not as
   fictitious fill/order work. Bound is eight tracks by eight slots, no new scheduler.
4. Tick zero and same-tick readmission must not impersonate old request dedup state.
   Reset request accounting explicitly on successful admission, not interval history.

## Controls and verification
Existing ownership fixture, injectable completion-clock reader (production reads
ClockManager), production selector and worker. Forced source/epoch/geometry changes,
late and on-time final grain, no-work dispatch, repeated reads, valid new-epoch
boundary recovery and source preservation. Mutation tests remove the completion
read and publication guard. Focused ownership, full native, capture build, strict
OpenSpec validation, git diff --check. No measured maximum is a timing guarantee.
Device route still lacks a production committed-window requester; a baseline cannot
exercise these synthetic promise cases. No hardware qualification claimed here.

Status: reviewed lifecycle defects repaired and verified; live timing envelope remains unqualified.

## Verification so far
- Focused ownership: 100/100, completion-focused2.log. Four old fixtures initially
  failed because they encoded old-epoch return/publication-without-revalidation;
  rewritten against explicit new-epoch recovery. The on-time fixture controls the
  actual ClockManager completion seam independently of the dispatch sample.
- Full native: 2,743 cases / 2,736 passed / 7 skipped / 0 failed,
  completion-native.log. Capture build SUCCESS, completion-build.log.
- RAM1 variables96,192 / code390,536 / padding2,680 / locals34,880;
  approved padding floor840 unchanged. Target6832 and host8784 runtime pins remain
  unchanged; the invalidation flag fits existing padding. No capacity or RAM1 rebase.
- Baseline device regression and mutation checks pending. No envelope claim.

## Final implementation and results
Root cause was conflating build activity, completion time, publication permission,
and interval fault history. Those now have distinct lifecycle behavior:
- The completing worker reads ClockManager AFTER its final grain. Qualified work
  completed before D publishes; crossing D is counted immediately and the candidate
  is withheld. No extra worker turn is needed. Missing/foreign/backward clock or
  changed geometry cannot permit publication. Legacy unqualified shared builders
  retain existing behavior; live activation remains gated.
- main sweeps each track's admitted deadlines BEFORE offer selection, including
  completed withheld candidates; pending publication is not a fictitious build
  grain. A bounded 8x8 scan, no second scheduler. The worker's additional sweep is
  idempotent. On-time published work owes no further preparation judgment.
- revalidateCompletedPlaybackWindow is an explicit boundary API: qualified clock,
  valid matching loop geometry, complete candidate, operation generation and source
  currency are required. Same-epoch recovery is a following interval entry, not the
  missed entry. New-epoch recovery is validated at the supplied projection boundary,
  without returning to the old epoch or rewriting the original admitted deadline.
  Serving content is preserved; recovery prepares a successor when one is serving.
  The existing boundary consumer retains responsibility for adoption/recovery.
- Admission cannot overwrite a withheld candidate. Per-request counting resets at
  admission (including same-tick admissions), independent of same-interval history.
  Invalidation has an explicit bool, so tick-zero promises are reported correctly.
- Cancel/reset clear retained completion state and qualified work. One bounded
  review found a preview cancellation path that checked only active build work;
  a production cancelPreviewWindowForSlot fixture red-proved it (Expected3 Was2),
  then the predicate was fixed to include the completed withheld candidate.

Final focused ownership101/101. Full `pio test -e native`: 2,744 total, 2,737 passed,
7 skipped, 0 failed. Capture build SUCCESS: variables96,192, code390,536,
padding2,680, locals34,880. Target6832/host8784 runtime pins unchanged; no capacity
reduction, timing-limit change or budget rebase.

Three mutations failed at intended assertions: fresh completion clock replaced by
entry clock (Expected1 Was0, final-grain crossing); publication guard removed
(epoch-invalidated candidate becomes serving); boundary check removed (wrong-phase
revalidation succeeds). Restores byte-identical, SHA256
0578108920120e6e4c59367a957d4e9ba851be44b375deb2ced372f4c2c8a422.

## Device regression
Authoritative command:
`host_midi_hitl.py run --preset base --track 8 --loop-slot 4 --midi-channel 8
--build-upload --out-dir captures/completion-clock-repair-baseline`.
Canonical STIM-BASE-2-2-01@v1 unchanged. Authorized track8 slots4/5 only.
Image dfb21ada3121a92e2eb645448d30372fe6bb134bbdea334dfd12db89a2d5d52d, dirty tree on baseline25542299,
metadata saved in verification.json. PASS, overall_ok=true, capture COMPLETE,
normal stopped/drain shutdown. Advisory record_first_note_too_late remains in the
report. This is a baseline regression, NOT a qualified-window timing workload.

## Remaining work, not reclassified as a pass
The concrete completion/revalidation blockers reviewed on increment28 are fixed.
No production committed-window requester or retry/source-buffer owner is added;
revalidation's boundary caller and live consumer are still gated. O8 retry, live
clock/projection integration, permitted-gap operational qualification and all-holder
contention/device sweeps remain task3.10 work. No claim that 64ticks is a guaranteed
bound or that containment after a miss satisfies normal-operation continuity.
No commit was made in this slice.
