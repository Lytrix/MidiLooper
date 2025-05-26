#pragma once
#include <cstdint>

class EditManager;
class Track;

class EditState {
public:
    virtual ~EditState() {}
    virtual void onEnter(EditManager& manager, Track& track, uint32_t startTick) {}
    virtual void onExit(EditManager& manager, Track& track) {}
    virtual void onEncoderTurn(EditManager& manager, Track& track, int delta) = 0;
    virtual void onButtonPress(EditManager& manager, Track& track) = 0;
    virtual const char* getName() const = 0;
};
