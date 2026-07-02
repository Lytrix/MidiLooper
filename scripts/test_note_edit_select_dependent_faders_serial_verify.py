#!/usr/bin/env python3
"""Unit tests for NOTE_EDIT select-dependent fader motor serial verification."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_SCRIPT_DIR = Path(__file__).resolve().parent
if str(_SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPT_DIR))

from hitl.verify.note_edit_select_dependent_faders import (
    verify_note_edit_select_dependent_faders,
    verify_outbound_motor_values,
)


def _pb_wire(pb: int) -> tuple[int, int]:
    raw = pb + 8192
    return raw & 0x7F, (raw >> 7) & 0x7F


class NoteEditSelectDependentFadersSerialVerifyTests(unittest.TestCase):
    def _motor_sync_bundle(
        self,
        apply_t: float,
        *,
        note_idx: int = 0,
        f2_pb: int = 100,
        f4_pitch: int = 60,
        motor_delay_s: float = 0.35,
    ) -> list[str]:
        d1, d2 = _pb_wire(f2_pb)
        motor_t = apply_t + motor_delay_s
        return [
            (
                f"[{motor_t + 0.01:.3f}] #DBG outbound_ctx f2 anchor_tick=96 rel_tick=96 "
                f"loop_start=0 loop_len=1536 pb={f2_pb} expected_pb_rel={f2_pb} "
                f"step=6 f1_pb=0 slot=1 mode=SELECT_SYNC"
            ),
            (
                f"[{motor_t + 0.02:.3f}] #DBG outbound_ctx f4 pitch={f4_pitch} "
                f"selected_idx={note_idx} mode=SELECT_SYNC"
            ),
            (
                f"[{motor_t + 0.03:.3f}] #DBG select_motor_sync sent=1 note_idx={note_idx} "
                f"prior_note_idx=-1 reason=note_changed f2_pb={f2_pb} f4_cc={f4_pitch} "
                f"prior_f2_pb=0 prior_f4_cc=0 motor_value_changed=1"
            ),
            f"[{motor_t + 0.04:.3f}] #DBG select_motor_sync mode=SELECT_SYNC sequence=parallel_timed_burst",
            f"[{motor_t + 0.05:.3f}] #CAP,1,MO,224,14,{d1},{d2}",
            f"[{motor_t + 0.06:.3f}] #CAP,1,MO,144,14,0,127",
            f"[{motor_t + 0.061:.3f}] #CAP,1,MI,H,144,13,82,127",
            f"[{motor_t + 0.07:.3f}] #CAP,1,MO,176,15,2,64",
            f"[{motor_t + 0.08:.3f}] #CAP,1,MO,144,15,1,127",
            f"[{motor_t + 0.081:.3f}] #CAP,1,MI,H,144,13,84,127",
            f"[{motor_t + 0.09:.3f}] #CAP,1,MO,176,15,3,{f4_pitch}",
            f"[{motor_t + 0.10:.3f}] #CAP,1,MO,144,15,2,127",
            f"[{motor_t + 0.101:.3f}] #CAP,1,MI,H,144,13,86,127",
            f"[{motor_t + 0.14:.3f}] #CAP,1,MI,H,224,14,{d1},{d2}",
            f"[{motor_t + 0.15:.3f}] #CAP,1,MI,H,176,15,3,{f4_pitch}",
        ]

    def _note_changed_bundle(
        self,
        t: float,
        *,
        note_idx: int = 0,
        f2_pb: int = 100,
        f4_pitch: int = 60,
        motor_delay_s: float = 0.35,
    ) -> list[str]:
        return [
            f"[{t:.3f}] #DBG select_apply bracket_tick=96 slot=1 prior_slot=0 apply=1 reason=note_changed note_idx={note_idx}",
            *self._motor_sync_bundle(
                t, note_idx=note_idx, f2_pb=f2_pb, f4_pitch=f4_pitch, motor_delay_s=motor_delay_s
            ),
        ]

    def test_ok_single_note_changed_apply(self) -> None:
        lines = self._note_changed_bundle(1.0)
        result = verify_note_edit_select_dependent_faders(lines)
        self.assertTrue(result["ok"], result.get("issues"))

    def test_ok_debounced_cluster_after_multiple_applies(self) -> None:
        lines = [
            "[1.000] #DBG select_apply bracket_tick=96 slot=0 prior_slot=-1 apply=1 reason=note_changed note_idx=0",
            "[1.100] #DBG select_apply bracket_tick=144 slot=1 prior_slot=0 apply=1 reason=note_changed note_idx=1",
            "[1.200] #DBG select_apply bracket_tick=192 slot=2 prior_slot=1 apply=1 reason=note_changed note_idx=2",
        ]
        lines.extend(self._motor_sync_bundle(1.2, note_idx=2, f2_pb=120, f4_pitch=62))
        result = verify_note_edit_select_dependent_faders(lines)
        self.assertTrue(result["ok"], result.get("issues"))
        triple = result["note_edit_select_triple_motor_ack"]
        self.assertEqual(3, triple["note_changed_count"])
        self.assertEqual(1, triple.get("dwell_cluster_count", 1))

    def test_fail_when_f2_mo_value_mismatch(self) -> None:
        lines = self._note_changed_bundle(1.0, f2_pb=100)
        wrong_d1, wrong_d2 = _pb_wire(50)
        lines[5] = f"[1.400] #CAP,1,MO,224,14,{wrong_d1},{wrong_d2}"
        result = verify_outbound_motor_values(lines)
        self.assertFalse(result["outbound_motor_values_ok"])
        self.assertGreater(result["f2_value_misses"], 0)

    def test_fail_when_no_motor_sync_on_note_changed(self) -> None:
        lines = [
            "[1.000] #DBG select_apply slot=1 prior_slot=0 apply=1 reason=note_changed note_idx=0",
        ]
        result = verify_note_edit_select_dependent_faders(lines)
        self.assertFalse(result["ok"])
        self.assertTrue(any("triple_motor_ack_failed" in issue for issue in result["issues"]))

    def test_rc11_pb_rel_mismatch_reported(self) -> None:
        lines = self._note_changed_bundle(1.0, f2_pb=100)
        lines[1] = (
            "[1.010] #DBG outbound_ctx f2 anchor_tick=96 rel_tick=96 "
            "loop_start=424 loop_len=1536 pb=100 expected_pb_rel=50 "
            "step=6 f1_pb=0 slot=1 mode=SELECT_SYNC"
        )
        result = verify_note_edit_select_dependent_faders(lines)
        self.assertGreater(result["f2_pb_rel_mismatches"], 0)
        self.assertTrue(
            any("f2_pb_rel_mismatches" in issue for issue in result["issues"])
        )


if __name__ == "__main__":
    unittest.main()
