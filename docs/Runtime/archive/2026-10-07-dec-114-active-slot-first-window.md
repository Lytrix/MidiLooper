# DEC-114 — active-slot first-window scope (historical)

**Source:** `docs/Runtime/CURRENT_WORK.md`, former declaration under `## Now implementing` (owner-directed, 2026-10-07).
**Status:** Historical scope record retained for search. This is not current implementation authorization; check the active `CURRENT_WORK.md` scope and OpenSpec tasks.

**DEC-114 scope extension — task 3.7c, the active slot's first window (declared 2026-10-07 at the owner's direction).**
The authorized device run of 2026-10-07 (`captures/s64-demand-01`) measured the change's remaining blocker: every demoted,
span-aligned, **playing** slot left the lane as `win_not_servable` with `sv=0`, including where its root **was** current
(`rt=1`) and its coverage role free (`rl=1`). The demand's entry required a window that was already *served*, and only a
switch's launch could produce one (`preparePlaybackLaunchForSlot` refuses the active slot by construction), so a slot
that reached playback **without** a switch — a freshly recorded body, or a restored one — could never be served from a
window at all: against this change's own first requirement (*the demoted case is served by interval publication*;
playback SHALL NOT require whole-loop publication). **In scope then:** task **3.7c** — drop the served window as a
precondition of *requesting* one in `Track::serviceRollingPlaybackWindow`, keeping the named refusals (no view set to
publish into, no current source), through the existing positioned demand, off-clock owner and admission. Its G2 packet
and production-path fixture:
[`2026-10-07-s37c-active-slot-first-window/EXECUTION.md`](../../../openspec/changes/windowed-playback-generation/evidence/2026-10-07-s37c-active-slot-first-window/EXECUTION.md);
its first device fixture is a repeat of the same windowed leg. **Not in scope, unchanged then:** 3.14, 4.5, the 3.10/6.3/6.5
service-envelope qualification, the launch lane itself, the display consumer (RWO S4e), and **3.7b** fallback removal,
which stays the final ladder stage.
