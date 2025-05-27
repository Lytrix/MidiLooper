#include "EditStartNoteState.h"
#include "EditManager.h"
#include "MidiEvent.h"
#include "Track.h"
#include <vector>
#include <algorithm>
#include <cstdint>
#include "Logger.h"
#include <map>

void EditStartNoteState::onEnter(EditManager& manager, Track& track, uint32_t startTick) {
    // Optionally log or highlight
    logger.debug("Entered EditStartNoteState");
}

void EditStartNoteState::onExit(EditManager& manager, Track& track) {
    // Optionally log or cleanup
    logger.debug("Exited EditStartNoteState");
}

void EditStartNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    int noteIdx = manager.getSelectedNoteIdx();
    if (noteIdx < 0) return;
    auto& midiEvents = track.getMidiEvents(); // ensure non-const
    uint32_t loopLength = track.getLength();
    // Reconstruct notes (supporting multiple overlapping notes of same pitch)
    struct DisplayNote {
        uint8_t note;
        uint8_t velocity;
        uint32_t startTick;
        uint32_t endTick;
    };
    std::vector<DisplayNote> notes;
    std::vector<DisplayNote> activeNotes;
    for (const auto& evt : midiEvents) {
        if (evt.type == midi::NoteOn && evt.data.noteData.velocity > 0) {
            activeNotes.push_back({evt.data.noteData.note, evt.data.noteData.velocity, evt.tick, evt.tick});
        } else if ((evt.type == midi::NoteOff) || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) {
            auto itAct = std::find_if(activeNotes.begin(), activeNotes.end(), [&](const DisplayNote& a) {
                return a.note == evt.data.noteData.note && a.endTick == a.startTick;
            });
            if (itAct != activeNotes.end()) {
                DisplayNote closed = *itAct;
                closed.endTick = evt.tick;
                notes.push_back(closed);
                activeNotes.erase(itAct);
            }
        }
    }
    for (auto& a : activeNotes) {
        a.endTick = loopLength;
        notes.push_back(a);
    }
    if (noteIdx >= (int)notes.size()) return;
    // Find the corresponding MidiEvent indices for this note
    const auto& dn = notes[noteIdx];
    // Find the NoteOn and NoteOff events in midiEvents using non-const iterators
    auto onIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        return evt.type == midi::NoteOn && evt.data.noteData.note == dn.note && evt.tick == dn.startTick;
    });
    auto offIt = std::find_if(midiEvents.begin(), midiEvents.end(), [&](MidiEvent& evt) {
        return (evt.type == midi::NoteOff || (evt.type == midi::NoteOn && evt.data.noteData.velocity == 0)) && evt.data.noteData.note == dn.note && evt.tick == dn.endTick;
    });
    if (onIt == midiEvents.end() || offIt == midiEvents.end()) return;

    // --- Compute new start/end for moved note ---
    int32_t newStart = ((int32_t)dn.startTick + delta + (int32_t)loopLength) % (int32_t)loopLength;
    int32_t noteLen = (int32_t)dn.endTick - (int32_t)dn.startTick;
    if (noteLen < 0) noteLen += loopLength; // handle wrap-around
    int32_t newEnd = (newStart + noteLen) % (int32_t)loopLength;
    if (newEnd == newStart) newEnd = (newStart + 1) % (int32_t)loopLength;

    // Helper for overlap (handles wrap-around)
    auto overlaps = [loopLength](uint32_t aStart, uint32_t aEnd, uint32_t bStart, uint32_t bEnd) {
        // Treat ranges as inclusive: overlap if aStart <= bEnd && bStart <= aEnd, handling wrap-around
        if (aStart < aEnd) {
            if (bStart < bEnd)
                return (aStart <= bEnd && bStart <= aEnd);
            else // b wraps
                return (aStart <= bEnd || bStart <= aEnd);
        } else { // a wraps
            if (bStart < bEnd)
                return (bStart <= aEnd || aStart <= bEnd);
            else // both wrap
                return true;
        }
    };

    // Update moved note on/off first
    onIt->tick = newStart;
    offIt->tick = newEnd;
    std::sort(midiEvents.begin(), midiEvents.end(), [](const MidiEvent& a, const MidiEvent& b){ return a.tick < b.tick; });
    
    // Handle overlapping notes: always restore then remove
    auto& removedMap = manager.temporarilyRemovedNotes[&track][dn.note];
    // Restore previously removed notes no longer overlapping (or stepping onto start/end)
    {
        std::vector<EditManager::RemovedNote> prevRemoved = removedMap;
        removedMap.clear();
        logger.debug("Restoring %zu notes", prevRemoved.size());
        for (auto& r : prevRemoved) {
            // Only restore when the moved note no longer overlaps
            if (!overlaps(newStart, newEnd, r.startTick, r.endTick)) {
                for (const auto& evt : r.events) {
                    bool exists = std::any_of(midiEvents.begin(), midiEvents.end(), [&](const MidiEvent& e){
                        return e.tick==evt.tick && e.type==evt.type && e.channel==evt.channel
                            && e.data.noteData.note==evt.data.noteData.note;
                    });
                    if (!exists) midiEvents.push_back(evt);
                }
                logger.debug("Restored note: note=%d [%lu..%lu]", r.note, r.startTick, r.endTick);
            } else {
                removedMap.push_back(r);
            }
        }
        std::sort(midiEvents.begin(), midiEvents.end(), [](const MidiEvent& a, const MidiEvent& b){ return a.tick < b.tick; });
    }
    // Remove newly overlapping notes
    {
        std::vector<DisplayNote> currNotes;
        std::vector<DisplayNote> currActive;
        for (const auto& e : midiEvents) {
            if (e.type==midi::NoteOn && e.data.noteData.velocity>0) currActive.push_back({e.data.noteData.note,e.data.noteData.velocity,e.tick,e.tick});
            else if ((e.type==midi::NoteOff)||(e.type==midi::NoteOn&&e.data.noteData.velocity==0)) {
                auto ita = std::find_if(currActive.begin(),currActive.end(),[&](const DisplayNote& a){return a.note==e.data.noteData.note&&a.endTick==a.startTick;});
                if (ita!=currActive.end()){auto c=*ita;c.endTick=e.tick;currNotes.push_back(c);currActive.erase(ita);} }
        }
        for (auto& a:currActive){a.endTick=loopLength;currNotes.push_back(a);}        
        logger.debug("Removing on move: checking %zu notes", currNotes.size());
        for (auto& other : currNotes) {
            if (other.note != dn.note) continue;
            if (other.startTick==(uint32_t)newStart && other.endTick==(uint32_t)newEnd) continue;
            bool ov = overlaps(newStart, newEnd, other.startTick, other.endTick);
            // Only remove notes that actually overlap
            if (!ov) continue;
            bool already=false;
            for (auto& r: removedMap) if(r.startTick==other.startTick&&r.endTick==other.endTick){already=true;break;}
            if(already) continue;
            std::vector<MidiEvent> toRemove;
            for (auto it=midiEvents.begin(); it!=midiEvents.end();) {
                if(it->data.noteData.note==other.note&&((it->type==midi::NoteOn&&it->tick==other.startTick)||((it->type==midi::NoteOff||(it->type==midi::NoteOn&&it->data.noteData.velocity==0))&&it->tick==other.endTick))){
                    toRemove.push_back(*it);
                    it=midiEvents.erase(it);
                } else { ++it; }
            }
            if(toRemove.size()==2){removedMap.push_back({other.note,other.velocity,other.startTick,other.endTick,toRemove});
                logger.debug("Removed note: note=%d [%lu..%lu]",other.note,other.startTick,other.endTick);}
        }
        std::sort(midiEvents.begin(),midiEvents.end(),[](const MidiEvent&a,const MidiEvent&b){return a.tick<b.tick;});
    }

    // Rebuild notes after movement to find new index of moved note
    notes.clear();
    activeNotes.clear();
    for (const auto& evt2 : midiEvents) {
        if (evt2.type == midi::NoteOn && evt2.data.noteData.velocity > 0) {
            activeNotes.push_back({evt2.data.noteData.note, evt2.data.noteData.velocity, evt2.tick, evt2.tick});
        } else if ((evt2.type == midi::NoteOff) || (evt2.type == midi::NoteOn && evt2.data.noteData.velocity == 0)) {
            auto itAct2 = std::find_if(activeNotes.begin(), activeNotes.end(), [&](const DisplayNote& a) {
                return a.note == evt2.data.noteData.note && a.endTick == a.startTick;
            });
            if (itAct2 != activeNotes.end()) {
                DisplayNote closed2 = *itAct2;
                closed2.endTick = evt2.tick;
                notes.push_back(closed2);
                activeNotes.erase(itAct2);
            }
        }
    }
    for (auto& a2 : activeNotes) {
        a2.endTick = loopLength;
        notes.push_back(a2);
    }
    // Find the moved note in the new note list
    int newSelectedIdx = -1;
    for (int i = 0; i < (int)notes.size(); ++i) {
        if (notes[i].note == dn.note && notes[i].startTick == (uint32_t)newStart) {
            newSelectedIdx = i;
            break;
        }
    }
    // Update manager selection to the moved note
    manager.resetSelection();
    manager.setBracketTick(newStart);
    if (newSelectedIdx >= 0) {
        manager.setSelectedNoteIdx(newSelectedIdx);
    } else {
        // Fallback: select closest note if moved note not found
        manager.selectClosestNote(track, newStart);
    }
    logger.debug("onEncoderTurn end: newStart=%ld selectedNoteIdx=%d removedMapSize=%zu", newStart, manager.getSelectedNoteIdx(), removedMap.size());
}

void EditStartNoteState::onButtonPress(EditManager& manager, Track& track) {
    // Switch back to note state
    manager.setState(manager.getNoteState(), track, manager.getBracketTick());
} 