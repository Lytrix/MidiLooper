# HITL authoring traps (read before writing a HITL test or serial marker)

Three failures account for most wasted hardware iterations. Each has a cheap
check that runs **before** a flash or a full scenario. Cite, do not duplicate:
this file is a checklist; the owners are linked.

## 1. A Tier-A log line that never shows up

Facts:

- The capture ring **sheds non-Tier-A head records silently, by design**; Tier-A
  is displaced only by newer Tier-A (`include/Utils/DebugSessionCapture.h`,
  comment on the Tier-A row family; verdict side: `_tier_a_evictions` in
  `scripts/hitl/verify/midi_boundary_timing.py`, RC-MEM-7).
- Classification lives in **two** places: `isTierALine` in `include/Utils/CaptureLineTier.h`
  (pinned by `test/test_capture_line_tier`) and `isTierATextLine` in
  `src/Utils/DebugSessionCapture.cpp` (pinned by `test/test_capture_ring`, which is the suite that
  asks that predicate). **Put new tags in the `.cpp` list**: every literal in the header is
  instantiated in every including TU, and one more was measured to snap DTCM by +4,096 B.
  The prefix check reads only the **first 47 bytes** of a record, so a tag that starts later can
  never be Tier-A.
- Tier-A is necessary, not sufficient, for a row raised **while playing**: under transport the
  ring is retained and can lose everything (a diagnostic delivered zero rows in two episodes, with
  `tier_a_evicted` and large `shed` counts). For a decisive measurement use the two delivery rules
  from the start (see `readinessReceiptImmediate`): **write directly when USB has room, fall back to
  the ring**. Never let the only copy of a decisive row live in the ring.
- A marker must be the timestamped `#CAP,<micros>,DIAG,<tag>,...` form. Untimestamped
  lines are not Tier-A-classified and the tier parser mis-reads their tag
  (`scripts/hitl_boot_census.py`).

Checklist before flashing:

0. Run the pre-flight (host-only, seconds):
   `python3 scripts/check_marker_contract.py 'DIAG,my_event,'`. It reports `EMITTED` (a literal exists in
   `src/`/`include/`), `TIER-A` (classified by both real prefix lists) and `FIT`
   (classifying prefix <= 31 bytes, so it still matches the 47-byte head read at a 10-digit micros).
   `--audit` checks every registered prefix. Exit 1 means do not flash.
1. Add the tag to `isTierATextLine` in `DebugSessionCapture.cpp` and pin both boundaries in
   `test_capture_ring` (`pio test -e native -f test_capture_ring`).
2. Keep the line short (tag inside 47 bytes) and low-rate. For per-event data emit a
   counter or one aggregated row, not one Tier-A line per event.
3. After the run, distinguish the two causes of "missing marker":
   *firmware never emitted it* (grep the source and the tail of the log) versus
   *ring displaced it* (look for the Tier-A eviction count). Do not re-flash until you know which.
4. Read the **tail** of the capture log or use a `scripts/parse_*.py` helper. Never read a whole log.
5. **No rows in the capture is not no rows on the device.** Before reading absence as evidence run
   `python3 scripts/hitl_absence_check.py <session.log> 'DIAG,my_event,'`: `ABSENT-EVIDENCE` only when
   the capture's own `RING,stats` reports no loss (routine non-Tier-A `shed` does not count against a
   Tier-A tag); `ABSENT-NOT-EVIDENCE` when the ring lost rows or no stats row exists.
6. One row per call: a "decision" row plus an "exit" row in the same call makes a state-change dedupe
   alternate forever (2 rows per turn). Name the earliest failed decision inside the single row's flags,
   and document at which instant each field is sampled (a flag read at the *exit* is read after the
   request, so `req` and `pend` together are the reading).

## 2. RAM1 overflow / silent boot hang

- `scripts/check_ram1_budget.py` already runs as a post-link hook in
  `platformio.ini` (`extra_scripts = post:...`). A build that fails it is **not** a
  linker quirk: read the printed padding and bank count first.
- RAM1 is 16 x 32 KB banks. Crossing into a 13th ITCM bank costs 32,768 B and leaves
  ~2 KB for locals, which is the documented silent boot hang. Padding is the
  early-warning line. Recipe: `.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc`.
- Budget is keyed per env in `scripts/ram1_budget.json` and is re-based only by a
  human with a recorded reason. Never edit it to make a build pass.
- **A few string literals can cost exactly +4,096 B of DTCM.** `.rodata` is DTCM in the linker
  script; new tags or holder names can cross an align edge with `code` byte-identical (the guard then
  fails with `variables` +4,096). Put new literals in a TU that already has a `.rodata.str*` carve-out
  (`TrackPlaybackWindowBuild.cpp` does) or add the carve-out *before* the build; `TRACK_COLD_MEM` on the
  literals is rejected by the compiler. `check_ram1_budget.py` now names this case when the change is a
  multiple of 4,096 B. Read the current `padding` headroom first: a few hundred bytes means the next snap is certain.
- Test-only instrumentation spends the same padding. Before adding it, decide where
  it lives: gated behind session capture / a debug gate, cold paths in FLASHMEM,
  no new inline code in ITCM hot paths.

## 3. Wrong key-stroke / control logic

- Do **not** hand-roll MIDI bytes. Notes, channel and timing constants are in
  `scripts/hitl/control_constants.py` (record 36, play/stop 40, edit 38, length edit 35,
  control channel 16, edit debounce 350 ms, ...). Gestures are in
  `scripts/hitl/actions/` (`buttons.py`, `slots.py`, `transport.py`, `observation.py`)
  and composed in `scripts/hitl/flows/`.
- A press is the SEND stage owned by the action helper; pass `correlate` and
  `before_device_us`, then wait on the observed response instead of sleeping.
- List existing stimuli before adding: `.venv/bin/python scripts/host_midi_hitl.py stimulus list`.
  Never edit a canonical stimulus; make a temporary variant.
- Never execute a captured `command.json`. Use the authoritative entry point.

## 4. Fixtures and device runs

- **A green native fixture can hide the defect.** If a regression steps production by hand (for example
  `stepPendingSnapshotPublish(...)`), check who performs that step on hardware for *your* shape. Drive the
  **production owner** that decides; for every seam you substitute, write down who calls the real thing and
  whether that caller exists for your shape.
- **A seam substitutes the transport, not the policy.** Keep cadence/dedupe state in production code and
  let the seam replace only emission; otherwise the fixture records every call and cannot test the cadence.
- **Guard order beats expectation.** A leg can fail on an earlier guard than the one you meant (no runtime
  yet beats not-demoted). Arrange the state that makes the intended guard reachable first, or assert the
  discriminating field rather than the token you assumed.
- **Flash and run are two steps.** After a flash the board re-enumerates over USB. The runners now wait up
  to 30 s for the Teensy MIDI input and output (`wait_for_midi_ports`) and warn if they do not return; if
  the run still aborts at port open, re-run without `--build-upload` / with `--no-upload`. After a flash the
  accepted-sequence wrapper also wants `--boot-reset`, and a slow seed persist (~9 s) needs
  `--seed-settle-timeout-s 30`.
- **The exact invocation is recorded** in `invocation.json` beside `metadata.json` for managed runs
  (evidence only; never execute it). CLI `--slot-a/--slot-b/--loop-slot/--switch-target-slot` are
  **1-based operator indices**; firmware rows, census and `RECA/RECS/reentry` slot fields are **0-based**.
- **Exit codes are not the verdict.** An oracle can fail (exit 2) while the capture carries the answer, or
  pass while delivering no rows. State the row-level oracle before running, then read rows.
- **Check the tree at both ends.** Run `git status` before and after long builds; unrelated work can appear
  mid-task. Never blanket `git stash`; report which hunks are yours and stage by hunk.

## Start a new experiment with the scaffold

`.venv/bin/python scripts/host_midi_hitl.py stimulus scaffold <PARENT_ID> <SUFFIX> --reason "<why canonical is insufficient>" [--bars N] [--must-prove "..."] [--marker 'DIAG,my_event,'] [--require-markers]`

It writes an unregistered TEMP variant plus a pre-filled recipe under `captures/stimuli/`, lists the real gesture
helpers and control constants (read from `scripts/hitl/`), and runs the marker pre-flight. `--require-markers`
exits 1 when a declared marker fails, which is the "do not flash" signal. `NOT YET` is normal before the
firmware row is written. It also writes `<ID>.flow.py`, a draft flow that composes only
`hitl.actions.*` helpers, embeds the authorized track/slots and your markers, and leaves each unknown as a TODO
(it is a draft under `captures/stimuli/`; moving it into `scripts/hitl/flows/` is a deliberate later step).
Implementation: `scripts/hitl/stimuli/scaffold.py`.

## Tools added for these traps

| Tool | Answers |
|---|---|
| `scripts/check_marker_contract.py` | Is the marker emitted, Tier-A (either list), and inside the 47-byte head read? |
| `scripts/hitl_absence_check.py` | Is a missing row evidence of absence, or did the ring lose rows? |
| `host_midi_hitl.py stimulus scaffold` | TEMP variant + recipe + gesture list + draft flow + marker pre-flight |
| `check_ram1_budget.py` (post-link) | Names the 4 KB align-edge snap |
| `invocation.json`, `wait_for_midi_ports` | Exact argv beside evidence; flash-then-run without the MIDI race |

## Ladder: stop at the first red step

1. Native test (`pio test -e native -f <suite>`), including the tier test for any new marker.
2. Build `teensy41-capture-serial`; read the RAM1 line.
3. Ask before flashing (unless a dedicated HITL preset is evaluating the change); run a short
   boot census and confirm the marker appears.
4. Full scenario via the authoritative entry point.

Test template: [`docs/Templates/HITL_TEST_RECIPE.md`](../Templates/HITL_TEST_RECIPE.md).
