class EditState {
public:
    virtual void onEncoderTurn(int delta) = 0;
    virtual void onButtonPress() = 0;
    virtual EditState* parent() { return nullptr; }
    virtual const char* getName() const = 0;
    virtual ~EditState() {}
};

class EditNoneState : public EditState {
public:
    void onEncoderTurn(int) override {}
    void onButtonPress() override {}
    const char* getName() const override { return "None"; }
};

class EditNoteState : public EditState {
    EditState* substate = nullptr;
public:
    void onEncoderTurn(int delta) override {
        if (substate) substate->onEncoderTurn(delta);
        else {/* handle note selection */}
    }
    void onButtonPress() override {
        if (substate) substate->onButtonPress();
        else {/* enter tick or pitch substate */}
    }
    void setSubstate(EditState* s) { substate = s; }
    EditState* parent() override { return nullptr; }
    const char* getName() const override { return "Note"; }
};

class EditNoteTickState : public EditState {
    EditNoteState* parentState;
public:
    EditNoteTickState(EditNoteState* parent) : parentState(parent) {}
    void onEncoderTurn(int delta) override { /* change tick */ }
    void onButtonPress() override { parentState->setSubstate(nullptr); }
    EditState* parent() override { return parentState; }
    const char* getName() const override { return "NoteTick"; }
};

class EditNotePitchState : public EditState {
    EditNoteState* parentState;
public:
    EditNotePitchState(EditNoteState* parent) : parentState(parent) {}
    void onEncoderTurn(int delta) override { /* change pitch */ }
    void onButtonPress() override { parentState->setSubstate(nullptr); }
    EditState* parent() override { return parentState; }
    const char* getName() const override { return "NotePitch"; }
};
