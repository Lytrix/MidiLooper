"""The observation wait: waiting for the device's own rows, with no press and no parser (task 3.3 of
`response-driven-hitl-acceptance`).

Two kinds of wait live in the action layer, and the difference is what this module exists to keep clear:

* `hitl.actions.correlation.CorrelatedGesture` waits for the **answer to a gesture the host sent** -- it owns
  the stages, the identity rule and the staleness floor, and its evidence is a press.
* `await_markers` waits for a **row the device emits on its own** -- a commitment, a persistence handoff, a
  reentry release, a transport edge. Nothing is pressed, and the marker does not carry an interval identity,
  so the wait can only be bounded, cancellable and honest about what it saw.

Before this facade, each driver carried its own copy of that second wait (`_await_markers`,
`_wait_for_any`), each with its own deadline loop, its own poll cadence and its own ring-drain cadence. This
module is the one owner: it takes a *rows callable* (so the caller keeps its own row source), matches markers,
polls, and flushes the capture ring on the caller's cadence -- and it is where cancellation is checked while a
marker is outstanding, so a cancelled run stops here with its own reason instead of sitting out the bound.

It lives under `hitl.actions`, which the import gate already allows a flow to import, so no gate rule had to be
widened to let a scenario wait for an observation: the narrow facade *is* this module.
"""

from __future__ import annotations

import time
from typing import Callable, Optional, Sequence

#: How often the caller's ring-flush callback is invoked while a marker is outstanding.
DEFAULT_DRAIN_EVERY_S = 2.0

#: Default poll cadence: short enough that a marker is noticed promptly, long enough not to spin.
DEFAULT_POLL_S = 0.05


class ObservationTimeout(Exception):
    """A marker that never arrived. The caller reports it by name rather than as a bare `False`."""

    def __init__(self, markers: Sequence[str], waited_s: float) -> None:
        self.markers = tuple(markers)
        self.waited_s = waited_s
        super().__init__(f"none of {list(self.markers)} appeared within {waited_s:.1f}s")


def await_markers(
    rows: Callable[[], Sequence[str]],
    markers: Sequence[str],
    *,
    timeout_s: float,
    poll_s: float = DEFAULT_POLL_S,
    cancel: Optional[Callable[[], bool]] = None,
    on_poll: Optional[Callable[[], None]] = None,
    drain_every_s: float = DEFAULT_DRAIN_EVERY_S,
    now: Callable[[], float] = time.monotonic,
    sleep: Callable[[float], None] = time.sleep,
) -> bool:
    """Wait until any of `markers` appears in `rows()`. True when one did, False on the bound.

    `rows()` is re-read every pass, so the caller owns the row source (a log tail, a filtered window, a
    captured snapshot). `on_poll` is called at most every `drain_every_s` -- that is where a driver flushes the
    firmware's capture ring, because the ring holds rows until its owner is asked and nothing else drains it
    during a phase. `cancel` is checked every pass: a cancelled run stops waiting at once, with its own reason
    (`hitl.actions.cancellation`), rather than sitting out a commitment bound while the capture is released.

    Nothing here presses, retries or parses: an absent marker stays absent, and the caller decides what that
    means.
    """
    deadline = now() + max(timeout_s, 0.0)
    next_drain = now() + max(drain_every_s, 0.0)
    while True:
        if cancel is not None and cancel():
            return False
        if any(marker in line for line in rows() for marker in markers):
            return True
        current = now()
        if current >= deadline:
            return False
        if on_poll is not None and current >= next_drain:
            on_poll()
            next_drain = current + max(drain_every_s, 0.0)
        sleep(poll_s)
