"""Tests for the S7.5a envelope measurements (`hitl.measure.envelope_timings`).

One test per missing continuity timing, plus the availability contract each one owes:

* a metric with no instrument in a capture must say so (`missing`, with the reason) — never a zero;
* a metric whose instrument cannot distinguish "no lateness" from "never fed" is `unproven`, never
  `measured 0`;
* `measure_all` covers every metric in `metric_names()` (no metric quietly folded into another).

The real-capture tests read the two captures this repo already holds and assert only structural facts
(the metric is measurable and non-zero where the instrument is proven), so they cannot go stale on a
re-measured number.
"""

from __future__ import annotations

import unittest
from pathlib import Path

from hitl.measure import envelope_timings as m

REPO_ROOT = Path(__file__).resolve().parents[1]
REAL_CAPTURES = (
    Path("captures/note-edit-enter-24bar-t8s4-01/session_20261010_194956.log"),
    Path("captures/s39-hw-receipts-run1/session_20261009_153953.log"),
)


def _win_gap(
    *,
    max_delta: int = 0,
    opportunities: int = 1,
    eligible: int = 0,
    progress: int = 0,
    denied: int = 0,
    permitted: int = 64,
    overruns: int = 0,
    overrun_max: int = 0,
    saturated: int = 0,
) -> str:
    """One `DIAG,win_gap` row in the emitter's field order (see `scripts/hitl/verify/windowed_lane.py`)."""
    fields = [
        max_delta, opportunities, 0, 0, 0, 0, eligible, progress, denied, 0, 1, saturated,
        permitted, overruns, overrun_max, 3,
    ]
    return f"#CAP,1000,DIAG,win_gap,{','.join(str(field) for field in fields)}"


class TestMidiInputServiceGap(unittest.TestCase):
    def test_reads_the_worst_gap_from_the_telemetry_family(self) -> None:
        lines = [
            "#CAP,1000,DIAG,midi_gap,25724,1",
            "#CAP,2000,DIAG,midi_gap,372702,113",
            "#CAP,3000,DIAG,midi_input,627,0",
        ]
        result = m.measure_midi_input_service_gap(lines)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.unit, "us")
        self.assertEqual(result.value, 372702)
        self.assertEqual(result.source_rows, 2)
        self.assertEqual(result.detail["over_count_sum"], 114)
        # No DIAG,env_dist row in this capture, so it carries no distribution at all — and says what is owed
        # rather than leaving a reader to assume one.
        self.assertNotIn("midi_gap_p99_us_interval", result.detail)
        self.assertIn("env_dist", result.detail["distribution_owed"])

    def test_absent_family_is_missing_not_zero(self) -> None:
        result = m.measure_midi_input_service_gap(["#CAP,1000,DIAG,midi_input,627,0"])
        self.assertEqual(result.availability, m.MISSING)
        self.assertIsNone(result.value)
        self.assertIn("midi_gap", result.reason)


class TestMidiInputServiceDuration(unittest.TestCase):
    def test_measures_the_service_duration_apart_from_the_gap(self) -> None:
        lines = ["#CAP,1000,DIAG,midi_gap,90000,0", "#CAP,2000,DIAG,midi_input,627,2"]
        result = m.measure_midi_input_service_duration(lines)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.value, 627)
        self.assertEqual(result.detail["over_count_sum"], 2)

    def test_gap_and_duration_are_separate_metrics(self) -> None:
        lines = ["#CAP,1000,DIAG,midi_input,627,0"]
        self.assertEqual(m.measure_midi_input_service_gap(lines).availability, m.MISSING)
        self.assertEqual(m.measure_midi_input_service_duration(lines).availability, m.MEASURED)

class TestTheDistributionRows(unittest.TestCase):
    """`DIAG,env_dist` (the window's p50/p99 **intervals**) and `DIAG,late_route` (the per-route lateness split)."""

    def test_a_distribution_is_attached_as_the_interval_the_quantile_lies_in(self) -> None:
        lines = [
            "#CAP,1000,DIAG,midi_gap,372702,113",
            "#CAP,1000,DIAG,env_dist,midi_gap,0,255,256,511,17",
            "#CAP,2000,DIAG,env_dist,midi_gap,0,255,32768,65535,40",
        ]
        result = m.measure_midi_input_service_gap(lines)
        self.assertEqual(result.detail["midi_gap_p50_us_interval"], [0, 255], "the worst window's median")
        self.assertEqual(result.detail["midi_gap_p99_us_interval"], [32768, 65535], "its 99th percentile")
        self.assertEqual(result.detail["midi_gap_distribution_samples"], 57)
        self.assertEqual(result.detail["midi_gap_distribution_windows"], 2)
        self.assertIn("bucket bounds", result.detail["distribution_basis"])
        self.assertNotIn("distribution_owed", result.detail)

    def test_a_channel_without_a_sample_contributes_no_interval(self) -> None:
        # A zero-sample row is what the device does NOT emit, but a capture carrying one must still not invent an
        # interval from it.
        lines = ["#CAP,1000,DIAG,midi_input,627,2", "#CAP,1000,DIAG,env_dist,midi_input,0,0,0,0,0"]
        result = m.measure_midi_input_service_duration(lines)
        self.assertNotIn("midi_input_p99_us_interval", result.detail)
        self.assertIn("env_dist", result.detail["distribution_owed"])

    def test_the_lateness_split_names_each_route_and_the_worst_one(self) -> None:
        lines = [
            "#CAP,900,DIAG,late_route,144,8,6620,12,14,0",
            "#CAP,1900,DIAG,late_route,128,8,1200,3,5,0",
            "#CAP,2900,DIAG,late_route,144,8,7922,20,22,0",
        ]
        result = m.measure_per_route_output_lateness(lines)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.value, 7922, "the worst route's window maximum, not one aggregate maximum")
        self.assertEqual(result.detail["routes"], {"128/8": 1200, "144/8": 7922})
        self.assertEqual(result.detail["route_count"], 2)
        self.assertEqual(result.detail["worst_route_(type/ch)"], "144/8")
        self.assertEqual(result.detail["sample_count"], 41)
        self.assertEqual(result.detail["spill_samples"], 0)

    def test_a_truncated_split_carries_its_spill(self) -> None:
        lines = ["#CAP,900,DIAG,late_route,144,8,6620,12,14,3"]
        result = m.measure_per_route_output_lateness(lines)
        self.assertEqual(result.value, 6620)
        self.assertEqual(result.detail["spill_samples"], 3, "routes that found no slot are visible, not silent")

    def test_an_all_zero_split_is_unproven_not_zero_lateness(self) -> None:
        lines = ["#CAP,900,DIAG,late_route,144,8,0,0,14,0"]
        result = m.measure_per_route_output_lateness(lines)
        self.assertEqual(result.availability, m.UNPROVEN)
        self.assertIsNone(result.value)
        self.assertIn("indistinguishable", result.reason)

    def test_a_capture_without_the_split_falls_back_to_the_aggregate_and_says_so(self) -> None:
        lines = ["#CAP,900,DIAG,late_on,44234,20", "#CAP,1900,DIAG,late_off,6620,12"]
        result = m.measure_per_route_output_lateness(lines)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.value, 44234)
        self.assertIn("late_route", result.detail["split_owed"])


class TestClockCorrelation(unittest.TestCase):
    """The host-stamp ↔ device-µs correlation `action_to_audible` owed, and the base it buys."""

    def _lines(self) -> list[str]:
        # The host logs its own lines with a millisecond stamp; device rows carry only the device's µs. The smallest
        # gap between a host stamp and the nearest preceding device row is the bound on the offset.
        return [
            "#CAP,1000000,MO,144,8,0,100,96",
            "[1.001] [INFO] Button press: Record",
            "#CAP,1200000,MO,144,8,0,100,192",
            "[1.500] [INFO] something else",
            "#CAP,2000000,MO,144,8,0,100,288",
        ]

    def test_the_bound_is_the_smallest_host_to_preceding_device_gap(self) -> None:
        correlation = m.clock_correlation(self._lines())
        self.assertIsNotNone(correlation)
        self.assertEqual(correlation["pairs"], 2)
        self.assertEqual(correlation["min_gap_us"], 1000, "1.001 s against the row stamped 1.000000 s")
        self.assertEqual(correlation["host_stamp_resolution_us"], 1000)

    def test_a_capture_without_host_stamps_has_no_correlation(self) -> None:
        self.assertIsNone(m.clock_correlation(["#CAP,1000,MO,144,8,0,100,96"]))

    def test_action_to_audible_uses_the_marker_instant_and_keeps_the_positional_reading(self) -> None:
        lines = self._lines()
        result = m.measure_action_to_audible(lines, accept_row="Button press: Record")
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.value, 199000, "the first emission after the marker, from the marker's own instant")
        self.assertEqual(result.detail["device_base_us"], 1001000, "the host stamp, read as device time")
        self.assertEqual(result.detail["positional_base_us"], 1000000)
        self.assertEqual(result.detail["positional_base_interval_us"], 200000)
        self.assertEqual(result.detail["clock_correlation"]["min_gap_us"], 1000)
        self.assertEqual(result.detail["device_time_error_bound_us"], 2000)
        self.assertIn("host stamp", result.detail["base_basis"])

    def test_without_a_host_stamp_the_positional_base_is_kept(self) -> None:
        lines = ["#CAP,1000000,MO,144,8,0,100,96", "[INFO] Button press: Record", "#CAP,1002000,MO,144,8,0,100,192"]
        result = m.measure_action_to_audible(lines, accept_row="Button press: Record")
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.value, 2000, "no host stamp: the nearest preceding row's µs is the base")
        self.assertEqual(result.detail["device_base_us"], 1000000)
        self.assertNotIn("positional_base_us", result.detail)


class TestCaptureTimestampError(unittest.TestCase):
    def test_the_device_row_is_the_metric_and_its_max_is_kept_across_windows(self) -> None:
        lines = [
            "#CAP,900,DIAG,envelope_instr,capstamp_max=3,capstamp_n=4,repl_wait_us=0,repl_work_us=0,"
            "repl_refused=0,repl_pairs=0",
            "#CAP,1900,DIAG,envelope_instr,capstamp_max=7,capstamp_n=5,repl_wait_us=0,repl_work_us=0,"
            "repl_refused=0,repl_pairs=0",
        ]
        result = m.measure_capture_timestamp_error(lines)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.unit, "ticks")
        self.assertEqual(result.value, 7)  # the max across the windows
        self.assertEqual(result.source_rows, 9)  # and the samples summed
        self.assertIn("envelope_instr", result.detail["basis"])

    def test_a_capture_predating_the_row_keeps_its_proxy_as_unproven(self) -> None:
        lines = [
            "#CAP,900,REVT,1,0,60,0,100",
            "#CAP,910,REVT,1,192,62,0,100",
            "#CAP,920,REVT,1,384,64,0,100",
        ]
        result = m.measure_capture_timestamp_error(lines)
        self.assertEqual(result.availability, m.UNPROVEN)
        self.assertIsNone(result.value, "a schedule-relative proxy must not be reported as the metric")
        self.assertEqual(result.detail["proxy_worst_off_grid_ticks"], 0)
        self.assertIn("predates", result.reason)

    def test_the_off_grid_proxy_keeps_its_own_reading(self) -> None:
        lines = ["#CAP,900,REVT,1,0,60,0,100", "#CAP,910,REVT,1,232,62,0,100"]
        result = m.measure_capture_timestamp_error(lines)
        self.assertEqual(result.availability, m.UNPROVEN)
        self.assertEqual(result.detail["proxy_worst_off_grid_ticks"], 40)

    def test_no_row_and_no_stored_notes_names_the_instrument(self) -> None:
        result = m.measure_capture_timestamp_error(["#CAP,900,DIAG,midi_gap,10,0"])
        self.assertEqual(result.availability, m.MISSING)
        self.assertIn("envelope_instr", result.reason)


class TestPerRouteOutputLateness(unittest.TestCase):
    def test_all_zero_samples_are_unproven_not_zero(self) -> None:
        lines = ["#CAP,1000,DIAG,late_on,0,0", "#CAP,2000,DIAG,late_off,0,0"]
        result = m.measure_per_route_output_lateness(lines)
        self.assertEqual(result.availability, m.UNPROVEN)
        self.assertIsNone(result.value)
        self.assertIn("indistinguishable", result.reason)

    def test_a_non_zero_sample_is_measured(self) -> None:
        lines = ["#CAP,1000,DIAG,late_on,0,0", "#CAP,2000,DIAG,late_on,4200,1"]
        result = m.measure_per_route_output_lateness(lines)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.value, 4200)


class TestPerRouteOutputJitter(unittest.TestCase):
    def test_spacing_spread_is_reported_as_a_proxy(self) -> None:
        lines = [
            "#CAP,1000,MO,144,8,0,100",
            "#CAP,2000,MO,144,8,0,100",
            "#CAP,4100,MO,144,8,0,100",
        ]
        result = m.measure_per_route_output_jitter(lines)
        self.assertEqual(result.availability, m.UNPROVEN)
        self.assertIsNone(result.value, "a proxy must not be reported as the metric")
        self.assertEqual(result.detail["proxy_onset_spread_us"], 1100)
        self.assertIn("predates the emission-row tick column", result.reason)

    def test_too_few_onsets_is_missing(self) -> None:
        result = m.measure_per_route_output_jitter(["#CAP,1000,MO,144,8,0,100"])
        self.assertEqual(result.availability, m.MISSING)


class TestQueueResidence(unittest.TestCase):
    def test_reads_the_oldest_age_of_the_droid_feedback_queue(self) -> None:
        lines = [
            "#CAP,1000,DIAG,droid_queue,0,0,0",
            "#CAP,2000,DIAG,droid_queue,3,12500,0",
        ]
        result = m.measure_queue_residence(lines)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.value, 12500)
        self.assertEqual(result.detail["max_depth"], 3)

    def test_env_gated_family_is_missing_with_its_gate_named(self) -> None:
        result = m.measure_queue_residence(["#CAP,1000,DIAG,midi_gap,10,0"])
        self.assertEqual(result.availability, m.MISSING)
        self.assertIn(m.DROID_QUEUE_ENV_GATE, result.reason)


class TestRouteThroughput(unittest.TestCase):
    def test_reports_sustained_and_peak_for_the_busiest_route(self) -> None:
        lines = ["#CAP,%d,MO,144,8,0,100" % stamp for stamp in range(0, 500_000, 50_000)]
        result = m.measure_route_throughput(lines)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.detail["route_key_(type,ch)"], [144, 8])
        # 10 events over 450 ms sustained; the 100 ms peak window holds 3 of them.
        self.assertAlmostEqual(result.detail["sustained_events_per_s"], 22.222, places=2)
        self.assertEqual(result.value, 30.0)

    def test_other_routes_do_not_change_the_busiest_reading(self) -> None:
        lines = ["#CAP,%d,MO,144,8,0,100" % stamp for stamp in range(0, 300_000, 50_000)]
        lines.append("#CAP,250000,MO,128,8,0,0")
        result = m.measure_route_throughput(lines)
        self.assertEqual(result.detail["routes_seen"], 2)
        self.assertEqual(result.source_rows, 6)


class TestSameTickBurst(unittest.TestCase):
    def test_counts_events_sharing_one_emission_stamp(self) -> None:
        lines = [
            "#CAP,1000,MO,144,8,0,100",
            "#CAP,1000,MO,146,8,60,0",
            "#CAP,1000,MO,144,8,0,100",
            "#CAP,2000,MO,144,8,0,100",
        ]
        result = m.measure_same_tick_burst(lines)
        self.assertEqual(result.availability, m.UNPROVEN)
        self.assertIsNone(result.value, "a microsecond-stamp burst is not the musical-tick burst")
        self.assertEqual(result.detail["proxy_burst_events"], 3)
        self.assertEqual(result.detail["proxy_worst_emission_stamp_us"], 1000)

    def test_no_emission_rows_is_missing(self) -> None:
        self.assertEqual(m.measure_same_tick_burst(["#CAP,1,DIAG,clk,0,0"]).availability, m.MISSING)


class TestDeferredOwnerServiceAdvance(unittest.TestCase):
    def test_reads_max_delta_beside_the_declared_permitted_value(self) -> None:
        lines = [_win_gap(max_delta=0), _win_gap(max_delta=59, opportunities=256)]
        result = m.measure_deferred_owner_service_advance(lines)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.value, 59)
        self.assertEqual(result.detail["declared_permitted_ticks"], [64])

    def test_overruns_are_reported_not_smoothed(self) -> None:
        lines = [_win_gap(max_delta=120, overruns=1, overrun_max=120)]
        result = m.measure_deferred_owner_service_advance(lines)
        self.assertEqual(result.detail["overrun_sum"], 1)
        self.assertEqual(result.detail["overrun_max_ticks"], 120)

    def test_not_in_capture_is_missing_for_this_family(self) -> None:
        self.assertEqual(
            m.measure_deferred_owner_service_advance(["#CAP,1,DIAG,midi_gap,0,0"]).availability, m.MISSING
        )


class TestDeferredOwnerDenial(unittest.TestCase):
    def test_reads_denied_turns_against_the_rotation_bound(self) -> None:
        lines = [_win_gap(eligible=10, progress=10, denied=0), _win_gap(eligible=4, denied=3)]
        result = m.measure_deferred_owner_denial(lines)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.value, 3)
        self.assertEqual(result.detail["eligible_turns_sum"], 14)
        self.assertEqual(result.detail["progress_turns_sum"], 10)


class TestCaptureReplenishment(unittest.TestCase):
    def test_reclaim_cadence_is_reported_as_a_proxy(self) -> None:
        lines = [
            "#CAP,1000,DIAG,reclaim,479,479,0,0,233,Normal,0,1,0,1",
            "#CAP,4000,DIAG,reclaim,474,474,0,0,230,Normal,0,2,0,2",
        ]
        result = m.measure_capture_replenishment(lines)
        self.assertEqual(result.availability, m.UNPROVEN)
        self.assertIsNone(result.value, "an inter-reclaim cadence is not replenishment latency")
        self.assertEqual(result.detail["proxy_max_reclaim_interval_us"], 3000)
        self.assertIn("envelope_instr", result.detail["owed"])

    def test_single_reclaim_row_has_no_interval(self) -> None:
        lines = ["#CAP,1000,DIAG,reclaim,479,479,0,0,233,Normal,0,1,0,1"]
        self.assertEqual(m.measure_capture_replenishment(lines).availability, m.MISSING)


class TestActionToAudible(unittest.TestCase):
    def test_measures_from_the_acceptance_row_to_the_first_emission(self) -> None:
        lines = ["#CAP,1000,MO,144,8,0,100", "Button press: Edit Mode (short)", "#CAP,9000,MO,144,8,0,100"]
        result = m.measure_action_to_audible(lines, accept_row="Button press: Edit Mode (short)")
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.value, 8000)

    def test_without_an_acceptance_marker_the_metric_is_missing(self) -> None:
        result = m.measure_action_to_audible(["#CAP,1000,MO,144,8,0,100"])
        self.assertEqual(result.availability, m.MISSING)
        self.assertIn("acceptance marker", result.reason)

    def test_unknown_acceptance_row_is_named(self) -> None:
        result = m.measure_action_to_audible(["#CAP,1000,MO,144,8,0,100"], accept_row="no such row")
        self.assertEqual(result.availability, m.MISSING)
        self.assertIn("no such row", result.reason)

    def test_an_emission_earlier_than_the_base_refuses_the_interval(self) -> None:
        """A capture whose rows are not device-time ordered around the marker must not yield a negative gap."""
        lines = ["#CAP,1000,DIAG,clk,0,0", "Button press: Edit Mode (short)", "#CAP,500,MO,144,8,0,100"]
        result = m.measure_action_to_audible(lines, accept_row="Button press: Edit Mode (short)")
        self.assertEqual(result.availability, m.UNPROVEN)
        self.assertIsNone(result.value)
        self.assertIn("not device-time ordered", result.reason)
        self.assertEqual(result.detail["earliest_emission_us"], 500)

    def test_a_host_stamped_marker_is_reported_but_never_used_as_the_base(self) -> None:
        """Host microseconds are not the device timeline; the marker's host time is reported only."""
        lines = ["#CAP,1000,DIAG,clk,0,0", "[150.256] Button press: Edit Mode (short)", "#CAP,9000,MO,144,8,0,100"]
        result = m.measure_action_to_audible(lines, accept_row="Button press: Edit Mode (short)")
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.value, 8000)
        self.assertEqual(result.detail["device_base_us"], 1000)
        self.assertEqual(result.detail["marker_host_time_us"], 150_256_000)


class TestMeasureAllContract(unittest.TestCase):
    def test_measure_all_covers_every_named_metric_once(self) -> None:
        results = m.measure_all(["#CAP,1000,DIAG,midi_gap,5,0"])
        names = [item.metric for item in results]
        self.assertEqual(names, m.metric_names())
        self.assertEqual(len(names), len(set(names)))

    def test_every_metric_carries_a_unit_and_an_availability(self) -> None:
        for item in m.measure_all([]):
            self.assertIn(item.availability, {m.MEASURED, m.UNPROVEN, m.MISSING})
            self.assertTrue(item.unit)
            if item.availability != m.MEASURED:
                self.assertTrue(item.reason, f"{item.metric} must name why it is not measured")
            if item.availability == m.MEASURED:
                self.assertIsNotNone(item.value)


class TestTheNewInstrumentRows(unittest.TestCase):
    """The rows S7.5a increment 3 added: the emission tick column and `DIAG,envelope_instr`."""

    def test_same_tick_burst_counts_emissions_sharing_one_scheduled_tick(self) -> None:
        lines = [
            "#CAP,1000,MO,144,8,0,100,96",
            "#CAP,1010,MO,146,8,60,0,96",
            "#CAP,1020,MO,144,8,0,100,96",
            "#CAP,2000,MO,144,8,0,100,192",
        ]
        result = m.measure_same_tick_burst(lines)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.value, 3, "three emissions share scheduled tick 96")
        self.assertEqual(result.detail["worst_tick"], 96)

    def test_jitter_is_the_deviation_from_the_routes_fitted_clock_line(self) -> None:
        # A device clock of 1000 µs per tick, with one emission 700 µs late for its own scheduled tick. The fixture
        # starts at tick 48 because tick 0 is reserved for "no scheduled tick stamped" (see below).
        lines = [f"#CAP,{1000 + tick * 1000},MO,144,8,0,100,{tick}" for tick in range(48, 48 * 9, 48)]
        lines[5] = f"#CAP,{1000 + 6 * 48 * 1000 + 700},MO,144,8,0,100,{6 * 48}"
        result = m.measure_per_route_output_jitter(lines)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.value, 700)
        self.assertEqual(result.detail["fitted_us_per_tick"], 1000.0)
        self.assertEqual(result.detail["route_key_(type,ch)"], [144, 8])
        self.assertEqual(result.detail["loop_traversals_fitted"], 1)

    def test_unstamped_tick_zero_rows_stay_out_of_the_tick_readings(self) -> None:
        # Every outgoing row built without a scheduled tick reads tick 0, and on the S7.5a test image that was 443 of
        # the capture's MO rows. Counting them reported a burst nobody played.
        lines = [f"#CAP,{2000 + index},MO,144,8,0,100,0" for index in range(400)]
        lines += ["#CAP,9000,MO,144,8,0,100,96", "#CAP,9010,MO,144,8,0,100,96"]
        burst = m.measure_same_tick_burst(lines)
        self.assertEqual(burst.availability, m.MEASURED)
        self.assertEqual(burst.value, 2, "only the two stamped emissions share tick 96")

    def test_a_loop_wrap_does_not_inflate_the_jitter(self) -> None:
        # Two traversals of one loop: the tick column returns to 48 while the device clock keeps running, so a line
        # fitted across the wrap reports most of a loop as lateness — the artifact this segmentation replaced.
        first = [f"#CAP,{1000 + tick * 100},MO,144,8,0,100,{tick}" for tick in range(48, 48 * 10, 48)]
        start = 1000 + 48 * 9 * 100 + 1000
        second = [f"#CAP,{start + tick * 100},MO,144,8,0,100,{tick}" for tick in range(48, 48 * 10, 48)]
        result = m.measure_per_route_output_jitter(first + second)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.detail["loop_traversals_total"], 2)
        self.assertEqual(result.detail["loop_traversals_fitted"], 2)
        self.assertEqual(result.value, 0.0, "both traversals sit exactly on a 100 µs/tick line")

    def test_too_few_ticked_emissions_refuse_to_fit_the_line(self) -> None:
        lines = ["#CAP,1000,MO,144,8,0,100,0", "#CAP,2000,MO,144,8,0,100,48"]
        result = m.measure_per_route_output_jitter(lines)
        self.assertEqual(result.availability, m.UNPROVEN)
        self.assertIsNone(result.value, "a clock line cannot be fitted from one stamped emission")
        self.assertIn("not enough samples", result.reason)

    def test_replenishment_reads_the_request_to_completion_wait(self) -> None:
        lines = [
            "#CAP,5,DIAG,envelope_instr,capstamp_max=0,capstamp_n=0,repl_wait_us=2400,repl_work_us=180,"
            "repl_refused=3,repl_pairs=2"
        ]
        result = m.measure_capture_replenishment(lines)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertEqual(result.value, 2400)
        self.assertEqual(result.source_rows, 2)
        self.assertEqual(result.detail["completed_pairs"], 2)
        self.assertEqual(result.detail["max_replenish_work_us"], 180)

    def test_refusals_without_a_completed_pair_are_unproven(self) -> None:
        lines = [
            "#CAP,5,DIAG,envelope_instr,capstamp_max=0,capstamp_n=0,repl_wait_us=0,repl_work_us=90,"
            "repl_refused=4,repl_pairs=0"
        ]
        result = m.measure_capture_replenishment(lines)
        self.assertEqual(result.availability, m.UNPROVEN)
        self.assertIsNone(result.value)
        self.assertIn("no completed pair", result.reason)

    def test_a_malformed_row_falls_back_instead_of_reading_a_partial_row(self) -> None:
        lines = ["#CAP,5,DIAG,envelope_instr,capstamp_max=9,capstamp_n=1", "#CAP,4000,DIAG,reclaim,4,4,0,0,2,Normal,0,1,0,1"]
        self.assertEqual(m.measure_capture_timestamp_error(lines).availability, m.MISSING)
        self.assertEqual(m.measure_capture_replenishment(lines).availability, m.MISSING)


class TestAgainstRealCaptures(unittest.TestCase):
    """Structural readings from the captures this repo already holds (no frozen numbers)."""

    def _lines(self, relative: Path) -> list[str] | None:
        path = REPO_ROOT / relative
        if not path.is_file():
            self.skipTest(f"capture not present: {relative}")
            return None
        return path.read_text(encoding="utf-8", errors="replace").splitlines()

    def test_input_gap_is_measurable_on_a_real_capture(self) -> None:
        for relative in REAL_CAPTURES:
            lines = self._lines(relative)
            if lines is None:
                continue
            result = m.measure_midi_input_service_gap(lines)
            self.assertEqual(result.availability, m.MEASURED, str(relative))
            self.assertGreater(result.value, 0.0, str(relative))
            self.assertGreater(result.source_rows, 0)

    def test_output_lateness_on_a_real_capture_is_either_measured_or_unproven(self) -> None:
        for relative in REAL_CAPTURES:
            lines = self._lines(relative)
            if lines is None:
                continue
            result = m.measure_per_route_output_lateness(lines)
            self.assertIn(result.availability, {m.MEASURED, m.UNPROVEN, m.MISSING}, str(relative))
            if result.availability == m.MEASURED:
                self.assertGreater(result.value, 0.0, str(relative))
            if result.availability == m.UNPROVEN:
                self.assertIsNone(result.value, "an unproven reading must not carry a number")

    def test_throughput_is_measurable_on_a_real_capture(self) -> None:
        lines = self._lines(RealCaptureWithEmissions := Path("captures/s39-hw-receipts-run1/session_20261009_153953.log"))
        if lines is None:
            return
        result = m.measure_route_throughput(lines)
        self.assertEqual(result.availability, m.MEASURED)
        self.assertGreater(result.value, 0.0)
        self.assertGreater(result.detail["routes_seen"], 0)

    def test_queue_residence_is_absent_from_a_default_env_capture(self) -> None:
        """The DROID queue family is env-gated off in the default capture env, so its metric is missing."""
        lines = self._lines(Path("captures/note-edit-enter-24bar-t8s4-01/session_20261010_194956.log"))
        if lines is None:
            return
        result = m.measure_queue_residence(lines)
        self.assertEqual(result.availability, m.MISSING)
        self.assertIn(m.DROID_QUEUE_ENV_GATE, result.reason)


if __name__ == "__main__":
    unittest.main()
