# P1 — readiness verdict

**Verdict: BLOCKED for firmware implementation and production packet generation.**
The five contract-level blockers are now specified in [contracts](../CONTRACTS.md) (C1 — independent
sealed custody and early Stop, C2 — total reserves and pre-admitted cap, C3 — private-source
preparation and deadline, C4 — successor-safe undo and empty cleanup, C5 — reachable bounded visual
continuation). Metadata is now measured too: the custody receipt prices at120 target bytes, the slab at
488/608 and the undo field at `+12` ([price](CONTRACT-PRICE.md)), so the cap's metadata tier is not the
binding constraint, and [the cap derivation](CAP-DERIVATION.md) fixes `kCaptureCustodyCapacity = 5` with
`R_rec = 5 chunks` while leaving body admission dynamic on the 512-chunk pool. Readiness is therefore no
longer gated: **DEC-160** ratified the undo/lifetime semantics, so the only remaining work is
implementation, with C2.4 as a first-slice pre-flight (task2.2.8) that cannot change `K`. Contract text is
not a measurement and does not admit capacity or timing.
The approved per-interval Track custody direction remains preferred; this packet does not choose a numeric cap, change undo semantics, remove guards or supersede installed queued-only behavior. Evidence is against the existing dirty source on `feature/unified-rolling-playback-path`, baseline `faa089b8365720972f8535decd747180a963723f`.

## Evidence classification

| Contract | Classification and evidence | Resolution required before firmware |
|---|---|---|
| Immediate button ingress | VERIFIED counterexample: B waits for A publication; [handoff](HANDOFF.md). CONFLICT with requested immediate behavior, not an authority conflict. | Private-source-qualified admission independent of canonical completion. |
| Independent early Stop / minimum A+B+C overlap | VERIFIED: direct B accepts input, Stop leaves B live and producer grows 2→3. C is not reached. Storage primitives can seal A/B/C without copying; that is not Track admission. | Stable generation-qualified receipts, independent closure/staging obligations and fixed pre-admitted cap; Stop must cut input at its command boundary. |
| Custody representation and release | VERIFIED: current per-slot range/shared worker cannot provide the trace; first release grain invalidates logical range readability. ASSUMPTION: candidate presentation plus two words is sufficient metadata. | Exact fields/API, reader/job/source references, stale/ABA rejection, final-consumer release, bounded retirement/rearm. |
| Target cost and reserve | VERIFIED ABI/allocator pricing in [custody ledger](CUSTODY.md), including directories, spare tails, disjoint canonical credits and retained physical high-water. UNKNOWN full overlapping setup price. | Price receipt additions, independent stages, roots/undo/visual/persistence/recovery reserve, allocator overhead and RAM1 stack high-water before fixing cap. Unknown terms are not zero. |
| Source binding / deadline | VERIFIED: A's accepted pitch60/NoteId1 at phase48 is absent from private-source participants; the same query succeeds after canonical publication. UNKNOWN bounded private recipe. | Qualified source/generation/geometry, fixed scan/pair/transform units, production wakeup and affected-tick deadline; differential B/C overlap oracle including publication mid-resolution. |
| Successor-safe finalization | VERIFIED nonempty A preserves live B rows and isolated undo state, with persistence dirty-marking only. VERIFIED empty A resets geometry768→0 while B rows survive. UNKNOWN sealed B/C causal history. | Predecessor-local empty cleanup, geometry and undo attachment; exact independent sealed chronology and persistence responsibility. Do not infer exactly-once admission from a dirty flag. |
| Bounded visual continuation | VERIFIED synchronous whole-pass-before-filter route on actual source miss with exact content oracle. UNKNOWN reachable bounded replacement. [Visual evidence](VISUAL.md) maps dormant/reset fallback and selected-clean-track-only device gate. | Persistent scan/pair/edit/compose/append/retire continuation, actual dirty-background-track producer/wakeup, enforced complete work units, cancellation and eventual coherent replacement. |
| Resource exhaustion / recovery | VERIFIED allocator growth rollback and retained pool backing. UNKNOWN production admission/exhaustion/recovery envelope. | Establish resources before acceptance; retain accepted content, independently release/rearm, and qualify existing fault/recovery policy. FIFO depth is not storage. |
| Command/input/output service | UNKNOWN numerical command-to-ingress/stop, input-service gap, output lateness and permitted service gaps. No device qualification in this packet. | Declare numeric workload/service oracle and measure first/later cycles with matched background display/persistence; separate gesture recognition. Native ordering and partial resolver counters are not hardware timing. |

## Contract mapping (2026-10-25)

[Contracts](../CONTRACTS.md) fixes the contract-level content of every BLOCKED row above. This is a
specification, not a measurement: a row leaves BLOCKED only when its named gate passes.

| Contract | Resolves from the table above | Still gated by |
|---|---|---|
| C1 — independent sealed custody and early Stop | Independent early Stop / minimum overlap; custody representation and release | receipt and cap constant delivered ([price](CONTRACT-PRICE.md), [derivation](CAP-DERIVATION.md)) |
| C2 — total reserves and pre-admitted cap | Target cost and reserve | `K = 5` and `R_rec = 5 chunks` derived ([derivation](CAP-DERIVATION.md)); C2.4 pre-flight only, cannot change `K` |
| C3 — private-source preparation and deadline | Source binding / deadline | device service envelope (separate gate, not this packet) |
| C4 — successor-safe undo and empty cleanup | Successor-safe finalization | RATIFIED by **DEC-160**; implementation only |
| C5 — reachable bounded visual continuation | Bounded visual continuation | device timing (separate gate, not this packet) |

Two rows are **not** closed by a contract and stay UNKNOWN as stated: "Resource exhaustion / recovery"
needs the C2.4 envelope (`R_rec` is declared, not measured), and "Command/input/output service" is a
device measurement with no number supplied here.

### Production packets

All four G2 packets are specified ([index](../packets/README.md)): **P-A** custody/admission/
early-Stop input cut, **P-B** private-source preparation, **P-C** successor-safe finalization/undo,
**P-D** bounded visual continuation. Each is status **specified with the named RED demonstrated**
([fixture receipt](PACKET-FIXTURES.md)) — none is implemented, green or measured, and the remaining
completion fields are NOT RUN. **DEC-160** authorised P-C by ratifying the undo/lifetime semantics, so
task2.2 — production packet generation — is complete; implementation has not started.
- Packet fixtures: `IMMEDIATE_HANDOFF_CASE=start|stop|source|empty` each exit1 with their named
  behavioural failures; `IMMEDIATE_VISUAL_CASE=bound` exits1 at the whole-pass assertion after the
  exact first-bar content check; both control suites are green (4 cases each). A harness defect was
  found while collecting this: the handoff suite rejected an unknown selector with
  `TEST_FAIL_MESSAGE` outside `RUN_TEST`, which `longjmp`s to an unestablished frame and **segfaulted
  (exit139)**. Fixed to a printed rejection with exit2; the four named profiles still exit1 and the
  default still exits0. Full native after the fix is unchanged at3,454/7skipped/3,447passed/0failed,
  147.2s. This is the only non-documentation change in this packet and it is test-only.

## Measured resource floor, not capacity

Shipping ARM: CaptureRange80 bytes, presentation120, illustrative custody box128, full stop-job ring29,704. Each armed range needs a1,024-byte requested directory and a5,208-byte real spare chunk; a sealed interval owns its actual bodies instead of an additional spare. A257/B1/C1 plus empty live producer uses5chunks in the native primitive trace. Canonical output and unconsumed credits are distinct bodies; reader/undo references to a shared body do not duplicate its charge. Target pool backing is333,312 bytes/block, up to2,666,496 across8blocks, retained at high-water.

A2048-row event stage has59,392 target dynamic bytes; another existing-style staging role totals62,004, not80/128. Minimum three sealed intervals is a required trace, **not an admitted cap3**. A distinct retiring source and the next Stop require additional custody/resource allowance. See the parametric whole-setup ledger rather than extrapolating metadata to supported capacity.

## Verification receipts

- Default full native regression:3,454 cases /7 skipped /3,447 passed /0 failed,105.775s; retained `immediate-p1-native.log`.
- Handoff controls4/4 and visual controls4/4 PASS. Five independent desired acceptance profiles remain intentionally RED: Start, early Stop, private source, empty geometry, visual bound. These are not passing implementation tests.
- Preserved `probes/run-custody-probes.py --out-dir "$PI_SCRATCH_DIR/custody-repro"` rerun: ARM compilation/symbol extraction, host compilation/execution and native production allocator assertions all exit0. ABI table agrees with original measurements. Flag snapshot normalizes project/framework paths and resolves `$BOARD_F_CPU` to600000000; no changed compiler option was found. Target allocator execution, firmware linking and device timing were not performed.
- Fresh closeout rerun: `pio test -e native` exit0,3,454 cases /7 skipped /3,447 passed,114.583s (`immediate-p1-native-closeout.log`). Strict OpenSpec validation PASS,24 local Markdown link paths present, and `git diff --check` PASS.
- Contracts closeout: `pio test -e native` exit0, 3,454 cases / 7 skipped / 3,447 passed / 0 failed,
  119.006s (documentation-only packet; no firmware build, upload, device run or SD operation). Strict
  OpenSpec validation PASS; all 31 relative link targets across CONTRACTS/design/tasks/READINESS resolve;
  `git diff --check` PASS. The five desired acceptance profiles remain RED and unchanged by this packet.
- C2.4 price measurement: [price annex](CONTRACT-PRICE.md). Control run on the unmodified probe reproduced
  the published table byte-for-byte; the extended run priced `ContractCustodyReceipt` at120 target bytes
  (equal to the `CaptureStopPresentation` it supersedes), the slab at488 (K4) /608 (K5), and
  `UndoEntry::interval` at144 (`+12`). Both runs exit0 across ARM compile, `nm`, host build/run and the
  native allocator assertions. The metadata tier is measured and is **not** the binding constraint; the
  cap value is fixed separately by the derivation below. No firmware link, device timing or capacity
  claim.
- C2.2 cap derivation: [cap derivation](CAP-DERIVATION.md). Structural custody capacity is separated from
  dynamic body admission, so `kCaptureCustodyCapacity = 5` follows from the measured receipt/slab plus
  `K_trace` and the bounded retirement overlap, and `R_rec` is declared as5 chunks (26,040 target bytes)
  against the existing owner-deferred single-capturing-Track policy. Body admission stays dynamic on the
  512-chunk pool, which keeps477 chunks for committed library content at the dense worst case. The
  arithmetic is reproduced by the snippet in the annex; C2.4 remains a first-slice pre-flight that cannot
  change `K`.
- One focused independent proof review completed; persistence wording corrected to dirty-marking. No second broad review required for this evidence closeout.

## Task disposition and next boundary

Delivered: task1.1 — button Start counterexample; task1.2 — independent early-Stop counterexample; task1.4 — reproducible parametric custody pricing; task2.1 — this explicit readiness verdict; tasks2.2.1–2.2.7 — the five contracts, the metadata price and the cap derivation. The cap is now a derived structural constant (`K = 5`) with `R_rec` declared; what remains unproved is body capacity at user density (dynamic, by design), the C4.6 ratification and the first-slice pre-flight.

Incomplete: task1.3 — full lifetime/undo/persistence chronology; task1.5 — private source
recipe/deadline and differential; task1.6 — bounded visual producer and continuation. Task2.2 —
production packet generation remains unchecked: C2's cap *value* and C4.6's ratification are still
missing. No speculative production packets are generated. Tasks2.2.1–2.2.5 deliver the five contracts.

Next work is the C2.4 target measurement and the C4.6 ratification — those are the only two gates left
on production packet generation; the contract-level content is fixed in [contracts](../CONTRACTS.md).
No new product UX or capacity exception is adopted. DEC-159 — immediate first-overdub sealed custody
remains direction with a contract and still no packet; DEC-158 — post-Record retained intent remains
truthful installed behavior. Contracts restate the approved direction and fix clause-level mechanics;
the lifetime/undo semantics they specify are **not yet ratified**, so no decision-log append is made by
this packet. Nothing shipped; delivery tracking remains unchanged. Installed image74392e15 and
device/SD content are untouched; no upload, reset, content stimulus, commit or push occurred in this
packet.
