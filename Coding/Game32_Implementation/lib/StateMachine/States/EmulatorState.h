#ifndef EMULATOR_STATE_H
#define EMULATOR_STATE_H

#include "IGameState.h"
#include <string>

#define AVR_SRAM_SIZE 0x0B00  // ATmega32u4: 2816 bytes (0x0000-0x0AFF)

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
    bool loadHexFile(const std::string& path);
    void resetCPU();
    void executeOpcode(uint16_t opcode);
    void writeIO(uint8_t io_addr, uint8_t value);
    uint8_t readIO(uint8_t io_addr);

    // Bounds-checked SRAM accessors
    inline void sramWrite(uint16_t addr, uint8_t val) {
        if (addr < AVR_SRAM_SIZE) m_sram[addr] = val;
    }
    inline uint8_t sramRead(uint16_t addr) {
        if (addr < AVR_SRAM_SIZE) return m_sram[addr];
        return 0;
    }

    uint16_t pc_trace[16];
    uint8_t trace_index = 0;

    std::string m_targetRom;
    uint8_t m_avrFlash[32768];
    size_t m_loadedBytes;
    
    // CPU State (m_isRunning placed BEFORE m_sram to prevent corruption from OOB writes)
    bool m_isRunning;
    uint16_t m_pc;
    uint8_t m_registers[32];
    uint8_t m_sreg;
    uint8_t m_sram[AVR_SRAM_SIZE];
    
    // Display State
    uint8_t m_displayBuffer[1024];
    uint16_t m_displayIndex;
};

#endif // EMULATOR_STATE_H

