# Midi Looper #


![midilooper.jpg](Images/midilooper.jpg)


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

## 🔴 Button A – Recording, Overdubbing, Playback Control ##

| Press #     | From State         | To State           | Symbol Change | Key Action         |
| ----------- | ------------------ | ------------------ | ------------- | ------------------ |
| 1× (single) | TRACK\_EMPTY       | TRACK\_RECORDING   | – → R         | `startRecording()` |
| 2× (single) | TRACK\_PLAYING     | TRACK\_OVERDUBBING | P → O         | `startOverdub()`   |
| 3× (single) | TRACK\_OVERDUBBING | TRACK\_PLAYING     | O → P         | `stopOverdub()`    |
| Long        | (any)              | TRACK\_EMPTY       | → –           | `clearTrack()`     |


## 🔵 Button B – Track Select and Mute Control ##

|  Press #    | From State         | To State           | Key Action                 |
| ----------- | ------------------ | ------------------ | -------------------------- |
| 1× (single) | Selected Track     | Select next track  | `setSelectedTrack()`       |
| Long        | (any               | TRACK\_MUTED       | `isAudible(0)`             |


- Retroactive bar-quantized recording (record complete bars, but allow earlier recording start)
```
| 1   2   3   4 | 1   2   3   4 |  
          ^ You press record here (beat 3)
                		    	^ You press stop here (beat 4)  

New Loop ready for overdub to add the notes in the first bar.
| 1   2   3   4 | 1   2   3   4 |   
```


## 🔧 Module Relationships and Data Flow ##

| Module         | Key Data                            | Connected Modules             | Purpose                                                                 |
|----------------|--------------------------------------|-------------------------------|-------------------------------------------------------------------------|
| `ButtonManager`| Button press type, `millis()`       | `TrackManager`, `ClockManager`| Triggers recording/playback/overdub/mute/clear actions                 |
| `TrackManager` | `selectedTrack`, `masterLoopLength` | `Track`, `ClockManager`       | Manages track states and coordination, handles quantized events        |
| `Track`        | `MidiEvent`, `NoteEvent`, `startLoopTick`, `loopLengthTicks` | N/A                        | Stores and manages MIDI/Note data, performs playback and recording     |
| `ClockManager` | `currentTick`                       | All other modules             | Provides global timing for sync and quantization                       |
| `DisplayManager`| —                                   | `TrackManager`, `Track`       | Displays NoteEvents, loop status, and other track information          |

