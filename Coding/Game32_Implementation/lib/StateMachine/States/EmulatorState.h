#ifndef EMULATOR_STATE_H
#define EMULATOR_STATE_H

#include "IGameState.h"
#include <string>
#include <stdint.h>
#include "esp_attr.h"

#define AVR_SRAM_SIZE 0x0B00

#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

enum OpcodeClass : uint8_t {
    OP_NOP = 0,
    OP_RJMP, OP_OUT, OP_LDI,
    OP_CPC, OP_SBC, OP_CP, OP_ADD, OP_ADC, OP_SUB, OP_AND, OP_OR, OP_EOR, OP_MOV,
    OP_CPI, OP_SBCI, OP_SUBI, OP_ORI, OP_ANDI, 
    OP_MUL, OP_MULS, OP_MULSU, OP_FMUL, OP_FMULS, OP_FMULSU,
    OP_INC, OP_DEC, OP_COM, OP_NEG, OP_MOVW,
    OP_ST_Z, OP_STD_Z_Q, OP_LDD_Z_Q, OP_LDD_Y_Q, OP_STD_Y_Q, OP_LD_Y, OP_ST_Y,
    OP_LD_Z_INC, OP_LPM_Z, OP_LPM_Z_INC, OP_PUSH, OP_POP, 
    OP_ST_Z_INC, OP_ST_Z_DEC, OP_LDS, OP_STS, OP_LD_X, OP_LD_X_INC, OP_LD_X_DEC,
    OP_ST_X, OP_ST_X_INC, OP_ST_X_DEC, OP_LD_Y_INC, OP_LD_Y_DEC, OP_ST_Y_INC, OP_ST_Y_DEC,
    OP_CPSE, OP_JMP, OP_CALL, OP_RET, OP_RETI, OP_RCALL, OP_SBRC, OP_SBRS, OP_BRBS, OP_BRBC,
    OP_SBI, OP_CBI, OP_SBIS, OP_SBIC, OP_BSET, OP_BCLR, OP_ADIW, OP_SBIW, OP_LSR, OP_ASR, OP_ROR, OP_SWAP, OP_IN, OP_BST, OP_BLD, OP_SLEEP, OP_SPM,
    OP_UNHANDLED
};

struct DecodedOp {
    union {
        struct { uint8_t d, r; };
        struct { uint16_t addr_or_k; };
        struct { int16_t offset; };
        struct { uint8_t A, b; }; 
    } operands;
    OpcodeClass opcode_class; // 1 byte
    uint8_t length : 2;       // 2 bits (max value 3)
    uint8_t cycles : 6;       // 6 bits (max value 63)
};

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
    void preDecodeRom();
    void runCpuSlice();
    void writeIO(uint8_t io_addr, uint8_t value);
    uint8_t readIO(uint8_t io_addr);

    static inline void IRAM_ATTR sramWrite(uint8_t* sram, uint16_t addr, uint8_t val) {
        if (likely(addr < AVR_SRAM_SIZE)) sram[addr] = val;
    }
    static inline uint8_t IRAM_ATTR sramRead(uint8_t* sram, uint16_t addr) {
        if (likely(addr < AVR_SRAM_SIZE)) return sram[addr];
        return 0;
    }

    uint16_t pc_trace[16];
    uint8_t trace_index = 0;

    std::string m_targetRom;
    uint8_t m_avrFlash[32768];
    size_t m_loadedBytes;
    
    uint32_t m_cycleAccumulator = 0;
    
    bool m_isRunning;
    uint16_t m_pc;
    uint8_t m_registers[32];
    uint8_t m_sreg;
    uint8_t m_sram[AVR_SRAM_SIZE];
    
    uint8_t m_displayBuffer[1024];
    uint16_t m_displayIndex;
    bool m_frameDirty = false;
};

#endif // EMULATOR_STATE_H