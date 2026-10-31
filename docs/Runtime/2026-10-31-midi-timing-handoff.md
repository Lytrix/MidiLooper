# MIDI timing handoff — overdub chain (2026-10-31)

**Rule for this work: MIDI timing has priority over display and persistence work.** No slice ships if the owner
rig chain shows a MIDI-gap or late-note regression against the previous image. **DEC-238 — MIDI timing first**
sets that rule for the display-base arbitration; this handoff applies it to everything below.

Branch: `feature/unified-rolling-playback-path`. Head at hand-off: `9dd5cee3` (**DEC-238 — arbitration ships
off**). Board image: `2c7bb5d3…`, boot verified (`boot_reset,crash=0`, `BOOT,load,ok`). Retained HEX:
`captures/midi-first-2c7bb5d34039/`.

## Where timing stands

All rows come from the same owner rig chain (12 overdubs, 47 s overdub span), scored with
`scripts/parse_overdub_chain_timing.py`. `midi_gap` is the worst MIDI-service gap per 5 s window; "med" is
the median of those per-window worst values.

| Session | State | midi_gap med/max | late_on max | frames > 50 ms | cer_win |
|---|---|---|---|---|---|
| `20261030_194716` | **baseline** (before the display-follow work) | **58 / 200 ms** | 108 ms | 0 | 13 |
| `20261031_151347` | before **DEC-236 — no stale-root re-walk on live input** | 101 / 262 ms | 176 ms | 148 | 165 |
| `20261031_153427` | DEC-236 | 85 / 214 ms | 104 ms | 22 | 27 |
| `20261031_162028` | DEC-236 + rebuild rows | 79 / 193 ms | 135 ms | 46 | 67 |
| `20261031_212428` | **DEC-237 — staged display base**, Slice 3 arbitration **on** (`0ebccccc`) | **281 / 365 ms** | 195 ms | 0 | 0 |
| `20261031_213255` | arbitration off (`2c7bb5d3`) | 81 / 197 ms | 96 ms | 29 | 39 |
| `20261031_214619` | arbitration off, current board | 94 / 204 ms | 157 ms | 23 | 31 |

**Gap to close:** about 25–35 ms of median `midi_gap`, plus the `late_on` tail (96–157 ms vs 108 ms), to get
back to baseline. The worst gap (~200 ms) is already at baseline.

**Conflict to resolve before acting:** the `2026-10-31` A/B note at the top of `CURRENT_WORK.md` says there is
"no evidence the 3c1 arbitration itself caused the later MIDI regression". It quotes per-window values around
40 ms. The scorer over the same session (`212428`) gives a 281 ms median. Re-derive that note's numbers with
the scorer, using `--windows` to see the per-window rows. Until it is reconciled, DEC-238 stands: do not
re-enable `kDisplayBaseArbitrationEnabled` in `runDeferredIdleMaintenance` on the strength of that note.
Session `213624` (`d92dae32`) is not a timing run either: no overdub started, and the Record stop wedged.

## Measurement method (use for every slice)

1. Build `teensy41-capture-serial`, upload, and verify boot over serial. Retain the HEX hash.
2. Run the owner rig, and only this rig:
   ```
   .venv/bin/python scripts/hitl_midifile_chain.py --hold-clock-on-settle --stop-bars 8 --record-bars 20 \
     --record-pattern ".../16ths.mid" --overdub-pattern ".../chords.mid" --out-dir captures/midifile-chain-NN
   ```
   Clear and reuse the authorized scratch slot (track 8, slots 4–5) only.
3. Score the new run against the previous image's run:
   `scripts/parse_overdub_chain_timing.py <prev>.log <new>.log --windows`.
4. Validity: `overdubs=12`, 0 `order_deny`, 0 `preparation_denied`. An invalid run is not timing evidence.
   Use the stop-edge note in `CURRENT_WORK.md`.
5. Acceptance, measured against the previous image (the A/B side):
   - the `midi_gap` median does not rise;
   - the `midi_gap` max does not rise above 210 ms;
   - the `late_on` max does not rise;
   - frames > 50 ms do not rise.

   Run a slice that targets one owner twice (A/B), because single runs vary by ~10 ms of median (`213255` vs
   `214619`).

## Cost owners, in repair order

Ordered by expected MIDI gain per unit of risk. Each row names the measured cost, then what to do.

1. **Stale-root committed pass walk**
   - **Measured cost:** one walk per window, 65–95 ms. This is what sets the median: `disp_rebuild` /
     `disp_passwalk` attribute each window's worst frame to it.
   - **What to do:** serve the previous committed base and take the new base in bounded steps, instead of the
     single-shot walk inside the frame.
     - Keep the MIDI service inside that work: each step yields to the MIDI service before it exceeds ~4 ms.
     - Do it without the arbitration. The display grain runs on idle turns where coverage is neither due nor
       owed. Never take a coverage turn: the `212428` failure was display work displacing the idle cadence.
   - **Expected:** median toward 58–65 ms.
   - **Gate:** `disp_passwalk` per window falls to 0 in the frame path.
2. **Producer wrap step is unbounded**
   - **Measured cost:** `disp_base_prep` `max_step_us=117,844` on the dense shape. A step of that size alone
     breaks MIDI timing.
   - **What to do:**
     - Make the wrap staging resumable and boundary-windowed.
     - Apply the `chunksInspected` cap on every call.
     - Use an indexed upsert merge.
   - **Gate:** native max step work bounded, and device `max_step_us` ≤ 4,000 before item 1 consumes the
     producer.
   - **Order:** this is a prerequisite for item 1 if item 1 reuses the DEC-237 producer.
3. **Per-tick centred follow origin** (`resolveCenteredWindowStart`)
   - **Measured cost:** every frame of the middle-bar follow phase is a full rebuild (`cer_win`, frames > 50 ms).
   - **What to do:** a bar-stable origin is the fix, but it blanked the entering bar until the next-bar tiles
     existed. Add next-bar tile prefetch first, as a bounded idle step, then snap the origin per bar.
   - **Gate:** frames > 50 ms ≤ 8, and no blank entering bar (owner visual check).
4. **Tile preparation turns**
   - **Measured cost:** `idle_turn,cov=tile`, 30–46 ms per turn.
   - **What to do:** split per tile with a MIDI-service yield.
   - **Gate:** `idle_maint` per-window worst ≤ 20 ms during the span.
5. **Arbitration starvation** (the `212428` mechanism)
   - **Status:** inferred, not instrumented. The idle-maintenance residual was 200–554 ms with `child=0`, and
     the long idle turns persisted after the candidates had completed.
   - **When to act:** only if the arbitration is ever wanted again. Instrument the idle child turn durations
     first, then run the on/off A/B. Otherwise remove Slice 3's dormant code when convenient.

## Correctness items that block timing evidence

- **Record-stop wedge.**
  - **Evidence:** `order_deny reason=5 persist=3`. Canonical storage is already Persisted while the
    private-order preflight requires `NotScheduled`; this reproduces only on the clear→Record race.
  - **Why it blocks:** it invalidates chains.
  - **Handling:** fix it only when it reappears in a timing run. Do not chase it inside a timing slice. Repair
    shape: generation/ownership guard plus skip-and-replan of the stale save; no journal close/reopen.
- **Stale mid-pass journal handle within one boot.** Latent and unproven on device. Keep it out of timing
  slices.

## Budgets and hygiene

- **ITCM headroom is 448 B.** The first-bar route diagnostic (`reportFirstBarRouteForSlot`, ~336 B) can be
  removed once **5.2 — first-bar playback handoff** closes; that is the cheapest headroom for item 1.
- Every new diagnostic stays session-capture gated and registered in `isTierATextLine`. No lambdas in FLASHMEM
  functions (they land in ITCM).
- Run native in parallel with the build and HITL. A slice is not done until native is green and the A/B score
  is recorded in `CURRENT_WORK.md`.

## Do not

- Re-enable the arbitration, or any display work that takes a coverage turn, without an A/B that passes the
  gate above.
- Accept a single device run as proof of a few-ms median change.
- Use `213624` or other wedge runs as timing data.
- Execute a captured `command.json`, or edit the canonical stimulus.
