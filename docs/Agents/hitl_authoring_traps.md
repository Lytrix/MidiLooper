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
- Classification lives in `include/Utils/CaptureLineTier.h` and is pinned by
  `test/test_capture_line_tier`. The prefix check reads only the **first 47 bytes**
  of a record, so a tag that starts later can never be Tier-A.
- A marker must be the timestamped `#CAP,<micros>,DIAG,<tag>,...` form. Untimestamped
  lines are not Tier-A-classified and the tier parser mis-reads their tag
  (`scripts/hitl_boot_census.py`).

Checklist before flashing:

0. Run the pre-flight (host-only, seconds):
   `python3 scripts/check_marker_contract.py 'DIAG,my_event,'`. It reports `EMITTED` (a literal exists in
   `src/`/`include/`), `TIER-A` (classified by the real `CaptureLineTier.h` prefix list) and `FIT`
   (classifying prefix <= 31 bytes, so it still matches the 47-byte head read at a 10-digit micros).
   `--audit` checks every registered prefix. Exit 1 means do not flash.
1. Add the tag to `CaptureLineTier.h` and extend `test_capture_line_tier`
   (`pio test -e native -f test_capture_line_tier`).
2. Keep the line short (tag inside 47 bytes) and low-rate. For per-event data emit a
   counter or one aggregated row, not one Tier-A line per event.
3. After the run, distinguish the two causes of "missing marker":
   *firmware never emitted it* (grep the source and the tail of the log) versus
   *ring displaced it* (look for the Tier-A eviction count). Do not re-flash until you know which.
4. Read the **tail** of the capture log or use a `scripts/parse_*.py` helper. Never read a whole log.

## 2. RAM1 overflow / silent boot hang

- `scripts/check_ram1_budget.py` already runs as a post-link hook in
  `platformio.ini` (`extra_scripts = post:...`). A build that fails it is **not** a
  linker quirk: read the printed padding and bank count first.
- RAM1 is 16 x 32 KB banks. Crossing into a 13th ITCM bank costs 32,768 B and leaves
  ~2 KB for locals, which is the documented silent boot hang. Padding is the
  early-warning line. Recipe: `.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc`.
- Budget is keyed per env in `scripts/ram1_budget.json` and is re-based only by a
  human with a recorded reason. Never edit it to make a build pass.
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

## Start a new experiment with the scaffold

`.venv/bin/python scripts/host_midi_hitl.py stimulus scaffold <PARENT_ID> <SUFFIX> --reason "<why canonical is insufficient>" [--bars N] [--must-prove "..."] [--marker 'DIAG,my_event,'] [--require-markers]`

It writes an unregistered TEMP variant plus a pre-filled recipe under `captures/stimuli/`, lists the real gesture
helpers and control constants (read from `scripts/hitl/`), and runs the marker pre-flight. `--require-markers`
exits 1 when a declared marker fails, which is the "do not flash" signal. `NOT YET` is normal before the
firmware row is written. It also writes `<ID>.flow.py`, a draft flow that composes only
`hitl.actions.*` helpers, embeds the authorized track/slots and your markers, and leaves each unknown as a TODO
(it is a draft under `captures/stimuli/`; moving it into `scripts/hitl/flows/` is a deliberate later step).
Implementation: `scripts/hitl/stimuli/scaffold.py`.

## Ladder: stop at the first red step

1. Native test (`pio test -e native -f <suite>`), including the tier test for any new marker.
2. Build `teensy41-capture-serial`; read the RAM1 line.
3. Ask before flashing (unless a dedicated HITL preset is evaluating the change); run a short
   boot census and confirm the marker appears.
4. Full scenario via the authoritative entry point.

Test template: [`docs/Templates/HITL_TEST_RECIPE.md`](../Templates/HITL_TEST_RECIPE.md).
