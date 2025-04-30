# Midi Looper #


This Midilooper has been created with help of chatgpt. It has proven to be a great tutor to help me guide and understand every added feature's logic. It saved me a lot of time looking for the correct approach or answers on how to tackle things.

The main inspiration was taken from this minimalistic 4 track looper using only 2 buttons and 4 digits:
https://iestyn-lewis.github.io/4by8/

## Software ##
The chat can be viewed here of my journey. 
 
MidiLooper (First one. Got really messy with too many workarounds, but with auto load/save functionality from SD)
https://chatgpt.com/share/680a4839-6720-800b-ae73-9aff16f6e41f

MidiLooperV2 (Simpler switch logic for tracks, focussing on workflow logic)
https://chatgpt.com/share/680e999a-c860-800b-a079-9862a59f1e89

MidiLooperV3 (Started off from a framework in C++, hope it will be more stable to use)
https://chatgpt.com/share/680e98f9-2a64-800b-abb2-4e1bd359c90f

## Hardware ##
- 1x Teensy 4.1
- 2x momentary button
- 1x liquid lcd display 16x2
- 1x 6N137

The midi circuit is based on https://www.pjrc.com/teensy/td_libs_MIDI.html

## Features ##
- 4 Midi Tracks
- 192 PPQN internal clock for live recording
- 24 PPQN midi Sync
- 16x2 information display
- Automatic saving of Loops on SD card
- Reload last used state and loops on startup

  - 2 button operation        
```
| **Button** | **Times Pressed** | **Press Type** | **Current State**              | **Action**                                               | **Next State**              |
|------------|-------------------|----------------|--------------------------------|----------------------------------------------------------|-----------------------------|
| A          | 1                 | Short          | Not armed/recording/playing    | Start recording                                          | `TRACK_RECORDING`           |
| A          | 2                 | Short          | `TRACK_RECORDING`              | Stop recording → Start playing + overdubbing            | `TRACK_OVERDUBBING`         |
| A          | 3                 | Short          | `TRACK_OVERDUBBING`            | Stop overdubbing                                         | `TRACK_PLAYING`             |
| A          | ≥4                | Short          | `TRACK_PLAYING`                | Start live overdubbing                                   | `TRACK_OVERDUBBING`         |
| A          | Any               | Long           | Any                            | 🔥 Clear selected track (erase all events)               | `TRACK_STOPPED`             |
| B          | 1 (each time)     | Short          | Any                            | Switch to next track                                     | (Next track selected)       |
| B          | Any               | Long           | Any                            | Toggle mute on current track                             | (Same, toggle mute flag)    |
````
- Retroactive bar-quantized recording (record complete bars, but allow earlier recording start)
```
| 1   2   3   4 | 1   2   3   4 |  
          ^ You press record here (beat 3)
                		    	^ You press stop here (beat 4)  

New Loop ready for overdub to add the notes in the first bar.
| 1   2   3   4 | 1   2   3   4 |   
```


## Module breakdown ##

- Globals		: Shared variables or constants.
- TrackManager  : Controls which track is modified
- Track			: Handle sequencing and recording.
- LooperState	: tracks main state of the looper
- ButtonManager : Manages physical button input.
- Clock 		: Timing and sync
- ClockManager  : Timing sync. Also fires of the each tick to process playEvents and midiRecordEvents.
- MidiHandler	: Handles MIDI input/output.
- DisplayManager: Showing status/info and notes per loop on a 16x2 LCD.

- LooperState: Tracks the state machine for looping.
