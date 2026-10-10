# Live current-root entry — focused lifecycle review

Single read-only review, delegation `fec0951c-847b-4f69-9a98-f6f66ebec680`, on the restored23-file live slice pinned by `live-entry-final-all-hashes.log`, not the accumulated dirty tree.

## Verdict: REQUEST CHANGES

Concrete finding: `cycleEditSession`, `sendEditSessionChange` and the real short-press wrapper treated a second press in PreviewTail/FeedbackTail as private cancellation. Atomic publication had already made Note session/Select active; cancellation then returned without the normal published-session transition and suppressed SessionOpened feedback permanently. Existing second-press fixture covered only Payload.

Smallest repair requested: distinguish private cancellation from post-publication gestures; after publication cancel obsolete tails and process the gesture through the appropriate existing transition. Add real cycle/short caller coverage at both PreviewTail and FeedbackTail, with final session/FSM and exact surface-event assertions.

Reviewed scalar admission/source qualification, one-grant main wiring, atomic selection publication, real Select lexical skip/final900@700 oracle, both preview bumps, callback-successor token handling, cancellation hooks and input guards. Static-only: no command execution/hash recomputation/native/build/timing/hardware qualification. No speculative cold/stale/feedback cutover requirement imposed on this partial lane.

## Repair and retest

Concrete finding repaired: private preparation second press still cancels unopened entry; after-publication cycle/short/explicit session gestures cancel obsolete tails then perform the normal published-session transition. Six separately registered PreviewTail/FeedbackTail cases cover both real press callers and explicit changes, exact session/FSM/events and no obsolete Note-open. Pre-repair3 failures/98 passes, independently reinstated reviewed defect6 failures/98 passes, repaired entry104/104 and seven focused suites470/470. Parent final full native3,027 passed/7 skipped and capture SUCCESS. No second review or APPROVE verdict claimed.

Initial RAM1 guard failure13 ITCM banks was repaired independently with scoped cold out-of-line cancellation and geometry placement; final12 banks/code392,360/padding856 >=840, only16B margin, locals34,880. Details/logs in [execution packet](LIVE-CURRENT-ROOT-ENTRY.md). Review did not execute commands or independently recompute hashes; hardware remains NOT RUN.
