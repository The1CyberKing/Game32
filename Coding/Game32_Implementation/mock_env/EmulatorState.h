#pragma once
#include "IGameState.h"
#include <string>
#include <cstdint>
namespace StateMachine {
    class EmulatorState : public State {
    public:
        static EmulatorState& getInstance() { static EmulatorState i; return i; }
        EmulatorState() {}
        void setTargetRom(const std::string& p) { m_targetRom = p; }
        void onEnter();
        void onUpdate();
        void onDraw();
        void onExit();
        uint16_t getPC() { return m_pc; }
    //private:
        std::string m_targetRom;
        bool m_isRunning;
        uint8_t m_avrFlash[65536];
        uint8_t m_sram[2560];
        uint8_t m_registers[32];
        uint16_t m_pc;
        uint8_t m_sreg;
        size_t m_loadedBytes;
        uint8_t m_displayBuffer[1024];
        int m_displayIndex;
        bool loadHexFile(const std::string& path);
        void resetCPU();
        void executeOpcode(uint16_t opcode);
        uint8_t readIO(uint8_t io_addr);
        void writeIO(uint8_t io_addr, uint8_t value);
    };
}
