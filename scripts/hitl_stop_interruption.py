#!/usr/bin/env python3
"""Run the focused Stop-interruption checkpoint on an explicitly named scratch slot.

Uses the existing single-overdub recipe and managed serial owner. The focused
verdict covers interruption/continued silence only, independently of the broader
baseline verifier. It never builds or uploads firmware.
"""
import sys, time, json, threading, pathlib, mido
root = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'scripts'))
from hitl import legacy_record_baseline as baseline
from hitl.runner import main
import argparse

def run_fixture():
    parser = argparse.ArgumentParser(description='Focused Stop-during-overdub-stopping HITL; clears the specified scratch slot')
    parser.add_argument('--track-number', type=int, required=True, choices=range(1, 9))
    parser.add_argument('--loop-slot', type=int, required=True, choices=range(1, 9))
    parser.add_argument('--out-dir', type=pathlib.Path, required=True)
    parser.add_argument('--intervention-delay-ms', type=float, default=305.0,
                        help='Delay from overdub-stop button release to transport Stop.')
    parser.add_argument('--overdub-bars', type=int, default=2,
                        help='Overdub duration used by the fixture before its Stop button.')
    parser.add_argument('--record-bars', type=int, default=2)
    parser.add_argument('--tempo-bpm', type=float, default=120.0)
    parser.add_argument('--overdub-low-note', type=int, default=24,
                        help='Lowest generated overdub note; choose a lane disjoint from the seed to avoid companion edits.')
    parser.add_argument('--overdub-high-note', type=int, default=39,
                        help='Highest generated overdub note.')
    parser.add_argument('--overdub-step-clocks', type=int, default=12,
                        help='Generated overdub grid spacing in MIDI clocks.')
    parser.add_argument('--overdub-gate-clocks', type=int, default=12,
                        help='Generated overdub gate length in MIDI clocks.')
    parser.add_argument('--final-wait-ms', type=int, default=8000)
    parser.add_argument('--normal-stop', action='store_true',
                        help='Require a fresh OVERDUB_STOPPING -> PLAYING stop without transport Stop.')
    config = parser.parse_args()
    out = config.out_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    log = (out / 'intervention.jsonl').open('w', buffering=1)
    midilog = (out / 'midi-output.jsonl').open('w', buffering=1)
    port = None
    finished = threading.Event()

    def monitor():
        nonlocal port
        while not finished.is_set():
            if port is None and (out / '.current_session.ready').exists():
                try:
                    port = mido.open_input('Teensy MIDI', callback=lambda m: midilog.write(json.dumps({'host_ns': time.monotonic_ns(), 'message': m.dict()}) + '\n'))
                    midilog.write(json.dumps({'host_ns': time.monotonic_ns(), 'event': 'monitor_opened'}) + '\n')
                except Exception:
                    pass
            finished.wait(0.1)
    from hitl.stop_intervention import StopInterventionPort
    original_pass = baseline._run_overdub_pass
    original_transition = baseline.request_state_transition
    from hitl.stop_intervention import (
        strict_normal_stop_transition_options,
        strict_stop_transition_options,
    )

    def focused_transition(**options):
        policy = strict_normal_stop_transition_options if config.normal_stop else strict_stop_transition_options
        return original_transition(**(policy(options) if config.normal_stop else
            policy(options, track=config.track_number - 1, slot=config.loop_slot - 1)))
    baseline.request_state_transition = focused_transition
    interventions = []

    def run_pass(*args, **kw):
        wrapped = StopInterventionPort(
            args[1], lambda row: log.write(json.dumps(row) + '\n'),
            delay_s=config.intervention_delay_ms / 1000.0,
            channel=int(args[5].midi_channel) - 1)
        interventions.append(wrapped)
        changed = list(args)
        changed[1] = wrapped
        log.write(json.dumps({'host_ns': time.monotonic_ns(), 'event': 'overdub_pass_wrapper_entered'}) + '\n')
        try:
            return original_pass(*changed, **kw)
        finally:
            wrapped.finish()
            if not wrapped.sent:
                args[1].send(mido.Message('stop'))
                log.write(json.dumps({'host_ns': time.monotonic_ns(), 'event': 'cleanup_stop_no_intervention'}) + '\n')
    if not config.normal_stop:
        baseline._run_overdub_pass = run_pass
    thread = threading.Thread(target=monitor)
    thread.start()
    args = ['run', '--preset', 'base', '--midi-out', 'Teensy', '--midi-in', 'Teensy', '--track-number', str(config.track_number), '--loop-slot', str(config.loop_slot), '--midi-channel', str(config.track_number), '--record-bars', str(config.record_bars), '--overdub-bars', str(config.overdub_bars), '--overdub-low-note', str(config.overdub_low_note), '--overdub-high-note', str(config.overdub_high_note), '--overdub-step-clocks', str(config.overdub_step_clocks), '--overdub-gate-clocks', str(config.overdub_gate_clocks), '--tempo-bpm', str(config.tempo_bpm), '--no-fixed-grid-notes', '--start-transport', '--overdub-start-delay-bars', '0', '--overdub-start-delay-beats', '1', '--phase-wait-ms', '500', '--final-wait-ms', str(config.final_wait_ms), '--press-ms', '120', '--no-undo-redo-after-overdub-stop', '--second-overdub-bars', '0', '--no-stop-after-overdub', '--deferred-save-wait-ms', '15000', '--out-dir', str(out), '--stimulus-temp-parent', 'STIM-BASE-2-ONE-OD-01', '--stimulus-temp-suffix', 'STOP-NORMAL' if config.normal_stop else 'STOP-INTERRUPT', '--stimulus-temp-reason', 'Fresh normal OVERDUB_STOPPING to PLAYING transition with device-clock timeline' if config.normal_stop else 'Held note 83 and timed MIDI Stop after overdub-stop button release; verify actual stopping overlap from fresh serial evidence']
    (out / 'command.json').write_text(json.dumps(args, indent=2))
    try:
        code = main(args)
        print('Baseline return:', code, 'intervention sent:', any((x.sent for x in interventions)), flush=True)
    finally:
        if not config.normal_stop:
            baseline._run_overdub_pass = original_pass
        baseline.request_state_transition = original_transition
        finished.set()
        thread.join()
        if port:
            port.close()
            midilog.write(json.dumps({'host_ns': time.monotonic_ns(), 'event': 'monitor_closed'}) + '\n')
        log.close()
        midilog.close()
    from hitl.stop_intervention import verify_interruption
    serial_logs = list(out.glob('session_*.log'))
    if serial_logs and not config.normal_stop:
        result = verify_interruption(serial_logs[-1].read_text(errors='replace').splitlines(), [json.loads(x) for x in (out / 'intervention.jsonl').read_text().splitlines()], [json.loads(x) for x in (out / 'midi-output.jsonl').read_text().splitlines()], channel=config.track_number - 1, slot=config.loop_slot - 1)
        (out / 'interruption-result.json').write_text(json.dumps(result, indent=2) + '\n')
        print('Focused interruption:', json.dumps(result), flush=True)
if __name__ == '__main__':
    run_fixture()
