# Execution packet — phase 3 (capability: one lane, every length)

From `docs/Templates/EXECUTION_PACKET.md`. Tasks 3.1–3.3. Test-only, plus a native-only gate switch; no target
behavior change.

## Identity and scope

- **Fixture.** `CapabilityFixture::run(bars, case, bypass)` in `test/test_runtime_working_set_ownership`. It drives the
  production producer and the production tick over a committed, root-published, bar-spaced slot that enters PLAYING
  with no stopped-time publication.
- **Lengths:** 1, 8 and 20 bars.
- **Cases:** steady, a transport restart (epoch re-stamp at bar 15), and a revision change (a new current root at
  bar 15).
- **Gate switch.** `Track::setWindowLengthGatesBypassedForTest` (native only), read through `windowLaneAdmitsLength` at
  both gates:
  - DEC-200's keystone in `requestCommittedPlaybackWindowForSlot`;
  - the producer's `win_window_ineligible`.
  The target image keeps both gates, with the same expression.
- **Non-goals.** Any repair (phases 4–5).
- **Must preserve.** RAM1 96,192; the default ITCM line; every existing native case.

## Oracle (fixed before the run)

- **Acquisition:** the first tick on the View route with the frame Serving.
- **After acquisition:**
  - the consumed routes;
  - the number of window adoptions;
  - note closure (one NoteOn and one NoteOff per bar crossed);
  - the merged builds;
  - after an event, whether and when the lane serves again.
- **Classification:** a length or case that the lane serves with the gates bypassed is **routing/admission**. A named
  failure to build, publish or close is a **capability gap**.

## What a RED means here

The measurement is pinned as a baseline (`test_capability_classification_baseline`). Assertions marked BASELINE are
the defects phases 4 and 5 invert; when a repair lands, they flip deliberately. The capability assertions (acquisition
with the gates bypassed, exact closure, no `none` route) must keep holding.

## Completion record (2026-11-01)

- **Result.** `evidence/2026-11-01-capability.md`: no capability gap; defects A–D, all routing/admission.
- **Native.** `test_runtime_working_set_ownership` 263/263. Full native: see `CURRENT_WORK.md`.
- **Build.** Default `teensy41-capture-serial`: RAM1 96,192, code 391,944, padding 1,272. Unchanged.
