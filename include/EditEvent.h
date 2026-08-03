//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

enum class EditEvent {
    SessionOpened,
    SessionClosed,
    SelectionChanged,
    GeometryChanged,
    LengthModeChanged,
};

class EditEventListener {
 public:
    virtual ~EditEventListener() = default;
    virtual void onEditEvent(EditEvent event) = 0;
};
