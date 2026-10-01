# S4c.1 — interval/window readiness and immutable currency acceptance (specification for sibling F)

**Status:** specification only. This change **does not** change the publication unit, the publication path or any
root/window behaviour (`IMPLEMENTATION_CONTRACT.md` §"… retains S4c only as contract/acceptance definition"; the
sibling `windowed-playback-generation` is planned and uncreated). S4c.0
([`S4C0-PUBLICATION-INVENTORY.md`](S4C0-PUBLICATION-INVENTORY.md)) records the machinery this specification is
about; this file states what the sibling must **accept**.

**Source requirement (cited, not invented).** `design.md` Revision 11 — *"immutable readiness at re-entry, not
whole-loop publication"* — and design row 11: **immutable readiness is owed per affected tick, before the playhead
returns to it**; and *"the unit of authority becomes an interval/window with a deadline, not the whole loop"*.
The same revision measures the jammed-switch deadline at **~16-31 ms** (`INTERNAL_PPQN = 192`, 1/32-1/16-beat
quantisation) and requires the **existing resumable deferred machinery** as the mechanism rather than new work.
Nothing here restates those decisions; it turns them into criteria a sibling can be held to.

## Terms, as this specification uses them

| term | meaning here |
|---|---|
| **interval** | the tick range a consumer is about to use, `[start, start + span)` in loop-local ticks, with `start` derived from the playhead and the quantisation grid |
| **window** | the immutable representation of the content covering an interval, bound to `(slot, revision, interval)` |
| **currency** | the revision the window was built from, compared against the loop's current `playbackRevision` |
| **ready** | the window for the interval the consumer needs **exists, is immutable, and is not stale beyond the allowed lag** |
| **allowed lag** | how far behind `playbackRevision` a window may still be authoritative — the one-pass-late rule fixes this at **one pass**, not more |
| **affected tick** | a tick whose content a recording, overdub or edit changed |

## Acceptance criteria for the sibling

**AC-1 — Readiness is per affected tick, against a deadline, not per loop.** After content changes at a tick, an
updated immutable window covering that tick must be ready **before the playhead returns to it** (design row 11).
A whole-loop publication is *sufficient* but not *required*, and must never be asserted as the unit.
*Evidence:* a fixture where an edit lands and the readiness for the affected interval is shown before the
playhead's return, with the loop-length-independent case included (S4c.5).

**AC-2 — A window is immutable and identified.** A published window is read-only to all consumers and carries its
identity (`slot`, `revision`, `interval`). No consumer may mutate it, and no consumer may observe a window change
underneath a read in progress. *Evidence:* the identity and immutability properties asserted directly, plus a
consumer-side read that cannot straddle a replacement.

**AC-3 — Readiness is a predicate with a named holder.** "Ready" must be a predicate a consumer can ask (**does the
window for this interval exist and is it current within the lag?**) and whose *failure* names its holder, exactly as
the existing pends/waits do (`hasPendingSnapshotPublish`, the restore-request vocabulary, the S4b.7 hold terminal).
A consumer that finds a window missing must be able to say **who owes it**. *Evidence:* the predicate's own
fixtures, including the "missing ⇒ named owner" case.

**AC-4 — Currency is the revision, and "current" has one definition.** Today the currency predicate is
`Loop::publishedSnapshotIsCurrent()` against `playbackRevision`, and playback readiness is the merged window
(`isPlaybackMergedMidiEventsReadyForSlot` requires a non-empty windowed gather stamped with the loop's current
`playbackRevision`). The sibling must keep **one** definition of current — not add a second validity counter — and
state how a window's revision compares to `playbackRevision`, including the wrap/replacement case the existing
coverage outcomes already name (`wrapTargetRevision`, `coveredPassRevision`, current). *Evidence:* the predicate's
fixtures over the coverage outcomes, plus a "second validity counter was not introduced" review note.

**AC-5 — One pass late is the only stale authority, and it is per pass.** A miss keeps the **previous** version
authoritative for that pass — never torn, never held, never timing-deferred (design row 12). Two consequences the
sibling must accept: (a) a consumer must not be *blocked* by a missed deadline, and (b) the lag must be bounded at
**one** pass: serving an older-than-one-pass window must be a **named** condition, not a silent fallback.
*Evidence:* a missed-deadline fixture asserting (a) and (b) together.

**AC-6 — Nothing allocates or blocks on a timing-critical edge.** Window acquisition, replacement and any
rebuild happen **off-clock**; the MIDI boundary, the transport tick and the clock path only *read* readiness. The
precedent this change already establishes is S4b.6/S4b.7: the commit binds to the target's occupancy and stays
pending rather than taking anything inline, with the off-clock `SlotProvision` turn as the vehicle. The sibling
must name its own off-clock owner the same way. *Evidence:* the call-site audit plus a fixture that fails if the
edge allocates (the S4b.6 shape).

**AC-7 — Reuse the resumable machinery; do not add a scheduler.** The mechanism is the existing one (S4c.0 §2):
the body format's bounded write/parse, the resume-arming pattern, and the single persistence drain budget. A second
publication scheduler is a recorded failure mode (design row 28). *Evidence:* the sibling's own record naming the
reused entry points, with no new scheduling owner.

**AC-8 — Readiness evidence is delivered where it is produced.** A row an acceptance surface fences on must not
depend on bulk-retention policy for its delivery, and a diagnostic must not evict the evidence it explains — the
two rules this change's device work produced
([`evidence/2026-10-01-telemetry-must-not-evict-evidence/`](evidence/2026-10-01-telemetry-must-not-evict-evidence/FIX.md),
[`evidence/2026-10-01-ring-resync-and-receipt-delivery/`](evidence/2026-10-01-ring-resync-and-receipt-delivery/FIX.md)).
A windowed publication's readiness receipts are exactly the rows this trap catches. *Evidence:* the sibling's
receipt rows in a device leg with the transport running throughout.

**AC-9 — Length independence is proved, not assumed.** Readiness must not require whole-root residency; the
window size must be **swept**, not hard-coded to bars (today's merged playback window is 2 bars = 1,536 ticks and
the detailed display window is capped at 16 bars). The acceptance shape is S4c.5's. *Evidence:* the sweep and the
root-residency gate named there, owned by the sibling once its contract is approved.

## Interfaces S2.1 needs from this specification

S2.1 (*"derive the playback window lease"*) may not edit behaviour before the phase-F contract exists; this section
is that contract's interface half, so S2.1 can proceed with a defined target:

1. **Window identity** — `(slot, revision, interval)`, as in AC-2, so the lease can be keyed by what it protects.
2. **The `P` class accounting** — the bytes a *window* lease holds, per holder, which is what `N × P` in the contract
   §2 envelope charges; today the honest figure is the whole-loop root's, labelled provisional until F.
3. **The readiness predicate** (AC-3) and the **lag bound** (AC-5) — so the lease's admission can state what makes
   a window *usable*.
4. **The off-clock owner** (AC-6) — so admission has a place to happen without touching the boundary.

## Not decided here (named, so they are not silently assumed)

* **The window size `N`.** A tunable with three coupled effects (`design.md` Revision 11), swept by S2.3/S2.6;
  `S2.3`'s `N` stays **provisional, bounded by phase F**.
* **The publication unit itself.** Changing it is the sibling's own change with its own DEC/reassessment
  (`IMPLEMENTATION_CONTRACT.md`: window currency, playback root consumers and LSV1 checkpoint authority require
  that sibling's decision).
* **Checkpoint parity (S4c.4)** and the immutable-window fixture list (S4c.5) have their own records; the
  stale-root/serialized-body divergence named in S4c.0 §4 belongs with them.
* **The visual tier's envelope** (S4e) is a different owner with its own caps.
