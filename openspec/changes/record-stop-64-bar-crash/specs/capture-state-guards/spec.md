## ADDED Requirements

### Requirement: Long record stop transitions to play
After long record stop finalize, the track SHALL transition from `STOPPED_RECORDING` to `PLAYING` without requiring a second manual stop press.

#### Scenario: 64-bar stop reaches play
- **WHEN** a 64-bar record pass is stopped from `TRACK_RECORDING`
- **THEN** the state sequence includes `RECORDING -> STOPPED_RECORDING -> PLAYING`
- **AND** overdub start can enter `TRACK_OVERDUBBING` from that post-stop play state

### Requirement: Stop-path finalize does not drop transport clock
Record stop finalize SHALL not leave transport without usable clock for overdub entry.

#### Scenario: Clock remains valid after long stop
- **WHEN** record stop finalize completes for a long record pass
- **THEN** transport clock remains available for the following overdub phase
- **AND** baseline verification does not abort with `midi clock missing before overdub phase`
