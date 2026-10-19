# Source-selection and Move confirmation repair

## Contract / authorized scope

Owner requested implement after run15 analysis. Existing host legacy-edit controls and note_edit_reselect_stacked scenario/verifier owners only. Reuse shared mapping extension; no firmware, runtime owner, marker, allocation, numerical timeout, canonical stimulus, device/SD action or Git delivery.

Production: qualified seed -> target PLAYING -> NOTE_EDIT -> F1 source -> F2 Move -> deselect -> two stacked reselects -> strict verifier -> canonical teardown. F1/F2 use logical -8192..8192 mapping while sent MIDI remains -8192..8191. F1 derives step/ordinal from target geometry, never full-loop slot index/count. Failed source wait returns false; source failure prohibits F2. Fresh Move must match source ID/pitch/from and expected offset-preserving target before deselect/reselect. Firmware GeometryFaderInput.cpp preserves within-step offset; source392 at step8 moved to step4 ends at200, not192. Check that exact target, not a wider tolerance.

Preserve previous strict applied-empty/identity/current-leg/unmeasured rules, original bounds and registered seed/gesture recipe. No new owner/schema/persistence/undo/hot-path change; no formal reassessment. Historical topic search found no conflicting decision; existing geometry and mapping code are reused, not silently redefined.

## Evidence and oracle before edits

Saved run15 seed qualifies under corrected seed rules; REVT source392/pitch67, destination200/pitch64, loop1536. Device reads F1 -3964 as empty336; F2 -6078 logs No note selected for coarse editing; zero EditSessionAction type3 and no movement trace. All three second reselect inputs -5813 produce apply0/unchanged_note on noteId2, not echo or successful deferred evidence. Canonical final shutdown complete/drains2/ok; no device was accessed for this investigation.

Frozen regression cases:
- F1 source step8 is not step7; F2 step4 is notstep3. Shared logical max8192, legal wire cap8191.
- Targeting uses desired step/stack despite extra note/empty slots in full-loop layout; off-grid source392 remains same selected note.
- False select wait propagates through both controls; no collector means attempted open-loop gesture only, not proof of qualification.
- Unreachable legal-wire endpoint is refused before a MIDI gesture, not clipped to another step or emitted as8192.
- Fresh expected Move with same source ID/pitch/from and offset target qualifies; no-selected-note/missing/wrong Move, old receipt or wrong identity prevents downstream deselect/reselect.
- Verifier accepts preserved offset geometry only when all existing strict identities/deselect/reselect proof remains; arbitrary within-step destination does not qualify.

## Verification recipe

New focused host suite plus existing edit_select_wait, reselect_guard, reselect_validation/stimulus/seed/fixture-stop regressions. Demonstrate RED before behavior edits, focused GREEN, broad host comparison to known6F1E and native compatibility. Single bounded review then fix/retest concrete findings. Strict OpenSpec/diff checks and runtime/guide/task closeout.
No device run/build/flash; real composed PASS remains separate with explicit target authorization. No stimulus/marker added (existing STIM-NE-RESELECT-STACKED-01 / legacy action helpers only), RAM1 untouched. No replay of captured invocation/command JSON.

## Completion

Pending implementation/verification. Parent seeded-edit acceptance remains open.
