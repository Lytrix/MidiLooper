# Record stop length shared helpers

Behavior-preserving extraction of pure record-stop length math from `Track` into [`RecordStopLength`](../../include/Utils/RecordStopLength.h).

## Helpers

| Function | Role |
|----------|------|
| `quantizeTransportRecordLength` | Bar-quantize raw capture length (grace = half bar) |
| `computeLoopLengthTicks` | Content length from last event tick (grace = 1/6 bar) |
| `computeRecordStopLengthTicks` | `min(transport, content)` (or transport if no content) |
| `computeTruncationRewindTicks` | Rewind ticks when final length snaps shorter than raw |

## Wiring

- `Track::quantizeTransportRecordLength` / `computeLoopLengthTicks` / `computeRecordStopLengthTicks` — thin delegates
- `stopRecording` / `stopRecordingToStopped` — call `RecordStopLength::computeTruncationRewindTicks`
- Native [`test_record_stop_length`](../../test/test_record_stop_length/test_record_stop_length.cpp) includes util `.cpp` directly (no Track)

## Ownership

`Track::prepareRecordStop` still owns *when* length is applied. Util owns only pure tick math.
