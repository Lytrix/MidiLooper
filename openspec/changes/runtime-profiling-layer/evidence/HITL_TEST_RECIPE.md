# HITL test recipe — runtime-profiling-layer device legs

Filled from [`docs/Templates/HITL_TEST_RECIPE.md`](../../../../docs/Templates/HITL_TEST_RECIPE.md) after
reading [`hitl_authoring_traps.md`](../../../../docs/Agents/hitl_authoring_traps.md). Task 0.7.

## Stimulus decision

`stimulus list` (2026-10-31) has no registered stimulus for the owner chain shape (20-bar Record from a
16ths pattern, 12 overdubs sliced from a running chord pattern, idle preparation between). The owner
rig `scripts/hitl_midifile_chain.py` (DEC-221) is the **measurement method of record** in
[`2026-10-31-midi-timing-handoff.md`](../../../../docs/Runtime/2026-10-31-midi-timing-handoff.md) § Measurement
method, and was the stimulus for DEC-230/231/238. It composes the registered MIDI-file clock pump and
button helpers, not hand-rolled bytes.

**Decision:** reuse the owner rig unchanged (no new stimulus, no edit to a canonical one). Rationale: the
profiler adds no gesture; its device legs must run the *same* stimulus as the timing work they explain,
so profile and legacy rows can be compared run-to-run. Profile dumps are sent **after** the rig exits,
over the capture session, not from inside a flow. **Owner accepted the reuse and the scratch-slot clear on 2026-10-31** (task 0.7/0.8); if the owner prefers a
registered variant, scaffold
`STIM-PROFILE-CHAIN-TEMP-01` from `STIM-BASE-2-2-01` with `--marker` for every `DIAG,profile_` prefix.

## Recipe

| Field | Fill in |
|---|---|
| Acceptance criterion | Slice 2: the boot row appears once with `source=1`, `cpu_hz=600000000` and both calibrations, and stats/ring dumps drain in batches under a running transport. Slice 3: `ViewPrewarm_unit` ring durations are ≤ and within 10 µs of the same run's `view_prewarm_step us=`; the prepared Record edge leaves no ring record. Slice 4: one identity per domain non-zero; overhead A/B within the P8 spread. |
| Reused stimulus / flow | Owner rig: `.venv/bin/python scripts/hitl_midifile_chain.py --hold-clock-on-settle --stop-bars 8 --record-bars 20 --record-pattern ".../droid_midilooper_test Project/16ths.mid" --overdub-pattern ".../droid_midilooper_test Project/chords.mid" --out-dir captures/midifile-chain-NN`, with `scripts/capture_session.py` running first. |
| New stimulus? | No (see decision). Fallback name: `STIM-PROFILE-CHAIN-TEMP-01`. |
| Gestures used | Those of the rig only (record/clear via its helpers, MIDI clock pump); no new gesture. Profile commands are capture-session serial commands, not MIDI. |
| Expected markers | `DIAG,profile_boot,` (A, once per boot, `RuntimeProfiler::initAtBoot`); `DIAG,profile_stat,` (A, ≤ 16 + 1 per request); `DIAG,profile_ring,` (A, ≤ 32 + 1 per request); `DIAG,profile_reset,` (A, per request); `DIAG,profile_cmd,` (A, per refused request). All from `src/Utils/ProfileDiagRows.cpp`. Comparison rows: `DIAG,view_prewarm_step,`, `DIAG,capture_prewarm_split,`, `DIAG,timing`, `DIAG,idle_turn,`, `DIAG,persist_save,step`, `midi_gap`, `late_on`, `win_gap_overrun`. |
| Marker tier test | `test_capture_ring` + `test_capture_line_tier` cases added in task 3.3 (header list unchanged). |
| `check_marker_contract.py` output | Recorded at task 3.3 for all five prefixes (NOT YET before the rows exist is expected). |
| RAM1 impact | Profile env only; default env must equal the P3 figures. Padding before → after recorded at 2.7 and 3.2. |
| Native test that fails without the change | `test_runtime_profiler` (slice 1/2); four owner-path count assertions (slice 3). |
| Boot-census / smoke check | After flash: serial shows `boot_reset,crash=0`, `BOOT,load,ok`, and exactly one `DIAG,profile_boot,` row. |
| Full scenario entry point | The owner rig above; bounded by the rig's own timing (~2–3 min); `HITL_MANAGED_RUN_TIMEOUT_S` 360 s. |
| Verdict rule | Validity first (handoff § 4): `overdubs=12`, 0 `order_deny`, 0 `preparation_denied`, else not evidence. A missing profile row is evidence only if `hitl_absence_check.py` reports `ABSENT-EVIDENCE`. |
| Evidence to record | Capture log path + tail, `parse_overdub_chain_timing.py --windows` output, `scripts/profile_report.py` output, HEX hash. |

## Slice-2 device tool

`scripts/hitl_profile_dump.py` — no gesture, no slot, no SD: a managed capture that owns a fresh boot,
the shared `MidiClockMaster` pump for a running transport, and the fenced, allowlisted
`!PROFILE_STATS` / `!PROFILE_RING` / `!PROFILE_RESET` requests. Slices 3–4 run the owner rig first,
then this tool's drains with `--no-boot` semantics added if needed.

## SD safety

The rig **clears** the scratch slot before recording (track 8, slot 4 1-based / slot 3 0-based). That is
a destructive SD operation and needs explicit owner permission in the same session
(`Destructive-SD-Operations.mdc`). No other SD operation is performed.
