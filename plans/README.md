# Cursor design plans (archived)

These files are **copies of plans from** `~/.cursor/plans/` so they are **versioned with the repo** and survive machine / Cursor UI changes.

- Filenames include a short id suffix from Cursor (e.g. `_856310b1`).
- Frontmatter (`name`, `overview`, `todos`) is preserved.
- In-repo links inside plans are as Cursor generated them.

**To refresh from your machine:**  
`cp ~/.cursor/plans/*.plan.md plans/`

---

## Jam / bar-step / multi-loop

| File | Topic |
|------|--------|
| **[dual-tick_view_override_architecture_856310b1.plan.md](dual-tick_view_override_architecture_856310b1.plan.md)** | **Main architecture:** jam state, Phase 2 jam tick, **Phase 3 multi-loop slots + jam recording + scenes** |
| [phase_2_jam_tick_3a63b6de.plan.md](phase_2_jam_tick_3a63b6de.plan.md) | Jam tick implementation |
| [jam_plan_snapshot_b7443ff8.plan.md](jam_plan_snapshot_b7443ff8.plan.md) | Jam plan snapshot |
| [bar_select_state_refactor_e11de8aa.plan.md](bar_select_state_refactor_e11de8aa.plan.md) | Bar select refactor |
| [hold_two_jam_state_fix_d5ab7bef.plan.md](hold_two_jam_state_fix_d5ab7bef.plan.md) | HOLD_TWO + jam state |
| [hold_two_overlap_fix_67b6c4e9.plan.md](hold_two_overlap_fix_67b6c4e9.plan.md) | HOLD_TWO overlap |
| [hold_two_fix_first-held_3a76f8d2.plan.md](hold_two_fix_first-held_3a76f8d2.plan.md) | HOLD_TWO first-held order |
| [hold_loop_revert_on_release_3e695eba.plan.md](hold_loop_revert_on_release_3e695eba.plan.md) | Hold loop revert |
| [double-press_exit_and_jam_seek_32bcb57a.plan.md](double-press_exit_and_jam_seek_32bcb57a.plan.md) | Double-press / jam seek |
| [multiloop_exit_and_direct_select_ux_94a8f282.plan.md](multiloop_exit_and_direct_select_ux_94a8f282.plan.md) | Multiloop UX |
| [loop_button_detection_95fbea95.plan.md](loop_button_detection_95fbea95.plan.md) | Loop button detection |
| [droid_position_button_handler_2aef4cb8.plan.md](droid_position_button_handler_2aef4cb8.plan.md) | Droid position / buttons |

## BPM / clock / transport

- [bpm_display_and_clock_persistence_96ba766a.plan.md](bpm_display_and_clock_persistence_96ba766a.plan.md)
- [bpm_display_clock_fix_ad200fab.plan.md](bpm_display_clock_fix_ad200fab.plan.md)
- [fix_bpm_off-by-one_39ee0246.plan.md](fix_bpm_off-by-one_39ee0246.plan.md)
- [smooth_bpm_display_ema_d66aa3fb.plan.md](smooth_bpm_display_ema_d66aa3fb.plan.md)
- [bpm_display_dead-band_011ae626.plan.md](bpm_display_dead-band_011ae626.plan.md)
- [stable_bpm_display_2d31ccfc.plan.md](stable_bpm_display_2d31ccfc.plan.md)
- [fix_8-tick_clock_offset_447fbd2d.plan.md](fix_8-tick_clock_offset_447fbd2d.plan.md)
- [global_transport_start_stop_e84559e4.plan.md](global_transport_start_stop_e84559e4.plan.md)
- [transport_button_fix_+_led_82cc2e28.plan.md](transport_button_fix_+_led_82cc2e28.plan.md)

## MIDI / buttons / LEDs

- [midibuttonconfig_notes_refactor_6dc60c12.plan.md](midibuttonconfig_notes_refactor_6dc60c12.plan.md)
- [midibuttonprocessor_state_machine_07fddb02.plan.md](midibuttonprocessor_state_machine_07fddb02.plan.md)
- [centralize_midi_config_bc6889a3.plan.md](centralize_midi_config_bc6889a3.plan.md)
- [midi_channel_display_and_track_default_45f497d6.plan.md](midi_channel_display_and_track_default_45f497d6.plan.md)
- [midi_thru_all_states_b491d34f.plan.md](midi_thru_all_states_b491d34f.plan.md)
- [controller_midi_priority_check_bed2cb36.plan.md](controller_midi_priority_check_bed2cb36.plan.md)
- [button_debounce_fix_0a4ef6cb.plan.md](button_debounce_fix_0a4ef6cb.plan.md)
- [exclude_led_channels_from_all_notes_off_1fca1ecd.plan.md](exclude_led_channels_from_all_notes_off_1fca1ecd.plan.md)
- [fix_channel_3_led_blocking_9239c3cb.plan.md](fix_channel_3_led_blocking_9239c3cb.plan.md)
- [8_bar_led_feedback_7d2101c7.plan.md](8_bar_led_feedback_7d2101c7.plan.md)

## Display / misc

- [display_protocol_discovery_311199ea.plan.md](display_protocol_discovery_311199ea.plan.md)
- [display_protocol_discovery_d2ad9283.plan.md](display_protocol_discovery_d2ad9283.plan.md)
- [code_optimizations_plan_6c7da97b.plan.md](code_optimizations_plan_6c7da97b.plan.md)
- [setup_order_fix_12f3d614.plan.md](setup_order_fix_12f3d614.plan.md)
- [fix_undo_state_storage_3a16bfa1.plan.md](fix_undo_state_storage_3a16bfa1.plan.md)
- [fix_wrapped_note_playback_cc3259d3.plan.md](fix_wrapped_note_playback_cc3259d3.plan.md)
- [stuck_note_and_clock_log_fix_3e15afed.plan.md](stuck_note_and_clock_log_fix_3e15afed.plan.md)
- [exit_diagnosis_mode_strategy_e5533991.plan.md](exit_diagnosis_mode_strategy_e5533991.plan.md)

---

Human-written summaries for some topics also live under [docs/FEATURE_PLANS.md](../docs/FEATURE_PLANS.md).
