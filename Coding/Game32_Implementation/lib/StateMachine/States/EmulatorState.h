#ifndef EMULATOR_STATE_H
#define EMULATOR_STATE_H

#include "IGameState.h"
#include <string>

class EmulatorState : public IGameState {
public:
    static EmulatorState& getInstance();
    EmulatorState(const EmulatorState&) = delete;
    EmulatorState& operator=(const EmulatorState&) = delete;

    void setTargetRom(const std::string& fullPath);

    void onEnter() override;
    void onUpdate() override;
    void onDraw() override;
    void onExit() override;

private:
    EmulatorState() = default;
    std::string m_targetRom;
};

#endif // EMULATOR_STATE_H