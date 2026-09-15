#!/usr/bin/env python3
import unittest
import mido
from hitl.stop_intervention import (
    StopInterventionPort,
    strict_normal_stop_transition_options,
    strict_stop_transition_options,
    verify_interruption,
)


class ConcreteOutput(mido.ports.BaseOutput):
    # RtMidi overrides BaseOutput.send; the fixture must use this exact method.
    def __init__(self):
        super().__init__()
        self.messages = []
    def send(self, message):
        self.messages.append(message)


class StopInterventionTests(unittest.TestCase):
    def setUp(self):
        self.output, self.events, self.callbacks = ConcreteOutput(), [], []
        def schedule(delay, callback):
            self.assertEqual(delay, .305)
            self.callbacks.append(callback)
        self.port = StopInterventionPort(self.output, self.events.append, schedule=schedule)

    def button(self, type, velocity):
        self.port.send(mido.Message(type, channel=15, note=36, velocity=velocity))

    def test_concrete_override_and_stream_stop_release_trigger_once(self):
        self.button('note_on',127); self.button('note_off',0)  # start, not stop
        self.assertFalse(self.callbacks)
        self.port.send(mido.Message('note_on',channel=4,note=60,velocity=90))
        self.button('note_on',127)
        self.assertEqual(self.output.messages[-2].note,83)
        self.assertFalse(self.callbacks)
        self.button('note_off',0)
        self.assertEqual(len(self.callbacks),1)
        self.assertFalse(self.port.sent)
        self.callbacks[0]()
        self.assertTrue(self.port.sent)
        self.assertEqual(self.output.messages[-2].type,'stop')
        self.button('note_on',127); self.button('note_off',0)
        self.assertEqual(len(self.callbacks),1)

    def test_already_overdubbing_needs_no_first_button_press(self):
        self.port.send(mido.Message('note_on',channel=4,note=60,velocity=90))
        self.button('note_on',127); self.button('note_on',0)
        self.assertEqual(len(self.callbacks),1)

    def test_stale_playing_cannot_skip_stop_and_retries_are_disabled(self):
        options = strict_stop_transition_options({'requested':'PLAYING_AFTER_OVERDUB_STOP',
                                                 'already_satisfied':lambda a,b:True,'max_retries':1})
        self.assertFalse(options['already_satisfied']([],0))
        self.assertEqual(options['max_retries'],0)
        self.assertFalse(options['confirmed'](['#CAP,1,ST,Track,OVERDUBBING,PLAYING'],0))
        self.assertTrue(options['confirmed'](['#CAP,2,ST,Track,OVERDUB_STOPPING,STOPPED'],0))
        self.assertFalse(options['confirmed'](['#CAP,2,ST,Track,OVERDUB_STOPPING,STOPPED'],1))

    def test_normal_stop_requires_fresh_stopping_to_playing_transition(self):
        options = strict_normal_stop_transition_options(
            {'requested': 'PLAYING_AFTER_OVERDUB_STOP', 'already_satisfied': lambda a, b: True}
        )
        self.assertFalse(options['already_satisfied']([], 0))
        self.assertFalse(options['confirmed'](['#CAP,1,ST,Track,OVERDUBBING,PLAYING'], 0))
        self.assertTrue(options['confirmed'](['#CAP,2,ST,Track,OVERDUB_STOPPING,PLAYING'], 0))

    def test_unrelated_channel_cannot_arm(self):
        self.port.send(mido.Message('note_on',channel=3,note=60,velocity=90))
        self.button('note_on',127); self.button('note_off',0)
        self.assertFalse(self.callbacks)


class InterruptionVerdictTests(unittest.TestCase):
    def setUp(self):
        self.lines = ["#CAP,100,ST,Track,OVERDUBBING,OVERDUB_STOPPING",
                      "#CAP,110,MI,U,252,0,0,0", "#CAP,115,MO,176,5,123,0",
                      "#CAP,200,ST,Track,OVERDUB_STOPPING,STOPPED",
                      "#CAP,8000200,DIAG,clockrate,0"]
        self.actions = [{'event':'midi_stop_sent','host_ns':1000}]
        self.midi = [{'event':'monitor_opened','host_ns':0},
                     {'event':'monitor_closed','host_ns':8000001000}]
    def test_inside_stop_and_bounded_silence_passes_only_focused_scope(self):
        result=verify_interruption(self.lines,self.actions,self.midi)
        self.assertEqual(result['result'],'PASS')
        self.assertEqual(result['intervention_offset_us'],10)
        self.assertEqual(result['all_notes_off_delay_us'],5)
        self.assertFalse(result['restart_survival_verified'])
    def test_stop_after_destination_is_inconclusive(self):
        self.lines[1]="#CAP,210,MI,U,252,0,0,0"
        self.assertEqual(verify_interruption(self.lines,self.actions,self.midi)['result'],'INCONCLUSIVE_CAPTURE')
    def test_missing_monitor_completion_cannot_pass(self):
        self.assertEqual(verify_interruption(self.lines,self.actions,self.midi[:1])['result'],'INCONCLUSIVE_CAPTURE')
    def test_resumed_target_note_fails(self):
        self.lines.append('#CAP,300,MO,144,5,60,100')
        self.assertEqual(verify_interruption(self.lines,self.actions,self.midi)['result'],'FAIL')
    def test_overflow_cannot_pass(self):
        self.lines.append('#CAP,300,RING,overflow,120')
        self.assertEqual(verify_interruption(self.lines,self.actions,self.midi)['result'],'INCONCLUSIVE_CAPTURE')


if __name__ == '__main__':
    unittest.main()
