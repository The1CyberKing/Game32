// ===========================================================================
// EmulatorState.cpp — Pre-Decoded Cached AVR Interpreter with Computed Gotos
// ===========================================================================
#include "EmulatorState.h"
#include "DisplayManager.h"
#include "InputManager.h"
#include "MenuState.h"
#include "StateManager.h"
#include "types.h"

#include <cstdio>
#include <cstring>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "EmulatorState";

// ===========================================================================
// Static pre-decoded instruction cache — lives in .bss (zero-initialized)
// 16384 entries × 4 bytes = 64 KB
// ===========================================================================
static DecodedOp s_cache[AVR_CACHE_SIZE];

// Dispatch table — filled once on first runCpuSlice() call
static void *s_dispatch[OP_COUNT];
static bool s_dispatchReady = false;

// ===========================================================================
// Diagnostic: opcode name table for debug printing
// ===========================================================================
static const char *s_opcodeNames[] __attribute__((used)) = {
    "NOP",     "ADD",     "ADC",     "SUB",     "SBC",      "AND",
    "OR",      "EOR",     "CP",      "CPC",     "MUL",      "MULS",
    "MULSU",   "FMUL",    "FMULS",   "FMULSU",  "CPI",      "SBCI",
    "SUBI",    "ORI",     "ANDI",    "LDI",     "INC",      "DEC",
    "COM",     "NEG",     "LSR",     "ASR",     "ROR",      "SWAP",
    "ADIW",    "SBIW",    "MOV",     "MOVW",    "LD_X",     "LD_X_INC",
    "LD_X_DEC","LD_Y_INC","LD_Y_DEC","LDD_Y_Q", "LD_Z_INC", "LD_Z_DEC",
    "LDD_Z_Q", "ST_X",   "ST_X_INC","ST_X_DEC","ST_Y_INC", "ST_Y_DEC",
    "STD_Y_Q", "ST_Z_INC","ST_Z_DEC","STD_Z_Q", "LDS",      "STS",
    "LPM_Z",  "LPM_Z_INC","PUSH",   "POP",     "IN",       "OUT",
    "SBI",     "CBI",     "SBIS",    "SBIC",    "RJMP",     "RCALL",
    "RET",     "RETI",    "IJMP",    "ICALL",   "JMP",      "CALL",
    "BRBS",    "BRBC",    "CPSE",    "SBRC",    "SBRS",     "BSET",
    "BCLR",    "BST",     "BLD",     "SLEEP",   "WDR",      "BREAK",
    "UNHANDLED"
};

// ===========================================================================
// Helper: check if an opcode class is a 2-word instruction
// ===========================================================================
static inline bool is_two_word(OpcodeClass c) {
  return c == OP_JMP || c == OP_CALL || c == OP_LDS || c == OP_STS;
}

// ===========================================================================
// Flag update helpers (inline, IRAM)
// ===========================================================================
static inline void IRAM_ATTR flags_arith(uint8_t &sreg, uint8_t rd, uint8_t rr,
                                         uint8_t res, bool isSub,
                                         bool carryIn) {
  uint8_t hc;
  if (isSub)
    hc = (~rd & rr) | (rr & res) | (res & ~rd);
  else
    hc = (rd & rr) | (rr & ~res) | (~res & rd);
  bool H = (hc >> 3) & 1;

  uint8_t ov;
  if (isSub)
    ov = (rd & ~rr & ~res) | (~rd & rr & res);
  else
    ov = (rd & rr & ~res) | (~rd & ~rr & res);
  bool V = (ov >> 7) & 1;

  bool N = (res >> 7) & 1;
  bool S = N ^ V;
  bool Z = (res == 0);

  uint8_t cc;
  if (isSub)
    cc = (~rd & rr) | (rr & res) | (res & ~rd);
  else
    cc = (rd & rr) | (rr & ~res) | (~res & rd);
  bool C = (cc >> 7) & 1;

  sreg = (sreg & FLAG_T) | (sreg & FLAG_I) | (H ? FLAG_H : 0) |
         (S ? FLAG_S : 0) | (V ? FLAG_V : 0) | (N ? FLAG_N : 0) |
         (Z ? FLAG_Z : 0) | (C ? FLAG_C : 0);
}

static inline void IRAM_ATTR flags_sbc(uint8_t &sreg, uint8_t rd, uint8_t rr,
                                       uint8_t res) {
  bool prevZ = (sreg & FLAG_Z) != 0;
  flags_arith(sreg, rd, rr, res, true, true);
  if (res != 0)
    sreg &= ~FLAG_Z;
  else if (!prevZ)
    sreg &= ~FLAG_Z;
}

static inline void IRAM_ATTR flags_logical(uint8_t &sreg, uint8_t res) {
  bool N = (res >> 7) & 1;
  sreg = (sreg & (FLAG_T | FLAG_I | FLAG_H | FLAG_C)) |
         (N ? (FLAG_N | FLAG_S) : 0) | ((res == 0) ? FLAG_Z : 0);
}

// ===========================================================================
// Singleton
// ===========================================================================
EmulatorState &EmulatorState::getInstance() {
  static EmulatorState instance;
  return instance;
}

void EmulatorState::setTargetRom(const std::string &fullPath) {
  m_targetRom = fullPath;
}

// ===========================================================================
// Intel HEX loader
// ===========================================================================
static uint8_t hexByte(const char *p) {
  uint8_t v = 0;
  for (int i = 0; i < 2; i++) {
    v <<= 4;
    char c = p[i];
    if (c >= '0' && c <= '9')
      v |= c - '0';
    else if (c >= 'A' && c <= 'F')
      v |= c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
      v |= c - 'a' + 10;
  }
  return v;
}

bool EmulatorState::loadHexFile(const std::string &path) {
  FILE *f = fopen(path.c_str(), "r");
  if (!f) {
    ESP_LOGE(TAG, "Cannot open %s", path.c_str());
    return false;
  }

  memset(m_avrFlash, 0xFF, sizeof(m_avrFlash));
  m_loadedBytes = 0;
  char line[128];

  while (fgets(line, sizeof(line), f)) {
    if (line[0] != ':')
      continue;
    uint8_t byteCount = hexByte(line + 1);
    uint16_t address = (hexByte(line + 3) << 8) | hexByte(line + 5);
    uint8_t recordType = hexByte(line + 7);

    if (recordType == 0x01)
      break;
    if (recordType != 0x00)
      continue;

    if (address + byteCount > AVR_FLASH_SIZE) {
      ESP_LOGE(TAG, "HEX address 0x%04X + %d out of range", address,
               byteCount);
      fclose(f);
      return false;
    }

    const char *dp = line + 9;
    for (uint8_t i = 0; i < byteCount; i++) {
      m_avrFlash[address + i] = hexByte(dp);
      dp += 2;
    }
    if (address + byteCount > m_loadedBytes)
      m_loadedBytes = address + byteCount;
  }
  fclose(f);
  ESP_LOGI(TAG, "Loaded %lu bytes from HEX", (unsigned long)m_loadedBytes);
  return true;
}

// ===========================================================================
// CPU reset
// ===========================================================================
void EmulatorState::resetCPU() {
  memset(m_registers, 0, sizeof(m_registers));
  memset(m_sram, 0, sizeof(m_sram));
  m_pc = 0;
  m_sreg = 0;
  m_isRunning = true;
  m_diagDone = false;
  m_diagUnhandledCount = 0;
  m_totalInsnsExecuted = 0;
  m_diagUpdateCalls = 0;
  m_diagSleepCount = 0;
  m_diagTimerOverflows = 0;
  m_diagTimerISRsFired = 0;
  m_diagSpiWrites = 0;
  m_diagAdcStarts = 0;
  m_diagFramesRendered = 0;

  // SP default for ATmega32u4 = RAMEND = 0x0AFF
  sramStore(m_sram, 0x5D, 0xFF); // SPL
  sramStore(m_sram, 0x5E, 0x0A); // SPH

  // Display state
  memset(m_displayBuffer, 0, sizeof(m_displayBuffer));
  m_displayIndex = 0;
  m_frameDirty = false;
}

// ===========================================================================
// Pre-decoder — called once after loading the .hex
// ===========================================================================
void EmulatorState::preDecodeRom() {
  memset(s_cache, 0, sizeof(s_cache));
  uint16_t maxPC = m_loadedBytes / 2;

  for (uint16_t pc = 0; pc < maxPC; pc++) {
    uint16_t w = m_avrFlash[pc * 2] | (m_avrFlash[pc * 2 + 1] << 8);
    DecodedOp &op = s_cache[pc];
    op.opcode_class = OP_UNHANDLED;
    op.reg = 0;
    op.param = 0;

#define RD5 ((w >> 4) & 0x1F)
#define RR5 ((w & 0x0F) | ((w >> 5) & 0x10))
#define IMM8 (((w >> 4) & 0xF0) | (w & 0x0F))
#define RD4 (((w >> 4) & 0x0F) + 16)
#define IO6 (((w >> 5) & 0x30) | (w & 0x0F))

    // === Exact matches ===
    if (w == 0x0000) { op.opcode_class = OP_NOP; continue; }
    if (w == 0x9508) { op.opcode_class = OP_RET; continue; }
    if (w == 0x9518) { op.opcode_class = OP_RETI; continue; }
    if (w == 0x9588) { op.opcode_class = OP_SLEEP; continue; }
    if (w == 0x9409) { op.opcode_class = OP_IJMP; continue; }
    if (w == 0x9509) { op.opcode_class = OP_ICALL; continue; }
    if (w == 0x95A8) { op.opcode_class = OP_WDR; continue; }
    if (w == 0x9598) { op.opcode_class = OP_BREAK; continue; }
    if (w == 0x95C8) { op.opcode_class = OP_LPM_Z; op.reg = 0; continue; }

    // === MOVW ===
    if ((w & 0xFF00) == 0x0100) {
      op.opcode_class = OP_MOVW;
      op.reg = ((w >> 4) & 0x0F) * 2;
      op.param = (w & 0x0F) * 2;
      continue;
    }
    // === MULS ===
    if ((w & 0xFF00) == 0x0200) {
      op.opcode_class = OP_MULS;
      op.reg = ((w >> 4) & 0x0F) + 16;
      op.param = (w & 0x0F) + 16;
      continue;
    }
    // === MULSU / FMUL / FMULS / FMULSU ===
    if ((w & 0xFF00) == 0x0300) {
      uint8_t d = ((w >> 4) & 0x07) + 16;
      uint8_t r = (w & 0x07) + 16;
      switch (w & 0x0088) {
      case 0x0000: op.opcode_class = OP_MULSU; break;
      case 0x0008: op.opcode_class = OP_FMUL; break;
      case 0x0080: op.opcode_class = OP_FMULS; break;
      case 0x0088: op.opcode_class = OP_FMULSU; break;
      }
      op.reg = d; op.param = r;
      continue;
    }

    // === BSET / BCLR ===
    if ((w & 0xFF8F) == 0x9408) {
      op.opcode_class = OP_BSET;
      op.reg = (w >> 4) & 0x07;
      continue;
    }
    if ((w & 0xFF8F) == 0x9488) {
      op.opcode_class = OP_BCLR;
      op.reg = (w >> 4) & 0x07;
      continue;
    }

    // === ADIW / SBIW ===
    if ((w & 0xFF00) == 0x9600) {
      op.opcode_class = OP_ADIW;
      op.reg = 24 + 2 * ((w >> 4) & 0x03);
      op.param = ((w >> 2) & 0x30) | (w & 0x0F);
      continue;
    }
    if ((w & 0xFF00) == 0x9700) {
      op.opcode_class = OP_SBIW;
      op.reg = 24 + 2 * ((w >> 4) & 0x03);
      op.param = ((w >> 2) & 0x30) | (w & 0x0F);
      continue;
    }

    // === CBI / SBIC / SBI / SBIS ===
    if ((w & 0xFF00) == 0x9800) { op.opcode_class = OP_CBI; op.reg = (w >> 3) & 0x1F; op.param = w & 0x07; continue; }
    if ((w & 0xFF00) == 0x9900) { op.opcode_class = OP_SBIC; op.reg = (w >> 3) & 0x1F; op.param = w & 0x07; continue; }
    if ((w & 0xFF00) == 0x9A00) { op.opcode_class = OP_SBI; op.reg = (w >> 3) & 0x1F; op.param = w & 0x07; continue; }
    if ((w & 0xFF00) == 0x9B00) { op.opcode_class = OP_SBIS; op.reg = (w >> 3) & 0x1F; op.param = w & 0x07; continue; }

    // === Load/Store group (1001 000d / 1001 001r) ===
    if ((w & 0xFE0F) == 0x9000) {
      uint16_t nw = m_avrFlash[(pc + 1) * 2] | (m_avrFlash[(pc + 1) * 2 + 1] << 8);
      op.opcode_class = OP_LDS; op.reg = RD5; op.param = nw;
      s_cache[pc + 1].opcode_class = OP_NOP; // mark data word
      pc++; // skip data word in pre-decode loop
      continue;
    }
    if ((w & 0xFE0F) == 0x9001) { op.opcode_class = OP_LD_Z_INC; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x9002) { op.opcode_class = OP_LD_Z_DEC; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x9004) { op.opcode_class = OP_LPM_Z; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x9005) { op.opcode_class = OP_LPM_Z_INC; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x9009) { op.opcode_class = OP_LD_Y_INC; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x900A) { op.opcode_class = OP_LD_Y_DEC; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x900C) { op.opcode_class = OP_LD_X; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x900D) { op.opcode_class = OP_LD_X_INC; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x900E) { op.opcode_class = OP_LD_X_DEC; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x900F) { op.opcode_class = OP_POP; op.reg = RD5; continue; }

    if ((w & 0xFE0F) == 0x9200) {
      uint16_t nw = m_avrFlash[(pc + 1) * 2] | (m_avrFlash[(pc + 1) * 2 + 1] << 8);
      op.opcode_class = OP_STS; op.reg = RD5; op.param = nw;
      s_cache[pc + 1].opcode_class = OP_NOP;
      pc++;
      continue;
    }
    if ((w & 0xFE0F) == 0x9201) { op.opcode_class = OP_ST_Z_INC; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x9202) { op.opcode_class = OP_ST_Z_DEC; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x9209) { op.opcode_class = OP_ST_Y_INC; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x920A) { op.opcode_class = OP_ST_Y_DEC; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x920C) { op.opcode_class = OP_ST_X; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x920D) { op.opcode_class = OP_ST_X_INC; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x920E) { op.opcode_class = OP_ST_X_DEC; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x920F) { op.opcode_class = OP_PUSH; op.reg = RD5; continue; }

    // === Unary ops ===
    if ((w & 0xFE0F) == 0x9400) { op.opcode_class = OP_COM; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x9401) { op.opcode_class = OP_NEG; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x9402) { op.opcode_class = OP_SWAP; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x9403) { op.opcode_class = OP_INC; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x9405) { op.opcode_class = OP_ASR; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x9406) { op.opcode_class = OP_LSR; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x9407) { op.opcode_class = OP_ROR; op.reg = RD5; continue; }
    if ((w & 0xFE0F) == 0x940A) { op.opcode_class = OP_DEC; op.reg = RD5; continue; }

    // === JMP / CALL (32-bit) ===
    if ((w & 0xFE0E) == 0x940C) {
      uint16_t nw = m_avrFlash[(pc + 1) * 2] | (m_avrFlash[(pc + 1) * 2 + 1] << 8);
      op.opcode_class = OP_JMP; op.param = nw;
      s_cache[pc + 1].opcode_class = OP_NOP;
      pc++;
      continue;
    }
    if ((w & 0xFE0E) == 0x940E) {
      uint16_t nw = m_avrFlash[(pc + 1) * 2] | (m_avrFlash[(pc + 1) * 2 + 1] << 8);
      op.opcode_class = OP_CALL; op.param = nw;
      s_cache[pc + 1].opcode_class = OP_NOP;
      pc++;
      continue;
    }

    // === MUL (unsigned) ===
    if ((w & 0xFC00) == 0x9C00) { op.opcode_class = OP_MUL; op.reg = RD5; op.param = RR5; continue; }

    // === Two-register ops ===
    if ((w & 0xFC00) == 0x0400) { op.opcode_class = OP_CPC; op.reg = RD5; op.param = RR5; continue; }
    if ((w & 0xFC00) == 0x0800) { op.opcode_class = OP_SBC; op.reg = RD5; op.param = RR5; continue; }
    if ((w & 0xFC00) == 0x0C00) { op.opcode_class = OP_ADD; op.reg = RD5; op.param = RR5; continue; }
    if ((w & 0xFC00) == 0x1000) { op.opcode_class = OP_CPSE; op.reg = RD5; op.param = RR5; continue; }
    if ((w & 0xFC00) == 0x1400) { op.opcode_class = OP_CP; op.reg = RD5; op.param = RR5; continue; }
    if ((w & 0xFC00) == 0x1800) { op.opcode_class = OP_SUB; op.reg = RD5; op.param = RR5; continue; }
    if ((w & 0xFC00) == 0x1C00) { op.opcode_class = OP_ADC; op.reg = RD5; op.param = RR5; continue; }
    if ((w & 0xFC00) == 0x2000) { op.opcode_class = OP_AND; op.reg = RD5; op.param = RR5; continue; }
    if ((w & 0xFC00) == 0x2400) { op.opcode_class = OP_EOR; op.reg = RD5; op.param = RR5; continue; }
    if ((w & 0xFC00) == 0x2800) { op.opcode_class = OP_OR; op.reg = RD5; op.param = RR5; continue; }
    if ((w & 0xFC00) == 0x2C00) { op.opcode_class = OP_MOV; op.reg = RD5; op.param = RR5; continue; }

    // === Register-immediate ops ===
    if ((w & 0xF000) == 0x3000) { op.opcode_class = OP_CPI; op.reg = RD4; op.param = IMM8; continue; }
    if ((w & 0xF000) == 0x4000) { op.opcode_class = OP_SBCI; op.reg = RD4; op.param = IMM8; continue; }
    if ((w & 0xF000) == 0x5000) { op.opcode_class = OP_SUBI; op.reg = RD4; op.param = IMM8; continue; }
    if ((w & 0xF000) == 0x6000) { op.opcode_class = OP_ORI; op.reg = RD4; op.param = IMM8; continue; }
    if ((w & 0xF000) == 0x7000) { op.opcode_class = OP_ANDI; op.reg = RD4; op.param = IMM8; continue; }
    if ((w & 0xF000) == 0xE000) { op.opcode_class = OP_LDI; op.reg = RD4; op.param = IMM8; continue; }

    // === LDD / STD (Y+q, Z+q) ===
    if ((w & 0xD000) == 0x8000) {
      uint8_t q = ((w & 0x2000) >> 8) | ((w & 0x0C00) >> 7) | (w & 0x07);
      uint8_t r = (w >> 4) & 0x1F;
      bool isStore = (w & 0x0200) != 0;
      bool isY = (w & 0x0008) != 0;
      if (isStore)
        op.opcode_class = isY ? OP_STD_Y_Q : OP_STD_Z_Q;
      else
        op.opcode_class = isY ? OP_LDD_Y_Q : OP_LDD_Z_Q;
      op.reg = r;
      op.param = q;
      continue;
    }

    // === IN / OUT ===
    if ((w & 0xF800) == 0xB000) { op.opcode_class = OP_IN; op.reg = RD5; op.param = IO6; continue; }
    if ((w & 0xF800) == 0xB800) { op.opcode_class = OP_OUT; op.reg = RD5; op.param = IO6; continue; }

    // === RJMP / RCALL ===
    if ((w & 0xF000) == 0xC000) {
      int16_t k = w & 0x0FFF;
      if (k & 0x0800) k |= 0xF000;
      op.opcode_class = OP_RJMP;
      op.param = (uint16_t)k;
      continue;
    }
    if ((w & 0xF000) == 0xD000) {
      int16_t k = w & 0x0FFF;
      if (k & 0x0800) k |= 0xF000;
      op.opcode_class = OP_RCALL;
      op.param = (uint16_t)k;
      continue;
    }

    // === BRBS / BRBC ===
    if ((w & 0xFC00) == 0xF000) {
      int16_t k = (w >> 3) & 0x7F;
      if (k & 0x40) k |= 0xFF80;
      op.opcode_class = OP_BRBS;
      op.reg = w & 0x07;
      op.param = (uint16_t)k;
      continue;
    }
    if ((w & 0xFC00) == 0xF400) {
      int16_t k = (w >> 3) & 0x7F;
      if (k & 0x40) k |= 0xFF80;
      op.opcode_class = OP_BRBC;
      op.reg = w & 0x07;
      op.param = (uint16_t)k;
      continue;
    }

    // === BLD / BST / SBRC / SBRS ===
    if ((w & 0xFE08) == 0xF800) { op.opcode_class = OP_BLD; op.reg = RD5; op.param = w & 0x07; continue; }
    if ((w & 0xFE08) == 0xFA00) { op.opcode_class = OP_BST; op.reg = RD5; op.param = w & 0x07; continue; }
    if ((w & 0xFE08) == 0xFC00) { op.opcode_class = OP_SBRC; op.reg = RD5; op.param = w & 0x07; continue; }
    if ((w & 0xFE08) == 0xFE00) { op.opcode_class = OP_SBRS; op.reg = RD5; op.param = w & 0x07; continue; }

    // If we get here, it's unhandled

#undef RD5
#undef RR5
#undef IMM8
#undef RD4
#undef IO6
  }
  ESP_LOGI(TAG, "Pre-decoded %u instruction words", maxPC);
}

// ===========================================================================
// I/O Write — intercepts SPI display output, SREG, and TIFR w1c
//
// BUG FIX: SREG writes now go through a pointer to the LIVE sreg variable
// inside runCpuSlice(), not to the stale m_sreg member.
// We achieve this by passing sreg as a parameter.
// ===========================================================================
void IRAM_ATTR EmulatorState::writeIO(uint8_t io_addr, uint8_t value,
                                      uint8_t *live_sreg) {
  uint16_t sram_addr = (uint16_t)io_addr + 0x20;

  // Fast path: SREG → write directly to the live local variable
  if (unlikely(io_addr == 0x3F)) {
    if (live_sreg)
      *live_sreg = value;
    m_sreg = value; // keep member in sync too
    return;
  }

  // TIFR0 (0x15) — AVR "write-1-to-clear" behavior
  // Writing 1 to a bit CLEARS it on real hardware.
  if (unlikely(io_addr == 0x15)) {
    m_sram[sram_addr] &= ~value; // clear bits that have 1 written
    return;
  }
  // TIFR1 (0x16) — same w1c behavior
  if (unlikely(io_addr == 0x16)) {
    m_sram[sram_addr] &= ~value;
    return;
  }

  // Store value in SRAM
  sramStore(m_sram, sram_addr, value);

  // SPI Data Register (SPDR) write — display pipeline
  if (unlikely(io_addr == 0x2E)) {
    m_sram[0x2D + 0x20] |= 0x80; // Set SPIF in SPSR
    m_diagSpiWrites++;

    // Check PORTD bit 4: high = data mode, low = command mode
    bool isData = (m_sram[0x0B + 0x20] & 0x10) != 0;
    if (isData && m_displayIndex < 1024) {
      m_displayBuffer[m_displayIndex++] = value;
      if (m_displayIndex >= 1024) {
        m_frameDirty = true;
        m_displayIndex = 0;
        m_diagFramesRendered++;
      }
    }
  }
}

// ===========================================================================
// I/O Read — maps buttons to Arduboy GPIO pin registers
//
// BUG FIX: SREG reads now return the LIVE sreg from the dispatch loop.
// ===========================================================================
uint8_t IRAM_ATTR EmulatorState::readIO(uint8_t io_addr,
                                        const uint8_t *live_sreg) {
  // SREG fast path — return the live local, not the stale member
  if (unlikely(io_addr == 0x3F))
    return live_sreg ? *live_sreg : m_sreg;

  // SPSR — always signal SPI complete
  if (unlikely(io_addr == 0x2D))
    return sramLoad(m_sram, 0x2D + 0x20) | 0x80;

  // PINF (0x0F) — D-Pad (active low)
  if (unlikely(io_addr == 0x0F)) {
    uint8_t val = 0xFF;
    auto &inp = InputManager::getInstance();
    if (inp.isHeld(0)) val &= ~(1 << 7); // UP
    if (inp.isHeld(1)) val &= ~(1 << 4); // DOWN
    if (inp.isHeld(2)) val &= ~(1 << 5); // LEFT
    if (inp.isHeld(3)) val &= ~(1 << 6); // RIGHT
    return val;
  }
  // PINE (0x0C) — A button
  if (unlikely(io_addr == 0x0C)) {
    uint8_t val = 0xFF;
    if (InputManager::getInstance().isHeld(4))
      val &= ~(1 << 6);
    return val;
  }
  // PINB (0x03) — B button
  if (unlikely(io_addr == 0x03)) {
    uint8_t val = 0xFF;
    if (InputManager::getInstance().isHeld(5))
      val &= ~(1 << 4);
    return val;
  }

  return sramLoad(m_sram, (uint16_t)io_addr + 0x20);
}

// ===========================================================================
// runCpuSlice — the hot-path computed-goto dispatch loop
//
// FIXES APPLIED:
// 1. SREG sync: readIO/writeIO now receive pointer to local `sreg`
// 2. Watchdog: yield every ~4000 instructions via mini-slices
// 3. Diagnostic: first run collects unhandled opcodes into array
// ===========================================================================
void IRAM_ATTR EmulatorState::runCpuSlice() {
  // --- Local aliases for register-allocation ---
  uint8_t *regs = m_registers;
  uint8_t *sram = m_sram;
  uint8_t sreg = m_sreg;
  uint16_t pc = m_pc;
  const DecodedOp *op;

  // --- Build dispatch table on first call ---
  if (unlikely(!s_dispatchReady)) {
    for (int i = 0; i < OP_COUNT; i++)
      s_dispatch[i] = &&L_UNHANDLED;
    s_dispatch[OP_NOP] = &&L_NOP;
    s_dispatch[OP_ADD] = &&L_ADD;
    s_dispatch[OP_ADC] = &&L_ADC;
    s_dispatch[OP_SUB] = &&L_SUB;
    s_dispatch[OP_SBC] = &&L_SBC;
    s_dispatch[OP_AND] = &&L_AND;
    s_dispatch[OP_OR] = &&L_OR;
    s_dispatch[OP_EOR] = &&L_EOR;
    s_dispatch[OP_CP] = &&L_CP;
    s_dispatch[OP_CPC] = &&L_CPC;
    s_dispatch[OP_CPI] = &&L_CPI;
    s_dispatch[OP_SBCI] = &&L_SBCI;
    s_dispatch[OP_SUBI] = &&L_SUBI;
    s_dispatch[OP_ORI] = &&L_ORI;
    s_dispatch[OP_ANDI] = &&L_ANDI;
    s_dispatch[OP_LDI] = &&L_LDI;
    s_dispatch[OP_INC] = &&L_INC;
    s_dispatch[OP_DEC] = &&L_DEC;
    s_dispatch[OP_COM] = &&L_COM;
    s_dispatch[OP_NEG] = &&L_NEG;
    s_dispatch[OP_LSR] = &&L_LSR;
    s_dispatch[OP_ASR] = &&L_ASR;
    s_dispatch[OP_ROR] = &&L_ROR;
    s_dispatch[OP_SWAP] = &&L_SWAP;
    s_dispatch[OP_MUL] = &&L_MUL;
    s_dispatch[OP_MULS] = &&L_MULS;
    s_dispatch[OP_MULSU] = &&L_MULSU;
    s_dispatch[OP_FMUL] = &&L_FMUL;
    s_dispatch[OP_FMULS] = &&L_FMULS;
    s_dispatch[OP_FMULSU] = &&L_FMULSU;
    s_dispatch[OP_ADIW] = &&L_ADIW;
    s_dispatch[OP_SBIW] = &&L_SBIW;
    s_dispatch[OP_MOV] = &&L_MOV;
    s_dispatch[OP_MOVW] = &&L_MOVW;
    s_dispatch[OP_LD_X] = &&L_LD_X;
    s_dispatch[OP_LD_X_INC] = &&L_LD_X_INC;
    s_dispatch[OP_LD_X_DEC] = &&L_LD_X_DEC;
    s_dispatch[OP_LD_Y_INC] = &&L_LD_Y_INC;
    s_dispatch[OP_LD_Y_DEC] = &&L_LD_Y_DEC;
    s_dispatch[OP_LDD_Y_Q] = &&L_LDD_Y_Q;
    s_dispatch[OP_LD_Z_INC] = &&L_LD_Z_INC;
    s_dispatch[OP_LD_Z_DEC] = &&L_LD_Z_DEC;
    s_dispatch[OP_LDD_Z_Q] = &&L_LDD_Z_Q;
    s_dispatch[OP_ST_X] = &&L_ST_X;
    s_dispatch[OP_ST_X_INC] = &&L_ST_X_INC;
    s_dispatch[OP_ST_X_DEC] = &&L_ST_X_DEC;
    s_dispatch[OP_ST_Y_INC] = &&L_ST_Y_INC;
    s_dispatch[OP_ST_Y_DEC] = &&L_ST_Y_DEC;
    s_dispatch[OP_STD_Y_Q] = &&L_STD_Y_Q;
    s_dispatch[OP_ST_Z_INC] = &&L_ST_Z_INC;
    s_dispatch[OP_ST_Z_DEC] = &&L_ST_Z_DEC;
    s_dispatch[OP_STD_Z_Q] = &&L_STD_Z_Q;
    s_dispatch[OP_LDS] = &&L_LDS;
    s_dispatch[OP_STS] = &&L_STS;
    s_dispatch[OP_LPM_Z] = &&L_LPM_Z;
    s_dispatch[OP_LPM_Z_INC] = &&L_LPM_Z_INC;
    s_dispatch[OP_PUSH] = &&L_PUSH;
    s_dispatch[OP_POP] = &&L_POP;
    s_dispatch[OP_IN] = &&L_IN;
    s_dispatch[OP_OUT] = &&L_OUT;
    s_dispatch[OP_SBI] = &&L_SBI;
    s_dispatch[OP_CBI] = &&L_CBI;
    s_dispatch[OP_SBIS] = &&L_SBIS;
    s_dispatch[OP_SBIC] = &&L_SBIC;
    s_dispatch[OP_RJMP] = &&L_RJMP;
    s_dispatch[OP_RCALL] = &&L_RCALL;
    s_dispatch[OP_RET] = &&L_RET;
    s_dispatch[OP_RETI] = &&L_RETI;
    s_dispatch[OP_IJMP] = &&L_IJMP;
    s_dispatch[OP_ICALL] = &&L_ICALL;
    s_dispatch[OP_JMP] = &&L_JMP;
    s_dispatch[OP_CALL] = &&L_CALL;
    s_dispatch[OP_BRBS] = &&L_BRBS;
    s_dispatch[OP_BRBC] = &&L_BRBC;
    s_dispatch[OP_CPSE] = &&L_CPSE;
    s_dispatch[OP_SBRC] = &&L_SBRC;
    s_dispatch[OP_SBRS] = &&L_SBRS;
    s_dispatch[OP_BSET] = &&L_BSET;
    s_dispatch[OP_BCLR] = &&L_BCLR;
    s_dispatch[OP_BST] = &&L_BST;
    s_dispatch[OP_BLD] = &&L_BLD;
    s_dispatch[OP_SLEEP] = &&L_SLEEP;
    s_dispatch[OP_WDR] = &&L_NOP;
    s_dispatch[OP_BREAK] = &&L_NOP;
    s_dispatchReady = true;
  }

  // --- Dispatch macro ---
  // Budget = ~266K instructions, split into mini-slices of 4096
  // to let us yield to the WDT between slices.
  int budget = 266666;
  int timerTicks = 64;
  int sliceBudget = 4096; // yield to WDT every 4096 instructions

#define NEXT()                                                                 \
  do {                                                                         \
    if (unlikely(--budget <= 0))                                               \
      goto L_EXIT;                                                             \
    if (unlikely(--sliceBudget <= 0))                                          \
      goto L_YIELD;                                                            \
    if (unlikely(--timerTicks <= 0))                                           \
      goto L_TIMER;                                                            \
    op = &s_cache[pc++];                                                       \
    goto *s_dispatch[op->opcode_class];                                        \
  } while (0)

  // --- Enter the loop ---
  NEXT();

  // ===================================================================
  // ARITHMETIC / LOGIC — TWO REGISTER
  // ===================================================================
  L_ADD : {
    uint8_t rd = regs[op->reg], rr = regs[(uint8_t)op->param];
    uint8_t res = rd + rr;
    flags_arith(sreg, rd, rr, res, false, false);
    regs[op->reg] = res;
    NEXT();
  }
  L_ADC : {
    uint8_t rd = regs[op->reg], rr = regs[(uint8_t)op->param];
    uint8_t c = sreg & FLAG_C;
    uint8_t res = rd + rr + c;
    flags_arith(sreg, rd, rr, res, false, c);
    regs[op->reg] = res;
    NEXT();
  }
  L_SUB : {
    uint8_t rd = regs[op->reg], rr = regs[(uint8_t)op->param];
    uint8_t res = rd - rr;
    flags_arith(sreg, rd, rr, res, true, false);
    regs[op->reg] = res;
    NEXT();
  }
  L_SBC : {
    uint8_t rd = regs[op->reg], rr = regs[(uint8_t)op->param];
    uint8_t c = sreg & FLAG_C;
    uint8_t res = rd - rr - c;
    flags_sbc(sreg, rd, rr, res);
    regs[op->reg] = res;
    NEXT();
  }
  L_CP : {
    uint8_t rd = regs[op->reg], rr = regs[(uint8_t)op->param];
    uint8_t res = rd - rr;
    flags_arith(sreg, rd, rr, res, true, false);
    NEXT();
  }
  L_CPC : {
    uint8_t rd = regs[op->reg], rr = regs[(uint8_t)op->param];
    uint8_t c = sreg & FLAG_C;
    uint8_t res = rd - rr - c;
    flags_sbc(sreg, rd, rr, res);
    NEXT();
  }
  L_AND : {
    uint8_t res = regs[op->reg] & regs[(uint8_t)op->param];
    flags_logical(sreg, res);
    regs[op->reg] = res;
    NEXT();
  }
  L_OR : {
    uint8_t res = regs[op->reg] | regs[(uint8_t)op->param];
    flags_logical(sreg, res);
    regs[op->reg] = res;
    NEXT();
  }
  L_EOR : {
    uint8_t res = regs[op->reg] ^ regs[(uint8_t)op->param];
    flags_logical(sreg, res);
    regs[op->reg] = res;
    NEXT();
  }
  L_MOV : {
    regs[op->reg] = regs[(uint8_t)op->param];
    NEXT();
  }
  L_MOVW : {
    uint8_t d = op->reg, r = (uint8_t)op->param;
    regs[d] = regs[r];
    regs[d + 1] = regs[r + 1];
    NEXT();
  }

  // ===================================================================
  // ARITHMETIC — REGISTER-IMMEDIATE
  // ===================================================================
  L_CPI : {
    uint8_t rd = regs[op->reg], K = (uint8_t)op->param;
    uint8_t res = rd - K;
    flags_arith(sreg, rd, K, res, true, false);
    NEXT();
  }
  L_SBCI : {
    uint8_t rd = regs[op->reg], K = (uint8_t)op->param;
    uint8_t c = sreg & FLAG_C;
    uint8_t res = rd - K - c;
    flags_sbc(sreg, rd, K, res);
    regs[op->reg] = res;
    NEXT();
  }
  L_SUBI : {
    uint8_t rd = regs[op->reg], K = (uint8_t)op->param;
    uint8_t res = rd - K;
    flags_arith(sreg, rd, K, res, true, false);
    regs[op->reg] = res;
    NEXT();
  }
  L_ORI : {
    uint8_t res = regs[op->reg] | (uint8_t)op->param;
    flags_logical(sreg, res);
    regs[op->reg] = res;
    NEXT();
  }
  L_ANDI : {
    uint8_t res = regs[op->reg] & (uint8_t)op->param;
    flags_logical(sreg, res);
    regs[op->reg] = res;
    NEXT();
  }
  L_LDI : {
    regs[op->reg] = (uint8_t)op->param;
    NEXT();
  }

  // ===================================================================
  // UNARY OPS
  // ===================================================================
  L_INC : {
    uint8_t rd = regs[op->reg];
    uint8_t res = rd + 1;
    bool V = (rd == 0x7F);
    bool N = (res >> 7) & 1;
    sreg = (sreg & (FLAG_I | FLAG_T | FLAG_H | FLAG_C)) |
           ((N ^ V) ? FLAG_S : 0) | (V ? FLAG_V : 0) | (N ? FLAG_N : 0) |
           ((res == 0) ? FLAG_Z : 0);
    regs[op->reg] = res;
    NEXT();
  }
  L_DEC : {
    uint8_t rd = regs[op->reg];
    uint8_t res = rd - 1;
    bool V = (rd == 0x80);
    bool N = (res >> 7) & 1;
    sreg = (sreg & (FLAG_I | FLAG_T | FLAG_H | FLAG_C)) |
           ((N ^ V) ? FLAG_S : 0) | (V ? FLAG_V : 0) | (N ? FLAG_N : 0) |
           ((res == 0) ? FLAG_Z : 0);
    regs[op->reg] = res;
    NEXT();
  }
  L_COM : {
    uint8_t res = ~regs[op->reg];
    sreg = (sreg & (FLAG_I | FLAG_T | FLAG_H)) | FLAG_C |
           (((res >> 7) & 1) ? (FLAG_N | FLAG_S) : 0) |
           ((res == 0) ? FLAG_Z : 0);
    regs[op->reg] = res;
    NEXT();
  }
  L_NEG : {
    uint8_t rd = regs[op->reg];
    uint8_t res = 0 - rd;
    bool H = (res & rd) & 0x08;
    bool V = (res == 0x80);
    bool N = (res >> 7) & 1;
    bool C = (res != 0);
    sreg = (sreg & (FLAG_I | FLAG_T)) | (H ? FLAG_H : 0) |
           ((N ^ V) ? FLAG_S : 0) | (V ? FLAG_V : 0) | (N ? FLAG_N : 0) |
           ((res == 0) ? FLAG_Z : 0) | (C ? FLAG_C : 0);
    regs[op->reg] = res;
    NEXT();
  }
  L_LSR : {
    uint8_t rd = regs[op->reg];
    bool C = rd & 1;
    uint8_t res = rd >> 1;
    sreg = (sreg & (FLAG_I | FLAG_T | FLAG_H)) |
           (C ? (FLAG_C | FLAG_V | FLAG_S) : 0) | ((res == 0) ? FLAG_Z : 0);
    regs[op->reg] = res;
    NEXT();
  }
  L_ASR : {
    uint8_t rd = regs[op->reg];
    bool C = rd & 1;
    uint8_t res = (rd >> 1) | (rd & 0x80);
    bool N = (res >> 7) & 1;
    bool V = N ^ C;
    sreg = (sreg & (FLAG_I | FLAG_T | FLAG_H)) | ((N ^ V) ? FLAG_S : 0) |
           (V ? FLAG_V : 0) | (N ? FLAG_N : 0) | ((res == 0) ? FLAG_Z : 0) |
           (C ? FLAG_C : 0);
    regs[op->reg] = res;
    NEXT();
  }
  L_ROR : {
    uint8_t rd = regs[op->reg];
    bool oldC = sreg & FLAG_C;
    bool C = rd & 1;
    uint8_t res = (rd >> 1) | (oldC ? 0x80 : 0);
    bool N = (res >> 7) & 1;
    bool V = N ^ C;
    sreg = (sreg & (FLAG_I | FLAG_T | FLAG_H)) | ((N ^ V) ? FLAG_S : 0) |
           (V ? FLAG_V : 0) | (N ? FLAG_N : 0) | ((res == 0) ? FLAG_Z : 0) |
           (C ? FLAG_C : 0);
    regs[op->reg] = res;
    NEXT();
  }
  L_SWAP : {
    uint8_t rd = regs[op->reg];
    regs[op->reg] = (rd >> 4) | (rd << 4);
    NEXT();
  }

  // ===================================================================
  // MULTIPLY
  // ===================================================================
  L_MUL : {
    uint16_t res =
        (uint16_t)regs[op->reg] * (uint16_t)regs[(uint8_t)op->param];
    regs[0] = res & 0xFF;
    regs[1] = (res >> 8) & 0xFF;
    sreg = (sreg & ~(FLAG_C | FLAG_Z)) | ((res & 0x8000) ? FLAG_C : 0) |
           ((res == 0) ? FLAG_Z : 0);
    NEXT();
  }
  L_MULS : {
    int16_t res = (int8_t)regs[op->reg] * (int8_t)regs[(uint8_t)op->param];
    regs[0] = res & 0xFF;
    regs[1] = (res >> 8) & 0xFF;
    sreg = (sreg & ~(FLAG_C | FLAG_Z)) | ((res & 0x8000) ? FLAG_C : 0) |
           ((res == 0) ? FLAG_Z : 0);
    NEXT();
  }
  L_MULSU : {
    int16_t res = (int8_t)regs[op->reg] * (uint8_t)regs[(uint8_t)op->param];
    regs[0] = res & 0xFF;
    regs[1] = (res >> 8) & 0xFF;
    sreg = (sreg & ~(FLAG_C | FLAG_Z)) | ((res & 0x8000) ? FLAG_C : 0) |
           ((res == 0) ? FLAG_Z : 0);
    NEXT();
  }
  L_FMUL : {
    uint16_t res =
        (uint16_t)regs[op->reg] * (uint16_t)regs[(uint8_t)op->param];
    bool C = (res >> 15) & 1;
    res <<= 1;
    regs[0] = res & 0xFF;
    regs[1] = (res >> 8) & 0xFF;
    sreg = (sreg & ~(FLAG_C | FLAG_Z)) | (C ? FLAG_C : 0) |
           ((res == 0) ? FLAG_Z : 0);
    NEXT();
  }
  L_FMULS : {
    int16_t res = (int8_t)regs[op->reg] * (int8_t)regs[(uint8_t)op->param];
    bool C = (res >> 15) & 1;
    res <<= 1;
    regs[0] = res & 0xFF;
    regs[1] = (res >> 8) & 0xFF;
    sreg = (sreg & ~(FLAG_C | FLAG_Z)) | (C ? FLAG_C : 0) |
           ((res == 0) ? FLAG_Z : 0);
    NEXT();
  }
  L_FMULSU : {
    int16_t res = (int8_t)regs[op->reg] * (uint8_t)regs[(uint8_t)op->param];
    bool C = (res >> 15) & 1;
    res <<= 1;
    regs[0] = res & 0xFF;
    regs[1] = (res >> 8) & 0xFF;
    sreg = (sreg & ~(FLAG_C | FLAG_Z)) | (C ? FLAG_C : 0) |
           ((res == 0) ? FLAG_Z : 0);
    NEXT();
  }

  // ===================================================================
  // WIDE ARITHMETIC (16-bit register pairs)
  // ===================================================================
  L_ADIW : {
    uint8_t dl = op->reg;
    uint16_t rd = regs[dl] | (regs[dl + 1] << 8);
    uint16_t res = rd + op->param;
    bool V = (!(rd >> 15)) & (res >> 15);
    bool N = (res >> 15) & 1;
    bool C = (!(res >> 15)) & (rd >> 15);
    sreg = (sreg & (FLAG_I | FLAG_T | FLAG_H)) | ((N ^ V) ? FLAG_S : 0) |
           (V ? FLAG_V : 0) | (N ? FLAG_N : 0) | ((res == 0) ? FLAG_Z : 0) |
           (C ? FLAG_C : 0);
    regs[dl] = res & 0xFF;
    regs[dl + 1] = (res >> 8) & 0xFF;
    NEXT();
  }
  L_SBIW : {
    uint8_t dl = op->reg;
    uint16_t rd = regs[dl] | (regs[dl + 1] << 8);
    uint16_t res = rd - op->param;
    bool V = (rd >> 15) & (!(res >> 15));
    bool N = (res >> 15) & 1;
    bool C = (res >> 15) & (!(rd >> 15));
    sreg = (sreg & (FLAG_I | FLAG_T | FLAG_H)) | ((N ^ V) ? FLAG_S : 0) |
           (V ? FLAG_V : 0) | (N ? FLAG_N : 0) | ((res == 0) ? FLAG_Z : 0) |
           (C ? FLAG_C : 0);
    regs[dl] = res & 0xFF;
    regs[dl + 1] = (res >> 8) & 0xFF;
    NEXT();
  }

  // ===================================================================
  // LOAD / STORE — X POINTER
  // ===================================================================
  L_LD_X : {
    uint16_t x = regs[26] | (regs[27] << 8);
    regs[op->reg] = sramLoad(sram, x);
    NEXT();
  }
  L_LD_X_INC : {
    uint16_t x = regs[26] | (regs[27] << 8);
    regs[op->reg] = sramLoad(sram, x);
    x++;
    regs[26] = x & 0xFF;
    regs[27] = x >> 8;
    NEXT();
  }
  L_LD_X_DEC : {
    uint16_t x = (regs[26] | (regs[27] << 8)) - 1;
    regs[26] = x & 0xFF;
    regs[27] = x >> 8;
    regs[op->reg] = sramLoad(sram, x);
    NEXT();
  }
  L_ST_X : {
    uint16_t x = regs[26] | (regs[27] << 8);
    sramStore(sram, x, regs[op->reg]);
    NEXT();
  }
  L_ST_X_INC : {
    uint16_t x = regs[26] | (regs[27] << 8);
    sramStore(sram, x, regs[op->reg]);
    x++;
    regs[26] = x & 0xFF;
    regs[27] = x >> 8;
    NEXT();
  }
  L_ST_X_DEC : {
    uint16_t x = (regs[26] | (regs[27] << 8)) - 1;
    regs[26] = x & 0xFF;
    regs[27] = x >> 8;
    sramStore(sram, x, regs[op->reg]);
    NEXT();
  }

  // ===================================================================
  // LOAD / STORE — Y POINTER
  // ===================================================================
  L_LD_Y_INC : {
    uint16_t y = regs[28] | (regs[29] << 8);
    regs[op->reg] = sramLoad(sram, y);
    y++;
    regs[28] = y & 0xFF;
    regs[29] = y >> 8;
    NEXT();
  }
  L_LD_Y_DEC : {
    uint16_t y = (regs[28] | (regs[29] << 8)) - 1;
    regs[28] = y & 0xFF;
    regs[29] = y >> 8;
    regs[op->reg] = sramLoad(sram, y);
    NEXT();
  }
  L_LDD_Y_Q : {
    uint16_t y = regs[28] | (regs[29] << 8);
    regs[op->reg] = sramLoad(sram, y + op->param);
    NEXT();
  }
  L_ST_Y_INC : {
    uint16_t y = regs[28] | (regs[29] << 8);
    sramStore(sram, y, regs[op->reg]);
    y++;
    regs[28] = y & 0xFF;
    regs[29] = y >> 8;
    NEXT();
  }
  L_ST_Y_DEC : {
    uint16_t y = (regs[28] | (regs[29] << 8)) - 1;
    regs[28] = y & 0xFF;
    regs[29] = y >> 8;
    sramStore(sram, y, regs[op->reg]);
    NEXT();
  }
  L_STD_Y_Q : {
    uint16_t y = regs[28] | (regs[29] << 8);
    sramStore(sram, y + op->param, regs[op->reg]);
    NEXT();
  }

  // ===================================================================
  // LOAD / STORE — Z POINTER
  // ===================================================================
  L_LD_Z_INC : {
    uint16_t z = regs[30] | (regs[31] << 8);
    regs[op->reg] = sramLoad(sram, z);
    z++;
    regs[30] = z & 0xFF;
    regs[31] = z >> 8;
    NEXT();
  }
  L_LD_Z_DEC : {
    uint16_t z = (regs[30] | (regs[31] << 8)) - 1;
    regs[30] = z & 0xFF;
    regs[31] = z >> 8;
    regs[op->reg] = sramLoad(sram, z);
    NEXT();
  }
  L_LDD_Z_Q : {
    uint16_t z = regs[30] | (regs[31] << 8);
    regs[op->reg] = sramLoad(sram, z + op->param);
    NEXT();
  }
  L_ST_Z_INC : {
    uint16_t z = regs[30] | (regs[31] << 8);
    sramStore(sram, z, regs[op->reg]);
    z++;
    regs[30] = z & 0xFF;
    regs[31] = z >> 8;
    NEXT();
  }
  L_ST_Z_DEC : {
    uint16_t z = (regs[30] | (regs[31] << 8)) - 1;
    regs[30] = z & 0xFF;
    regs[31] = z >> 8;
    sramStore(sram, z, regs[op->reg]);
    NEXT();
  }
  L_STD_Z_Q : {
    uint16_t z = regs[30] | (regs[31] << 8);
    sramStore(sram, z + op->param, regs[op->reg]);
    NEXT();
  }

  // ===================================================================
  // LDS / STS (32-bit instructions — skip data word)
  // ===================================================================
  L_LDS : {
    regs[op->reg] = sramLoad(sram, op->param);
    pc++;
    NEXT();
  }
  L_STS : {
    uint16_t stsAddr = op->param;
    if (stsAddr >= 0x20 && stsAddr <= 0x5F) {
      // Standard I/O space — route through writeIO for SREG/TIFR handling
      writeIO(stsAddr - 0x20, regs[op->reg], &sreg);
    } else if (unlikely(stsAddr == 0x7A)) {
      // ADCSRA — ADC Control and Status Register A
      // If game sets ADSC (bit 6) to start conversion, immediately clear
      // it to simulate instant completion. The game spin-waits on this bit.
      uint8_t val = regs[op->reg];
      sramStore(sram, stsAddr, val & ~(1 << 6)); // Store with ADSC cleared
      if (val & (1 << 6)) {
        m_diagAdcStarts++;
        // Provide a dummy ADC result (~2.5V midpoint = 512 = 0x200)
        sramStore(sram, 0x78, 0x00); // ADCL
        sramStore(sram, 0x79, 0x02); // ADCH
      }
    } else {
      sramStore(sram, stsAddr, regs[op->reg]);
    }
    pc++;
    NEXT();
  }

  // ===================================================================
  // LPM (program memory read)
  // ===================================================================
  L_LPM_Z : {
    uint16_t z = regs[30] | (regs[31] << 8);
    regs[op->reg] = (z < AVR_FLASH_SIZE) ? m_avrFlash[z] : 0;
    NEXT();
  }
  L_LPM_Z_INC : {
    uint16_t z = regs[30] | (regs[31] << 8);
    regs[op->reg] = (z < AVR_FLASH_SIZE) ? m_avrFlash[z] : 0;
    z++;
    regs[30] = z & 0xFF;
    regs[31] = z >> 8;
    NEXT();
  }

  // ===================================================================
  // PUSH / POP
  // ===================================================================
  L_PUSH : {
    uint16_t sp = sramLoad(sram, 0x5D) | (sramLoad(sram, 0x5E) << 8);
    sramStore(sram, sp--, regs[op->reg]);
    sramStore(sram, 0x5D, sp & 0xFF);
    sramStore(sram, 0x5E, sp >> 8);
    NEXT();
  }
  L_POP : {
    uint16_t sp = sramLoad(sram, 0x5D) | (sramLoad(sram, 0x5E) << 8);
    regs[op->reg] = sramLoad(sram, ++sp);
    sramStore(sram, 0x5D, sp & 0xFF);
    sramStore(sram, 0x5E, sp >> 8);
    NEXT();
  }

  // ===================================================================
  // I/O — now passing live sreg pointer for SREG sync
  // ===================================================================
  L_IN : {
    regs[op->reg] = readIO((uint8_t)op->param, &sreg);
    NEXT();
  }
  L_OUT : {
    writeIO((uint8_t)op->param, regs[op->reg], &sreg);
    NEXT();
  }
  L_SBI : {
    uint8_t ioAddr = op->reg;
    // For TIFR registers, SBI means "write 1 to clear" — but SBI sets
    // a single bit. On real AVR, SBI on TIFRn clears that bit.
    if (unlikely(ioAddr == 0x15 || ioAddr == 0x16)) {
      m_sram[(uint16_t)ioAddr + 0x20] &= ~(1 << (uint8_t)op->param);
    } else {
      uint16_t addr = (uint16_t)ioAddr + 0x20;
      sramStore(sram, addr, sramLoad(sram, addr) | (1 << (uint8_t)op->param));
    }
    NEXT();
  }
  L_CBI : {
    uint16_t addr = (uint16_t)op->reg + 0x20;
    sramStore(sram, addr, sramLoad(sram, addr) & ~(1 << (uint8_t)op->param));
    NEXT();
  }
  L_SBIS : {
    uint8_t val = readIO(op->reg, &sreg);
    if (val & (1 << (uint8_t)op->param))
      pc += is_two_word(s_cache[pc].opcode_class) ? 2 : 1;
    NEXT();
  }
  L_SBIC : {
    uint8_t val = readIO(op->reg, &sreg);
    if (!(val & (1 << (uint8_t)op->param)))
      pc += is_two_word(s_cache[pc].opcode_class) ? 2 : 1;
    NEXT();
  }

  // ===================================================================
  // BRANCH / CALL
  // ===================================================================
  L_RJMP : {
    pc += (int16_t)op->param;
    NEXT();
  }
  L_RCALL : {
    uint16_t ret = pc;
    uint16_t sp = sramLoad(sram, 0x5D) | (sramLoad(sram, 0x5E) << 8);
    sramStore(sram, sp--, (ret >> 8) & 0xFF);
    sramStore(sram, sp--, ret & 0xFF);
    sramStore(sram, 0x5D, sp & 0xFF);
    sramStore(sram, 0x5E, sp >> 8);
    pc += (int16_t)op->param;
    NEXT();
  }
  L_RET : {
    uint16_t sp = sramLoad(sram, 0x5D) | (sramLoad(sram, 0x5E) << 8);
    uint8_t pcl = sramLoad(sram, ++sp);
    uint8_t pch = sramLoad(sram, ++sp);
    sramStore(sram, 0x5D, sp & 0xFF);
    sramStore(sram, 0x5E, sp >> 8);
    pc = (pch << 8) | pcl;
    NEXT();
  }
  L_RETI : {
    uint16_t sp = sramLoad(sram, 0x5D) | (sramLoad(sram, 0x5E) << 8);
    uint8_t pcl = sramLoad(sram, ++sp);
    uint8_t pch = sramLoad(sram, ++sp);
    sramStore(sram, 0x5D, sp & 0xFF);
    sramStore(sram, 0x5E, sp >> 8);
    pc = (pch << 8) | pcl;
    sreg |= FLAG_I;
    NEXT();
  }
  L_IJMP : {
    pc = regs[30] | (regs[31] << 8);
    NEXT();
  }
  L_ICALL : {
    uint16_t ret = pc;
    uint16_t sp = sramLoad(sram, 0x5D) | (sramLoad(sram, 0x5E) << 8);
    sramStore(sram, sp--, (ret >> 8) & 0xFF);
    sramStore(sram, sp--, ret & 0xFF);
    sramStore(sram, 0x5D, sp & 0xFF);
    sramStore(sram, 0x5E, sp >> 8);
    pc = regs[30] | (regs[31] << 8);
    NEXT();
  }
  L_JMP : {
    pc = op->param;
    NEXT();
  }
  L_CALL : {
    uint16_t ret = pc + 1;
    uint16_t sp = sramLoad(sram, 0x5D) | (sramLoad(sram, 0x5E) << 8);
    sramStore(sram, sp--, (ret >> 8) & 0xFF);
    sramStore(sram, sp--, ret & 0xFF);
    sramStore(sram, 0x5D, sp & 0xFF);
    sramStore(sram, 0x5E, sp >> 8);
    pc = op->param;
    NEXT();
  }
  L_BRBS : {
    if (sreg & (1 << op->reg))
      pc += (int16_t)op->param;
    NEXT();
  }
  L_BRBC : {
    if (!(sreg & (1 << op->reg)))
      pc += (int16_t)op->param;
    NEXT();
  }
  L_CPSE : {
    if (regs[op->reg] == regs[(uint8_t)op->param])
      pc += is_two_word(s_cache[pc].opcode_class) ? 2 : 1;
    NEXT();
  }
  L_SBRC : {
    if (!(regs[op->reg] & (1 << (uint8_t)op->param)))
      pc += is_two_word(s_cache[pc].opcode_class) ? 2 : 1;
    NEXT();
  }
  L_SBRS : {
    if (regs[op->reg] & (1 << (uint8_t)op->param))
      pc += is_two_word(s_cache[pc].opcode_class) ? 2 : 1;
    NEXT();
  }

  // ===================================================================
  // BIT MANIPULATION
  // ===================================================================
  L_BSET : {
    sreg |= (1 << op->reg);
    NEXT();
  }
  L_BCLR : {
    sreg &= ~(1 << op->reg);
    NEXT();
  }
  L_BST : {
    if (regs[op->reg] & (1 << (uint8_t)op->param))
      sreg |= FLAG_T;
    else
      sreg &= ~FLAG_T;
    NEXT();
  }
  L_BLD : {
    if (sreg & FLAG_T)
      regs[op->reg] |= (1 << (uint8_t)op->param);
    else
      regs[op->reg] &= ~(1 << (uint8_t)op->param);
    NEXT();
  }

  // ===================================================================
  // MISC
  // ===================================================================
  L_NOP : { NEXT(); }
  L_SLEEP : {
    // On real AVR, SLEEP halts the CPU while Timer0 keeps running.
    // The CPU wakes when Timer0 overflows and fires its interrupt.
    // Simulate this by fast-forwarding TCNT0 to 0xFF so the very
    // next L_TIMER tick causes an overflow (0xFF → 0x00 + set TOV0).
    m_diagSleepCount++;
    sramStore(sram, 0x46, 0xFF);
    timerTicks = 1; // Force L_TIMER on the very next NEXT() call
    NEXT();
  }

  // ===================================================================
  // TIMER TICK (entered every 64 instructions)
  // ===================================================================
  L_TIMER : {
    timerTicks = 64;
    uint8_t tcnt0 = sramLoad(sram, 0x46);
    sramStore(sram, 0x46, tcnt0 + 1);

    if (tcnt0 == 0xFF) {
      sramStore(sram, 0x35, sramLoad(sram, 0x35) | 0x01); // Set TOV0
      m_diagTimerOverflows++;
    }

    // Check for pending Timer0 overflow interrupt
    bool gi = (sreg & FLAG_I) != 0;
    bool toie = (sramLoad(sram, 0x6E) & 0x01) != 0;
    bool tov = (sramLoad(sram, 0x35) & 0x01) != 0;

    if (unlikely(gi && toie && tov)) {
      sramStore(sram, 0x35, sramLoad(sram, 0x35) & ~0x01); // Clear TOV0
      sreg &= ~FLAG_I;
      m_diagTimerISRsFired++;

      uint16_t sp = sramLoad(sram, 0x5D) | (sramLoad(sram, 0x5E) << 8);
      sramStore(sram, sp--, (pc >> 8) & 0xFF);
      sramStore(sram, sp--, pc & 0xFF);
      sramStore(sram, 0x5D, sp & 0xFF);
      sramStore(sram, 0x5E, sp >> 8);

      pc = 0x002E; // TIMER0_OVF vector
    }

    op = &s_cache[pc++];
    goto *s_dispatch[op->opcode_class];
  }

  // ===================================================================
  // YIELD — yield to FreeRTOS scheduler so IDLE task can feed WDT
  // ===================================================================
  L_YIELD : {
    sliceBudget = 4096;
    m_pc = pc;
    m_sreg = sreg;
    vTaskDelay(0); // yield without delay — lets IDLE task reset WDT
    // Resume execution
    op = &s_cache[pc++];
    goto *s_dispatch[op->opcode_class];
  }

  // ===================================================================
  // UNHANDLED — collect for diagnostics, then skip
  // ===================================================================
  L_UNHANDLED : {
    uint16_t faultPC = pc - 1;
    uint16_t rawWord =
        m_avrFlash[faultPC * 2] | (m_avrFlash[faultPC * 2 + 1] << 8);

    // Record unique unhandled opcodes (no duplicates, bounded array)
    if (m_diagUnhandledCount < DIAG_MAX_UNHANDLED) {
      bool found = false;
      for (int i = 0; i < m_diagUnhandledCount; i++) {
        if (m_diagUnhandledRaw[i] == rawWord) {
          found = true;
          break;
        }
      }
      if (!found) {
        m_diagUnhandledPC[m_diagUnhandledCount] = faultPC;
        m_diagUnhandledRaw[m_diagUnhandledCount] = rawWord;
        m_diagUnhandledCount++;
      }
    }
    // Treat as NOP and continue (don't halt the emulator)
    NEXT();
  }

  // ===================================================================
  // EXIT — write back local state
  // ===================================================================
  L_EXIT:
  m_pc = pc;
  m_sreg = sreg;
  m_totalInsnsExecuted += (266666 - budget);
#undef NEXT
}

// ===========================================================================
// State lifecycle
// ===========================================================================
void EmulatorState::onEnter() {
  ESP_LOGI(TAG, "Entering EmulatorState — ROM: %s", m_targetRom.c_str());
  sysContext.current_state.store(SystemState::EmulatorRunning);

  DisplayManager::getInstance().clearBuffer();
  DisplayManager::getInstance().drawText(0, 0, "Loading...");
  DisplayManager::getInstance().drawText(0, 10, m_targetRom.c_str());
  DisplayManager::getInstance().renderPipelinePush();

  if (!loadHexFile(m_targetRom)) {
    ESP_LOGE(TAG, "Failed to load ROM!");
    DisplayManager::getInstance().clearBuffer();
    DisplayManager::getInstance().drawText(0, 0, "LOAD FAILED");
    DisplayManager::getInstance().renderPipelinePush();
    m_isRunning = false;
    return;
  }

  preDecodeRom();
  resetCPU();

  // --- Static pre-decode diagnostic: report any OP_UNHANDLED in the ROM ---
  {
    int unhandledStatic = 0;
    uint16_t maxPC = m_loadedBytes / 2;
    ESP_LOGI(TAG, "=== PRE-DECODE REPORT ===");
    for (uint16_t i = 0; i < maxPC; i++) {
      if (s_cache[i].opcode_class == OP_UNHANDLED) {
        uint16_t raw =
            m_avrFlash[i * 2] | (m_avrFlash[i * 2 + 1] << 8);
        if (raw != 0xFFFF) { // Skip empty flash
          unhandledStatic++;
          if (unhandledStatic <= 20) { // Print first 20
            ESP_LOGW(TAG, "  UNHANDLED at PC=0x%04X raw=0x%04X", i, raw);
          }
        }
      }
    }
    ESP_LOGI(TAG, "Total UNHANDLED in ROM: %d (of %u words)", unhandledStatic,
             maxPC);
    ESP_LOGI(TAG, "=========================");
  }

  ESP_LOGI(TAG, "ROM loaded and decoded. Starting execution.");
}

void EmulatorState::onUpdate() {
  if (InputManager::getInstance().justPressed(6)) {
    ESP_LOGI(TAG, "Exit requested");
    m_isRunning = false;
    StateManager::getInstance().changeState(&MenuState::getInstance());
    return;
  }

  if (!m_isRunning)
    return;

  m_diagUpdateCalls++;

  // Execute one frame's worth of AVR instructions
  runCpuSlice();

  // Push display buffer to OLED if a full frame was rendered
  if (m_frameDirty) {
    DisplayManager::getInstance().drawArduboyFrame(m_displayBuffer);
    m_frameDirty = false;
  }

  // --- Diagnostic report: print once after ~3 seconds of execution ---
  if (!m_diagDone && m_totalInsnsExecuted > 500000) {
    m_diagDone = true;
    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "=== RUNTIME DIAGNOSTIC REPORT ===");
    ESP_LOGI(TAG, "-------- CPU State --------");
    ESP_LOGI(TAG, "Total insns executed: %lu", (unsigned long)m_totalInsnsExecuted);
    ESP_LOGI(TAG, "onUpdate() calls: %lu", (unsigned long)m_diagUpdateCalls);
    ESP_LOGI(TAG, "Current PC: 0x%04X", m_pc);
    ESP_LOGI(TAG, "SREG: 0x%02X (I=%d)", m_sreg, (m_sreg & FLAG_I) ? 1 : 0);
    ESP_LOGI(TAG, "SP: 0x%04X", (m_sram[0x5E] << 8) | m_sram[0x5D]);
    ESP_LOGI(TAG, "-------- Timer State --------");
    ESP_LOGI(TAG, "TCNT0: 0x%02X  TIFR0: 0x%02X  TIMSK0: 0x%02X",
             m_sram[0x46], m_sram[0x35], m_sram[0x6E]);
    ESP_LOGI(TAG, "TCCR0A: 0x%02X  TCCR0B: 0x%02X",
             m_sram[0x44], m_sram[0x45]);
    ESP_LOGI(TAG, "Timer overflows: %lu  ISRs fired: %lu",
             (unsigned long)m_diagTimerOverflows,
             (unsigned long)m_diagTimerISRsFired);
    ESP_LOGI(TAG, "-------- Event Counters --------");
    ESP_LOGI(TAG, "SLEEP instructions: %lu", (unsigned long)m_diagSleepCount);
    ESP_LOGI(TAG, "ADC starts: %lu", (unsigned long)m_diagAdcStarts);
    ESP_LOGI(TAG, "SPI writes (SPDR): %lu", (unsigned long)m_diagSpiWrites);
    ESP_LOGI(TAG, "Display frames rendered: %lu", (unsigned long)m_diagFramesRendered);
    ESP_LOGI(TAG, "Display index: %d  Frame dirty: %d",
             m_displayIndex, m_frameDirty ? 1 : 0);
    ESP_LOGI(TAG, "-------- ADC Registers --------");
    ESP_LOGI(TAG, "ADMUX: 0x%02X  ADCSRA: 0x%02X  ADCSRB: 0x%02X",
             m_sram[0x7C], m_sram[0x7A], m_sram[0x7B]);
    ESP_LOGI(TAG, "ADCL: 0x%02X  ADCH: 0x%02X",
             m_sram[0x78], m_sram[0x79]);
    ESP_LOGI(TAG, "-------- SPI Registers --------");
    ESP_LOGI(TAG, "SPCR: 0x%02X  SPSR: 0x%02X  PORTD: 0x%02X",
             m_sram[0x4C], m_sram[0x4D], m_sram[0x2B]);

    if (m_diagUnhandledCount > 0) {
      ESP_LOGW(TAG, "--- Unique UNHANDLED opcodes hit at runtime: %d ---",
               m_diagUnhandledCount);
      for (int i = 0; i < m_diagUnhandledCount; i++) {
        ESP_LOGW(TAG, "  PC=0x%04X  raw=0x%04X", m_diagUnhandledPC[i],
                 m_diagUnhandledRaw[i]);
      }
    } else {
      ESP_LOGI(TAG, "No unhandled opcodes encountered at runtime!");
    }
    ESP_LOGI(TAG, "============================================================");
  }

  vTaskDelay(1);
}

void EmulatorState::onDraw() {
  // Display driven by AVR SPI writes → m_displayBuffer → drawArduboyFrame()
}

void EmulatorState::onExit() {
  ESP_LOGI(TAG, "Exiting EmulatorState");
  m_isRunning = false;
}