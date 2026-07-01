#ifndef EMULATOR_STATE_H
#define EMULATOR_STATE_H

#include "IGameState.h"
#include "esp_attr.h"
#include <stdint.h>
#include <string>

// ATmega32u4 memory layout
#define AVR_FLASH_SIZE 32768
#define AVR_SRAM_SIZE 0x0B00 // 2816 bytes (regs + I/O + ext I/O + SRAM)
#define AVR_CACHE_SIZE 16384 // 32KB flash / 2 bytes per word

// Diagnostic limits
#define DIAG_MAX_UNHANDLED 64

// Branch prediction hints
#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

// SREG flag masks
#define FLAG_C 0x01
#define FLAG_Z 0x02
#define FLAG_N 0x04
#define FLAG_V 0x08
#define FLAG_S 0x10
#define FLAG_H 0x20
#define FLAG_T 0x40
#define FLAG_I 0x80

// ---------------------------------------------------------------------------
// OpcodeClass — one enum value per distinct instruction handler
// ---------------------------------------------------------------------------
enum OpcodeClass : uint8_t {
  OP_NOP = 0,
  // Arithmetic / Logic (two-register)
  OP_ADD, OP_ADC, OP_SUB, OP_SBC, OP_AND, OP_OR, OP_EOR,
  OP_CP, OP_CPC, OP_MUL, OP_MULS, OP_MULSU,
  OP_FMUL, OP_FMULS, OP_FMULSU,
  // Arithmetic / Logic (register-immediate)
  OP_CPI, OP_SBCI, OP_SUBI, OP_ORI, OP_ANDI, OP_LDI,
  // Unary
  OP_INC, OP_DEC, OP_COM, OP_NEG,
  OP_LSR, OP_ASR, OP_ROR, OP_SWAP,
  // Wide arithmetic
  OP_ADIW, OP_SBIW,
  // Data transfer
  OP_MOV, OP_MOVW,
  OP_LD_X, OP_LD_X_INC, OP_LD_X_DEC,
  OP_LD_Y_INC, OP_LD_Y_DEC, OP_LDD_Y_Q,
  OP_LD_Z_INC, OP_LD_Z_DEC, OP_LDD_Z_Q,
  OP_ST_X, OP_ST_X_INC, OP_ST_X_DEC,
  OP_ST_Y_INC, OP_ST_Y_DEC, OP_STD_Y_Q,
  OP_ST_Z_INC, OP_ST_Z_DEC, OP_STD_Z_Q,
  OP_LDS, OP_STS,
  OP_LPM_Z, OP_LPM_Z_INC,
  OP_PUSH, OP_POP,
  // I/O
  OP_IN, OP_OUT,
  OP_SBI, OP_CBI, OP_SBIS, OP_SBIC,
  // Branch / Call
  OP_RJMP, OP_RCALL, OP_RET, OP_RETI,
  OP_IJMP, OP_ICALL, OP_JMP, OP_CALL,
  OP_BRBS, OP_BRBC,
  OP_CPSE, OP_SBRC, OP_SBRS,
  // Bit manipulation
  OP_BSET, OP_BCLR, OP_BST, OP_BLD,
  // Misc
  OP_SLEEP, OP_WDR, OP_BREAK,
  OP_UNHANDLED,
  OP_COUNT
};

// ---------------------------------------------------------------------------
// DecodedOp — 4-byte pre-decoded instruction cache entry
// ---------------------------------------------------------------------------
struct DecodedOp {
  OpcodeClass opcode_class; // 1 byte — handler selector
  uint8_t reg;              // 1 byte — primary register / I/O addr / bit index
  uint16_t param; // 2 bytes — secondary reg / imm K / address / offset
};
static_assert(sizeof(DecodedOp) == 4, "DecodedOp must be 4 bytes");

// ---------------------------------------------------------------------------
// EmulatorState
// ---------------------------------------------------------------------------
class EmulatorState : public IGameState {
public:
  static EmulatorState &getInstance();
  EmulatorState(const EmulatorState &) = delete;
  EmulatorState &operator=(const EmulatorState &) = delete;

  void setTargetRom(const std::string &fullPath);

  void onEnter() override;
  void onUpdate() override;
  void onDraw() override;
  void onExit() override;

private:
  EmulatorState() = default;

  // Lifecycle helpers
  bool loadHexFile(const std::string &path);
  void resetCPU();
  void preDecodeRom();
  void runCpuSlice();

  // I/O trapping — now takes pointer to live sreg for sync
  void writeIO(uint8_t io_addr, uint8_t value, uint8_t *live_sreg);
  uint8_t readIO(uint8_t io_addr, const uint8_t *live_sreg);

  // Bounds-checked SRAM helpers (inlined hot-path)
  static inline void IRAM_ATTR sramStore(uint8_t *sram, uint16_t a,
                                         uint8_t v) {
    if (likely(a < AVR_SRAM_SIZE))
      sram[a] = v;
  }
  static inline uint8_t IRAM_ATTR sramLoad(const uint8_t *sram, uint16_t a) {
    return likely(a < AVR_SRAM_SIZE) ? sram[a] : 0;
  }

  // Target ROM path
  std::string m_targetRom;

  // Program memory (flash image loaded from .hex)
  uint8_t m_avrFlash[AVR_FLASH_SIZE];
  uint32_t m_loadedBytes;

  // CPU state
  bool m_isRunning;
  uint16_t m_pc;
  uint8_t m_registers[32];
  uint8_t m_sreg;
  uint8_t m_sram[AVR_SRAM_SIZE];

  // Display intercept state
  uint8_t m_displayBuffer[1024];
  uint16_t m_displayIndex;
  bool m_frameDirty;

  // Diagnostic state
  bool m_diagDone;
  uint32_t m_totalInsnsExecuted;
  int m_diagUnhandledCount;
  uint16_t m_diagUnhandledPC[DIAG_MAX_UNHANDLED];
  uint16_t m_diagUnhandledRaw[DIAG_MAX_UNHANDLED];

  // Diagnostic event counters (for tracing execution flow)
  uint32_t m_diagUpdateCalls;
  uint32_t m_diagSleepCount;
  uint32_t m_diagTimerOverflows;
  uint32_t m_diagTimerISRsFired;
  uint32_t m_diagSpiWrites;
  uint32_t m_diagAdcStarts;
  uint32_t m_diagFramesRendered;
};

#endif // EMULATOR_STATE_H