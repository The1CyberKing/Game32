#include "InputManager.h"
#include "types.h"
#include "EmulatorState.h"
#include "MenuState.h"
#include "StateManager.h"
#include "DisplayManager.h"
#include <esp_log.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

static const char* TAG = "EmulatorState";
static DecodedOp s_instructionCache[16384];
static uint32_t spi_data_bytes = 0;
static uint32_t spi_cmd_bytes = 0;

EmulatorState& EmulatorState::getInstance() {
    static EmulatorState instance;
    return instance;
}

void EmulatorState::setTargetRom(const std::string& fullPath) {
    m_targetRom = fullPath;
}

void EmulatorState::onEnter() {
    ESP_LOGI(TAG, "Entering Emulator State. Target ROM: %s", m_targetRom.c_str());
    sysContext.current_state.store(SystemState::EmulatorRunning);
    
    memset(m_avrFlash, 0, sizeof(m_avrFlash));
    m_loadedBytes = 0;

    DisplayManager::getInstance().clearBuffer();
    DisplayManager::getInstance().drawText(0, 0, "LOADING...");
    DisplayManager::getInstance().drawText(0, 10, m_targetRom.c_str());
    DisplayManager::getInstance().renderPipelinePush();

    if (loadHexFile(m_targetRom)) {
        char msg[32];
        snprintf(msg, sizeof(msg), "LOADED: %d Bytes", (int)m_loadedBytes);
        DisplayManager::getInstance().drawText(0, 20, msg);
        
        resetCPU();
        m_isRunning = true;
    } else {
        DisplayManager::getInstance().drawText(0, 20, "ERROR: INVALID HEX");
        m_isRunning = false;
    }
    DisplayManager::getInstance().renderPipelinePush();
}

void EmulatorState::resetCPU() {
    m_pc = 0;
    m_sreg = 0;
    memset(m_registers, 0, sizeof(m_registers));
    memset(m_sram, 0, sizeof(m_sram));
    
    writeIO(0x3E, 0x0A); // SPH
    writeIO(0x3D, 0xFF); // SPL
    
    m_displayIndex = 0;
    memset(m_displayBuffer, 0, sizeof(m_displayBuffer));
    m_frameDirty = false;
    m_cycleAccumulator = 0;
}

void IRAM_ATTR EmulatorState::writeIO(uint8_t io_addr, uint8_t value) {
    if (unlikely(io_addr == 0x0B)) {
        bool wasData = (m_sram[0x0B + 0x20] & 0x10) != 0;
        bool isData = (value & 0x10) != 0;
        if (!wasData && isData) {
            m_displayIndex = 0;
        }
    }
    if (unlikely(io_addr == 0x3F)) m_sreg = value;
    
    sramWrite(m_sram, io_addr + 0x20, value);

    if (unlikely(io_addr == 0x2E)) {
        m_sram[0x2D + 0x20] |= 0x80;
        bool isData = (m_sram[0x0B + 0x20] & 0x10) != 0;
        
        if (isData) {
            spi_data_bytes++;
            if (m_displayIndex < 1024) {
                m_displayBuffer[m_displayIndex++] = value;
            }
            if (m_displayIndex == 1024) {
                m_frameDirty = true;
                m_displayIndex = 0;
            }
        } else {
            spi_cmd_bytes++;
        }
    }
}

uint8_t IRAM_ATTR EmulatorState::readIO(uint8_t io_addr) {
    if (unlikely(io_addr == 0x3F)) return m_sreg;
    
    if (unlikely(io_addr == 0x0F)) {
        uint8_t val = 0xFF;
        if (InputManager::getInstance().isHeld(0)) val &= ~(1 << 7);
        if (InputManager::getInstance().isHeld(1)) val &= ~(1 << 4);
        if (InputManager::getInstance().isHeld(2)) val &= ~(1 << 5);
        if (InputManager::getInstance().isHeld(3)) val &= ~(1 << 6);
        return val;
    }
    
    if (unlikely(io_addr == 0x0C)) {
        uint8_t val = 0xFF;
        if (InputManager::getInstance().isHeld(4)) val &= ~(1 << 6);
        return val;
    }
    
    if (unlikely(io_addr == 0x03)) {
        uint8_t val = 0xFF;
        if (InputManager::getInstance().isHeld(5)) val &= ~(1 << 4);
        return val;
    }

    if (unlikely(io_addr == 0x04)) return m_sram[0x04 + 0x20] | 0xE0;
    if (unlikely(io_addr == 0x05)) return m_sram[0x05 + 0x20] | 0x80;
    if (unlikely(io_addr == 0x0B)) return m_sram[0x0B + 0x20] | 0x10;
    if (unlikely(io_addr == 0x24)) return 0x50;
    if (unlikely(io_addr == 0x25)) return 0x02;
    if (unlikely(io_addr == 0x29)) return m_sram[0x29 + 0x20] | 0x01;
    if (unlikely(io_addr == 0x2D)) return m_sram[0x2D + 0x20] | 0x80;
    if (unlikely(io_addr == 0x1F)) return m_sram[0x1F + 0x20] & ~0x02;
    
    return sramRead(m_sram, io_addr + 0x20);
}

static uint8_t hex2byte(const char* hex) {
    uint8_t val = 0;
    for (int i = 0; i < 2; i++) {
        char c = hex[i];
        val <<= 4;
        if (c >= '0' && c <= '9') val |= (c - '0');
        else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
    }
    return val;
}

bool EmulatorState::loadHexFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != ':') continue;
        unsigned int byteCount, address, recordType;
        if (sscanf(line, ":%02x%04x%02x", &byteCount, &address, &recordType) != 3) continue;
        if (recordType == 0x01) break;
        if (recordType == 0x00) {
            if (address + byteCount > sizeof(m_avrFlash)) { fclose(f); return false; }
            const char* dataPtr = line + 9;
            for (unsigned int i = 0; i < byteCount; i++) {
                m_avrFlash[address + i] = hex2byte(dataPtr);
                dataPtr += 2;
            }
            m_loadedBytes += byteCount;
        }
    }
    fclose(f);
    
    memset(s_instructionCache, 0, sizeof(s_instructionCache));
    
    preDecodeRom();
    return true;
}

static inline void IRAM_ATTR update_flags_add(uint8_t& sreg, uint8_t rd, uint8_t rr, uint16_t res) {
    uint8_t r = res & 0xFF;
    bool H = (rd & rr & 0x08) | (rr & ~r & 0x08) | (~r & rd & 0x08);
    bool V = (rd & rr & ~r & 0x80) | (~rd & ~rr & r & 0x80);
    bool N = r & 0x80;
    bool S = N ^ V;
    bool Z = (r == 0);
    bool C = (rd & rr & 0x80) | (rr & ~r & 0x80) | (~r & rd & 0x80);
    sreg = (sreg & 0xC0) | (H ? 0x20 : 0) | (S ? 0x10 : 0) | (V ? 0x08 : 0) | (N ? 0x04 : 0) | (Z ? 0x02 : 0) | (C ? 0x01 : 0);
}

static inline void IRAM_ATTR update_flags_sub(uint8_t& sreg, uint8_t rd, uint8_t rr, uint16_t res) {
    uint8_t r = res & 0xFF;
    bool H = (~rd & rr & 0x08) | (rr & r & 0x08) | (r & ~rd & 0x08);
    bool V = (rd & ~rr & ~r & 0x80) | (~rd & rr & r & 0x80);
    bool N = r & 0x80;
    bool S = N ^ V;
    bool Z = (r == 0);
    bool C = (~rd & rr & 0x80) | (rr & r & 0x80) | (r & ~rd & 0x80);
    sreg = (sreg & 0xC0) | (H ? 0x20 : 0) | (S ? 0x10 : 0) | (V ? 0x08 : 0) | (N ? 0x04 : 0) | (Z ? 0x02 : 0) | (C ? 0x01 : 0);
}

static inline void IRAM_ATTR update_flags_sbc(uint8_t& sreg, uint8_t rd, uint8_t rr, uint16_t res) {
    uint8_t r = res & 0xFF;
    bool H = (~rd & rr & 0x08) | (rr & r & 0x08) | (r & ~rd & 0x08);
    bool V = (rd & ~rr & ~r & 0x80) | (~rd & rr & r & 0x80);
    bool N = r & 0x80;
    bool S = N ^ V;
    bool Z = (r == 0) && (sreg & 0x02);
    bool C = (~rd & rr & 0x80) | (rr & r & 0x80) | (r & ~rd & 0x80);
    sreg = (sreg & 0xC0) | (H ? 0x20 : 0) | (S ? 0x10 : 0) | (V ? 0x08 : 0) | (N ? 0x04 : 0) | (Z ? 0x02 : 0) | (C ? 0x01 : 0);
}

static inline void IRAM_ATTR update_flags_logic(uint8_t& sreg, uint8_t res) {
    bool N = res & 0x80;
    bool V = false;
    bool S = N ^ V;
    bool Z = (res == 0);
    sreg = (sreg & 0xE1) | (S ? 0x10 : 0) | (V ? 0x08 : 0) | (N ? 0x04 : 0) | (Z ? 0x02 : 0);
}

void EmulatorState::preDecodeRom() {
    for (uint32_t pc = 0; pc < 32768 / 2; pc++) {
        uint16_t opcode = m_avrFlash[pc * 2] | (m_avrFlash[(pc * 2) + 1] << 8);
        DecodedOp& op = s_instructionCache[pc];
        
        op.length = 1; op.cycles = 1; // Defaults

        if (opcode == 0x0000) { op.opcode_class = OP_NOP; } 
        else if ((opcode & 0xF000) == 0xC000) { op.opcode_class = OP_RJMP; int16_t off = opcode&0x0FFF; if(off&0x0800) off|=0xF000; op.operands.offset = off; op.cycles=2; }
        else if ((opcode & 0xF800) == 0xB800) { op.opcode_class = OP_OUT; op.operands.r = (opcode>>4)&0x1F; op.operands.addr_or_k = (opcode&0x0F)|((opcode>>5)&0x30); }
        else if ((opcode & 0xF000) == 0xE000) { op.opcode_class = OP_LDI; op.operands.d = ((opcode>>4)&0x0F)+16; op.operands.addr_or_k = (opcode&0x0F)|((opcode>>4)&0xF0); }
        else if ((opcode & 0xFC00) == 0x0400) { op.opcode_class = OP_CPC; op.operands.r = ((opcode>>5)&0x10)|(opcode&0x0F); op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFC00) == 0x0C00) { op.opcode_class = OP_ADD; op.operands.r = ((opcode>>5)&0x10)|(opcode&0x0F); op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFC00) == 0x0800) { op.opcode_class = OP_SBC; op.operands.r = ((opcode>>5)&0x10)|(opcode&0x0F); op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFF00) == 0x0100) { op.opcode_class = OP_MOVW; op.operands.r = (opcode&0x0F)*2; op.operands.d = ((opcode>>4)&0x0F)*2; }
        else if ((opcode & 0xFF00) == 0x0200) { op.opcode_class = OP_MULS; op.operands.r = 16+(opcode&0x0F); op.operands.d = 16+((opcode>>4)&0x0F); op.cycles=2; }
        else if ((opcode & 0xFF88) == 0x0300) { op.opcode_class = OP_MULSU; op.operands.r = 16+(opcode&0x07); op.operands.d = 16+((opcode>>4)&0x07); op.cycles=2; }
        else if ((opcode & 0xFF88) == 0x0308) { op.opcode_class = OP_FMUL; op.operands.r = 16+(opcode&0x07); op.operands.d = 16+((opcode>>4)&0x07); op.cycles=2; }
        else if ((opcode & 0xFF88) == 0x0380) { op.opcode_class = OP_FMULS; op.operands.r = 16+(opcode&0x07); op.operands.d = 16+((opcode>>4)&0x07); op.cycles=2; }
        else if ((opcode & 0xFF88) == 0x0388) { op.opcode_class = OP_FMULSU; op.operands.r = 16+(opcode&0x07); op.operands.d = 16+((opcode>>4)&0x07); op.cycles=2; }
        else if ((opcode & 0xFC00) == 0x1400) { op.opcode_class = OP_CP; op.operands.r = ((opcode>>5)&0x10)|(opcode&0x0F); op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFC00) == 0x1C00) { op.opcode_class = OP_ADC; op.operands.r = ((opcode>>5)&0x10)|(opcode&0x0F); op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFC00) == 0x1800) { op.opcode_class = OP_SUB; op.operands.r = ((opcode>>5)&0x10)|(opcode&0x0F); op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFC00) == 0x2000) { op.opcode_class = OP_AND; op.operands.r = ((opcode>>5)&0x10)|(opcode&0x0F); op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFC00) == 0x2800) { op.opcode_class = OP_OR; op.operands.r = ((opcode>>5)&0x10)|(opcode&0x0F); op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFC00) == 0x2400) { op.opcode_class = OP_EOR; op.operands.r = ((opcode>>5)&0x10)|(opcode&0x0F); op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFC00) == 0x2C00) { op.opcode_class = OP_MOV; op.operands.r = ((opcode>>5)&0x10)|(opcode&0x0F); op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xF000) == 0x3000) { op.opcode_class = OP_CPI; op.operands.addr_or_k = (opcode&0x0F)|((opcode>>4)&0xF0); op.operands.d = ((opcode>>4)&0x0F)+16; }
        else if ((opcode & 0xF000) == 0x4000) { op.opcode_class = OP_SBCI; op.operands.addr_or_k = (opcode&0x0F)|((opcode>>4)&0xF0); op.operands.d = 16+((opcode>>4)&0x0F); }
        else if ((opcode & 0xF000) == 0x5000) { op.opcode_class = OP_SUBI; op.operands.addr_or_k = (opcode&0x0F)|((opcode>>4)&0xF0); op.operands.d = 16+((opcode>>4)&0x0F); }
        else if ((opcode & 0xF000) == 0x6000) { op.opcode_class = OP_ORI; op.operands.addr_or_k = (opcode&0x0F)|((opcode>>4)&0xF0); op.operands.d = 16+((opcode>>4)&0x0F); }
        else if ((opcode & 0xF000) == 0x7000) { op.opcode_class = OP_ANDI; op.operands.addr_or_k = (opcode&0x0F)|((opcode>>4)&0xF0); op.operands.d = 16+((opcode>>4)&0x0F); }
        else if ((opcode & 0xFE0F) == 0x940A) { op.opcode_class = OP_DEC; op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFE0F) == 0x9403) { op.opcode_class = OP_INC; op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFE0F) == 0x9400) { op.opcode_class = OP_COM; op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFE0F) == 0x9401) { op.opcode_class = OP_NEG; op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFC00) == 0x9C00) { op.opcode_class = OP_MUL; op.operands.r = ((opcode>>5)&0x10)|(opcode&0x0F); op.operands.d = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x8200) { op.opcode_class = OP_ST_Z; op.operands.r = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xD208) == 0x8200) { op.opcode_class = OP_STD_Z_Q; op.operands.r = (opcode>>4)&0x1F; op.operands.addr_or_k = (opcode&0x07)|((opcode>>7)&0x18)|((opcode>>8)&0x20); op.cycles=2; }
        else if ((opcode & 0xD208) == 0x8000) { op.opcode_class = OP_LDD_Z_Q; op.operands.d = (opcode>>4)&0x1F; op.operands.addr_or_k = (opcode&0x07)|((opcode>>7)&0x18)|((opcode>>8)&0x20); op.cycles=2; }
        else if ((opcode & 0xD208) == 0x8008) { op.opcode_class = OP_LDD_Y_Q; op.operands.d = (opcode>>4)&0x1F; op.operands.addr_or_k = (opcode&0x07)|((opcode>>7)&0x18)|((opcode>>8)&0x20); op.cycles=2; }
        else if ((opcode & 0xD208) == 0x8208) { op.opcode_class = OP_STD_Y_Q; op.operands.r = (opcode>>4)&0x1F; op.operands.addr_or_k = (opcode&0x07)|((opcode>>7)&0x18)|((opcode>>8)&0x20); op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x8008) { op.opcode_class = OP_LD_Y; op.operands.d = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x8208) { op.opcode_class = OP_ST_Y; op.operands.r = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x9001) { op.opcode_class = OP_LD_Z_INC; op.operands.d = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x9004) { op.opcode_class = OP_LPM_Z; op.operands.d = (opcode>>4)&0x1F; op.cycles=3; }
        else if ((opcode & 0xFE0F) == 0x9005) { op.opcode_class = OP_LPM_Z_INC; op.operands.d = (opcode>>4)&0x1F; op.cycles=3; }
        else if ((opcode & 0xFE0F) == 0x920F) { op.opcode_class = OP_PUSH; op.operands.r = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x900F) { op.opcode_class = OP_POP; op.operands.d = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x9201) { op.opcode_class = OP_ST_Z_INC; op.operands.r = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x9202) { op.opcode_class = OP_ST_Z_DEC; op.operands.r = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x9000) { op.opcode_class = OP_LDS; op.length=2; op.cycles=2; op.operands.d = (opcode>>4)&0x1F; op.operands.addr_or_k = m_avrFlash[(pc+1)*2] | (m_avrFlash[((pc+1)*2)+1]<<8); }
        else if ((opcode & 0xFE0F) == 0x9200) { op.opcode_class = OP_STS; op.length=2; op.cycles=2; op.operands.r = (opcode>>4)&0x1F; op.operands.addr_or_k = m_avrFlash[(pc+1)*2] | (m_avrFlash[((pc+1)*2)+1]<<8); }
        else if ((opcode & 0xFE0F) == 0x900C) { op.opcode_class = OP_LD_X; op.operands.d = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x900D) { op.opcode_class = OP_LD_X_INC; op.operands.d = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x900E) { op.opcode_class = OP_LD_X_DEC; op.operands.d = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x920C) { op.opcode_class = OP_ST_X; op.operands.r = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x920D) { op.opcode_class = OP_ST_X_INC; op.operands.r = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x920E) { op.opcode_class = OP_ST_X_DEC; op.operands.r = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x9009) { op.opcode_class = OP_LD_Y_INC; op.operands.d = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x900A) { op.opcode_class = OP_LD_Y_DEC; op.operands.d = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x9209) { op.opcode_class = OP_ST_Y_INC; op.operands.r = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x920A) { op.opcode_class = OP_ST_Y_DEC; op.operands.r = (opcode>>4)&0x1F; op.cycles=2; }
        else if ((opcode & 0xFC00) == 0x1000) { op.opcode_class = OP_CPSE; op.operands.r = ((opcode>>5)&0x10)|(opcode&0x0F); op.operands.d = (opcode>>4)&0x1F; op.cycles=2;}
        else if ((opcode & 0xFE0E) == 0x940C) { op.opcode_class = OP_JMP; op.length=2; op.cycles=3; op.operands.addr_or_k = m_avrFlash[(pc+1)*2] | (m_avrFlash[((pc+1)*2)+1]<<8); }
        else if ((opcode & 0xFE0E) == 0x940E) { op.opcode_class = OP_CALL; op.length=2; op.cycles=4; op.operands.addr_or_k = m_avrFlash[(pc+1)*2] | (m_avrFlash[((pc+1)*2)+1]<<8); }
        else if (opcode == 0x9508) { op.opcode_class = OP_RET; op.cycles=4; }
        else if (opcode == 0x9518) { op.opcode_class = OP_RETI; op.cycles=4; }
        else if ((opcode & 0xF000) == 0xD000) { op.opcode_class = OP_RCALL; int16_t off = opcode&0x0FFF; if(off&0x0800) off|=0xF000; op.operands.offset = off; op.cycles=3; }
        else if ((opcode & 0xFE00) == 0xFC00) { op.opcode_class = OP_SBRC; op.operands.r = (opcode>>4)&0x1F; op.operands.b = opcode&0x07; op.cycles=2; }
        else if ((opcode & 0xFE00) == 0xFE00) { op.opcode_class = OP_SBRS; op.operands.r = (opcode>>4)&0x1F; op.operands.b = opcode&0x07; op.cycles=2; }
        else if ((opcode & 0xFC00) == 0xF000) { op.opcode_class = OP_BRBS; op.operands.b = opcode&0x07; int8_t off = (opcode>>3)&0x7F; if(off&0x40) off|=0x80; op.operands.offset = off; op.cycles=2; }
        else if ((opcode & 0xFC00) == 0xF400) { op.opcode_class = OP_BRBC; op.operands.b = opcode&0x07; int8_t off = (opcode>>3)&0x7F; if(off&0x40) off|=0x80; op.operands.offset = off; op.cycles=2; }
        else if ((opcode & 0xFF00) == 0x9A00) { op.opcode_class = OP_SBI; op.operands.A = (opcode>>3)&0x1F; op.operands.b = opcode&0x07; op.cycles=2; }
        else if ((opcode & 0xFF00) == 0x9800) { op.opcode_class = OP_CBI; op.operands.A = (opcode>>3)&0x1F; op.operands.b = opcode&0x07; op.cycles=2; }
        else if ((opcode & 0xFF00) == 0x9B00) { op.opcode_class = OP_SBIS; op.operands.A = (opcode>>3)&0x1F; op.operands.b = opcode&0x07; op.cycles=2; }
        else if ((opcode & 0xFF00) == 0x9900) { op.opcode_class = OP_SBIC; op.operands.A = (opcode>>3)&0x1F; op.operands.b = opcode&0x07; op.cycles=2; }
        else if ((opcode & 0xFF08) == 0x9408) { op.opcode_class = OP_BSET; op.operands.b = (opcode>>4)&0x07; }
        else if ((opcode & 0xFF08) == 0x9488) { op.opcode_class = OP_BCLR; op.operands.b = (opcode>>4)&0x07; }
        else if ((opcode & 0xFF00) == 0x9600) { op.opcode_class = OP_ADIW; op.operands.d = 24+((opcode>>4)&0x03)*2; op.operands.addr_or_k = (opcode&0x0F)|((opcode>>2)&0x30); op.cycles=2; }
        else if ((opcode & 0xFF00) == 0x9700) { op.opcode_class = OP_SBIW; op.operands.d = 24+((opcode>>4)&0x03)*2; op.operands.addr_or_k = (opcode&0x0F)|((opcode>>2)&0x30); op.cycles=2; }
        else if ((opcode & 0xFE0F) == 0x9406) { op.opcode_class = OP_LSR; op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFE0F) == 0x9405) { op.opcode_class = OP_ASR; op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFE0F) == 0x9407) { op.opcode_class = OP_ROR; op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xFE0F) == 0x9402) { op.opcode_class = OP_SWAP; op.operands.d = (opcode>>4)&0x1F; }
        else if ((opcode & 0xF800) == 0xB000) { op.opcode_class = OP_IN; op.operands.d = (opcode>>4)&0x1F; op.operands.addr_or_k = (opcode&0x0F)|((opcode>>5)&0x30); }
        else if ((opcode & 0xFE08) == 0xFA00) { op.opcode_class = OP_BST; op.operands.d = (opcode>>4)&0x1F; op.operands.b = opcode&0x07; }
        else if ((opcode & 0xFE08) == 0xF800) { op.opcode_class = OP_BLD; op.operands.d = (opcode>>4)&0x1F; op.operands.b = opcode&0x07; }
        else if (opcode == 0x9588) { op.opcode_class = OP_SLEEP; }
        else if (opcode == 0x95E8) { op.opcode_class = OP_SPM; }
        else { op.opcode_class = OP_UNHANDLED; }
    }
}

void IRAM_ATTR EmulatorState::runCpuSlice() {
    static void* dispatch_table[] = {
        &&L_OP_NOP,
        &&L_OP_RJMP, &&L_OP_OUT, &&L_OP_LDI,
        &&L_OP_CPC, &&L_OP_SBC, &&L_OP_CP, &&L_OP_ADD, &&L_OP_ADC, &&L_OP_SUB, &&L_OP_AND, &&L_OP_OR, &&L_OP_EOR, &&L_OP_MOV,
        &&L_OP_CPI, &&L_OP_SBCI, &&L_OP_SUBI, &&L_OP_ORI, &&L_OP_ANDI, 
        &&L_OP_MUL, &&L_OP_MULS, &&L_OP_MULSU, &&L_OP_FMUL, &&L_OP_FMULS, &&L_OP_FMULSU,
        &&L_OP_INC, &&L_OP_DEC, &&L_OP_COM, &&L_OP_NEG, &&L_OP_MOVW,
        &&L_OP_ST_Z, &&L_OP_STD_Z_Q, &&L_OP_LDD_Z_Q, &&L_OP_LDD_Y_Q, &&L_OP_STD_Y_Q, &&L_OP_LD_Y, &&L_OP_ST_Y,
        &&L_OP_LD_Z_INC, &&L_OP_LPM_Z, &&L_OP_LPM_Z_INC, &&L_OP_PUSH, &&L_OP_POP, 
        &&L_OP_ST_Z_INC, &&L_OP_ST_Z_DEC, &&L_OP_LDS, &&L_OP_STS, &&L_OP_LD_X, &&L_OP_LD_X_INC, &&L_OP_LD_X_DEC,
        &&L_OP_ST_X, &&L_OP_ST_X_INC, &&L_OP_ST_X_DEC, &&L_OP_LD_Y_INC, &&L_OP_LD_Y_DEC, &&L_OP_ST_Y_INC, &&L_OP_ST_Y_DEC,
        &&L_OP_CPSE, &&L_OP_JMP, &&L_OP_CALL, &&L_OP_RET, &&L_OP_RETI, &&L_OP_RCALL, &&L_OP_SBRC, &&L_OP_SBRS, &&L_OP_BRBS, &&L_OP_BRBC,
        &&L_OP_SBI, &&L_OP_CBI, &&L_OP_SBIS, &&L_OP_SBIC, &&L_OP_BSET, &&L_OP_BCLR, &&L_OP_ADIW, &&L_OP_SBIW, &&L_OP_LSR, &&L_OP_ASR, &&L_OP_ROR, &&L_OP_SWAP, &&L_OP_IN, &&L_OP_BST, &&L_OP_BLD, &&L_OP_SLEEP, &&L_OP_SPM,
        &&L_OP_UNHANDLED
    };

    uint32_t slice_cycles = 0;
    static uint32_t total_cycles = 0;
    while (slice_cycles < 25000 && m_isRunning) {
        const DecodedOp& op = s_instructionCache[m_pc];
        
        total_cycles += op.cycles;
        if (total_cycles >= 5000000) {
            ESP_LOGW("Emulator", "HEARTBEAT | PC: 0x%04X | OpClass: %d", m_pc, op.opcode_class);
            total_cycles %= 5000000;
        }

        m_pc += op.length;
        m_cycleAccumulator += op.cycles;
        slice_cycles += op.cycles;
        
        goto *dispatch_table[op.opcode_class];

    L_OP_NOP: goto dispatch_next;
    L_OP_RJMP: { m_pc += op.operands.offset; goto dispatch_next; }
    L_OP_OUT: { writeIO(op.operands.addr_or_k, m_registers[op.operands.r]); goto dispatch_next; }
    L_OP_LDI: { m_registers[op.operands.d] = op.operands.addr_or_k; goto dispatch_next; }
    L_OP_CPC: { uint16_t res = (uint16_t)m_registers[op.operands.d] - m_registers[op.operands.r] - (m_sreg & 0x01); update_flags_sbc(m_sreg, m_registers[op.operands.d], m_registers[op.operands.r], res); goto dispatch_next; }
    L_OP_ADD: { uint16_t res = (uint16_t)m_registers[op.operands.d] + m_registers[op.operands.r]; update_flags_add(m_sreg, m_registers[op.operands.d], m_registers[op.operands.r], res); m_registers[op.operands.d] = res & 0xFF; goto dispatch_next; }
    L_OP_SBC: { uint16_t res = (uint16_t)m_registers[op.operands.d] - m_registers[op.operands.r] - (m_sreg & 0x01); update_flags_sbc(m_sreg, m_registers[op.operands.d], m_registers[op.operands.r], res); m_registers[op.operands.d] = res & 0xFF; goto dispatch_next; }
    L_OP_MOVW: { m_registers[op.operands.d] = m_registers[op.operands.r]; m_registers[op.operands.d+1] = m_registers[op.operands.r+1]; goto dispatch_next; }
    L_OP_MULS: { int16_t res = (int16_t)(int8_t)m_registers[op.operands.d] * (int16_t)(int8_t)m_registers[op.operands.r]; m_registers[0] = res & 0xFF; m_registers[1] = (res >> 8) & 0xFF; if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01; if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; goto dispatch_next; }
    L_OP_MULSU: { int16_t res = (int16_t)(int8_t)m_registers[op.operands.d] * (uint16_t)m_registers[op.operands.r]; m_registers[0] = res & 0xFF; m_registers[1] = (res >> 8) & 0xFF; if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01; if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; goto dispatch_next; }
    L_OP_FMUL: { uint16_t res = ((uint16_t)m_registers[op.operands.d] * m_registers[op.operands.r]) << 1; m_registers[0] = res & 0xFF; m_registers[1] = res >> 8; if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01; if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; goto dispatch_next; }
    L_OP_FMULS: { int16_t res = ((int16_t)(int8_t)m_registers[op.operands.d] * (int16_t)(int8_t)m_registers[op.operands.r]) << 1; m_registers[0] = res & 0xFF; m_registers[1] = (res >> 8) & 0xFF; if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01; if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; goto dispatch_next; }
    L_OP_FMULSU: { int16_t res = ((int16_t)(int8_t)m_registers[op.operands.d] * (uint16_t)m_registers[op.operands.r]) << 1; m_registers[0] = res & 0xFF; m_registers[1] = (res >> 8) & 0xFF; if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01; if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; goto dispatch_next; }
    L_OP_CP: { uint16_t res = (uint16_t)m_registers[op.operands.d] - m_registers[op.operands.r]; update_flags_sub(m_sreg, m_registers[op.operands.d], m_registers[op.operands.r], res); goto dispatch_next; }
    L_OP_ADC: { uint16_t res = (uint16_t)m_registers[op.operands.d] + m_registers[op.operands.r] + (m_sreg & 0x01); update_flags_add(m_sreg, m_registers[op.operands.d], m_registers[op.operands.r], res); m_registers[op.operands.d] = res & 0xFF; goto dispatch_next; }
    L_OP_SUB: { uint16_t res = (uint16_t)m_registers[op.operands.d] - m_registers[op.operands.r]; update_flags_sub(m_sreg, m_registers[op.operands.d], m_registers[op.operands.r], res); m_registers[op.operands.d] = res & 0xFF; goto dispatch_next; }
    L_OP_AND: { m_registers[op.operands.d] &= m_registers[op.operands.r]; update_flags_logic(m_sreg, m_registers[op.operands.d]); goto dispatch_next; }
    L_OP_OR: { m_registers[op.operands.d] |= m_registers[op.operands.r]; update_flags_logic(m_sreg, m_registers[op.operands.d]); goto dispatch_next; }
    L_OP_EOR: { m_registers[op.operands.d] ^= m_registers[op.operands.r]; update_flags_logic(m_sreg, m_registers[op.operands.d]); goto dispatch_next; }
    L_OP_MOV: { m_registers[op.operands.d] = m_registers[op.operands.r]; goto dispatch_next; }
    L_OP_CPI: { uint16_t res = (uint16_t)m_registers[op.operands.d] - op.operands.addr_or_k; update_flags_sub(m_sreg, m_registers[op.operands.d], op.operands.addr_or_k, res); goto dispatch_next; }
    L_OP_SBCI: { uint16_t res = (uint16_t)m_registers[op.operands.d] - op.operands.addr_or_k - (m_sreg & 0x01); update_flags_sbc(m_sreg, m_registers[op.operands.d], op.operands.addr_or_k, res); m_registers[op.operands.d] = res & 0xFF; goto dispatch_next; }
    L_OP_SUBI: { uint16_t res = (uint16_t)m_registers[op.operands.d] - op.operands.addr_or_k; update_flags_sub(m_sreg, m_registers[op.operands.d], op.operands.addr_or_k, res); m_registers[op.operands.d] = res & 0xFF; goto dispatch_next; }
    L_OP_ORI: { m_registers[op.operands.d] |= op.operands.addr_or_k; update_flags_logic(m_sreg, m_registers[op.operands.d]); goto dispatch_next; }
    L_OP_ANDI: { m_registers[op.operands.d] &= op.operands.addr_or_k; update_flags_logic(m_sreg, m_registers[op.operands.d]); goto dispatch_next; }
    L_OP_DEC: { uint8_t res = m_registers[op.operands.d] - 1; bool V = (m_registers[op.operands.d] == 0x80); bool N = res & 0x80; bool S = N ^ V; bool Z = (res == 0); m_sreg = (m_sreg & 0xE1) | (S ? 0x10 : 0) | (V ? 0x08 : 0) | (N ? 0x04 : 0) | (Z ? 0x02 : 0); m_registers[op.operands.d] = res; goto dispatch_next; }
    L_OP_INC: { uint8_t res = m_registers[op.operands.d] + 1; bool V = (m_registers[op.operands.d] == 0x7F); bool N = res & 0x80; bool S = N ^ V; bool Z = (res == 0); m_sreg = (m_sreg & 0xE1) | (S ? 0x10 : 0) | (V ? 0x08 : 0) | (N ? 0x04 : 0) | (Z ? 0x02 : 0); m_registers[op.operands.d] = res; goto dispatch_next; }
    L_OP_COM: { m_registers[op.operands.d] = 0xFF - m_registers[op.operands.d]; m_sreg |= 0x01; if (m_registers[op.operands.d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; if (m_registers[op.operands.d] & 0x80) m_sreg |= 0x04; else m_sreg &= ~0x04; m_sreg &= ~0x08; if (m_sreg & 0x04) m_sreg |= 0x10; else m_sreg &= ~0x10; goto dispatch_next; }
    L_OP_NEG: { uint8_t res = -m_registers[op.operands.d]; if (res != 0) m_sreg |= 0x01; else m_sreg &= ~0x01; if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; if (res & 0x80) m_sreg |= 0x04; else m_sreg &= ~0x04; if (res == 0x80) m_sreg |= 0x08; else m_sreg &= ~0x08; if (((m_sreg & 0x04) >> 2) ^ ((m_sreg & 0x08) >> 3)) m_sreg |= 0x10; else m_sreg &= ~0x10; m_registers[op.operands.d] = res; goto dispatch_next; }
    L_OP_MUL: { uint16_t res = (uint16_t)m_registers[op.operands.d] * m_registers[op.operands.r]; m_registers[0] = res & 0xFF; m_registers[1] = (res >> 8) & 0xFF; if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01; if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; goto dispatch_next; }

    L_OP_ST_Z: { sramWrite(m_sram, m_registers[30] | (m_registers[31] << 8), m_registers[op.operands.r]); goto dispatch_next; }
    L_OP_STD_Z_Q: { sramWrite(m_sram, (m_registers[30] | (m_registers[31] << 8)) + op.operands.addr_or_k, m_registers[op.operands.r]); goto dispatch_next; }
    L_OP_LDD_Z_Q: { m_registers[op.operands.d] = sramRead(m_sram, (m_registers[30] | (m_registers[31] << 8)) + op.operands.addr_or_k); goto dispatch_next; }
    L_OP_LDD_Y_Q: { m_registers[op.operands.d] = sramRead(m_sram, (m_registers[28] | (m_registers[29] << 8)) + op.operands.addr_or_k); goto dispatch_next; }
    L_OP_STD_Y_Q: { sramWrite(m_sram, (m_registers[28] | (m_registers[29] << 8)) + op.operands.addr_or_k, m_registers[op.operands.r]); goto dispatch_next; }
    L_OP_LD_Y: { m_registers[op.operands.d] = sramRead(m_sram, m_registers[28] | (m_registers[29] << 8)); goto dispatch_next; }
    L_OP_ST_Y: { sramWrite(m_sram, m_registers[28] | (m_registers[29] << 8), m_registers[op.operands.r]); goto dispatch_next; }
    L_OP_LD_Z_INC: { uint16_t ptr = m_registers[30] | (m_registers[31] << 8); m_registers[op.operands.d] = sramRead(m_sram, ptr); ptr++; m_registers[30] = ptr & 0xFF; m_registers[31] = (ptr >> 8) & 0xFF; goto dispatch_next; }
    L_OP_LPM_Z: { m_registers[op.operands.d] = m_avrFlash[m_registers[30] | (m_registers[31] << 8)]; goto dispatch_next; }
    L_OP_LPM_Z_INC: { uint16_t ptr = m_registers[30] | (m_registers[31] << 8); m_registers[op.operands.d] = m_avrFlash[ptr]; ptr++; m_registers[30] = ptr & 0xFF; m_registers[31] = (ptr >> 8) & 0xFF; goto dispatch_next; }
    L_OP_PUSH: { uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8); sramWrite(m_sram, sp--, m_registers[op.operands.r]); writeIO(0x3D, sp & 0xFF); writeIO(0x3E, sp >> 8); goto dispatch_next; }
    L_OP_POP: { uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8); sp++; m_registers[op.operands.d] = sramRead(m_sram, sp); writeIO(0x3D, sp & 0xFF); writeIO(0x3E, sp >> 8); goto dispatch_next; }
    L_OP_ST_Z_INC: { uint16_t ptr = m_registers[30] | (m_registers[31] << 8); sramWrite(m_sram, ptr, m_registers[op.operands.r]); ptr++; m_registers[30] = ptr & 0xFF; m_registers[31] = (ptr >> 8) & 0xFF; goto dispatch_next; }
    L_OP_ST_Z_DEC: { uint16_t ptr = m_registers[30] | (m_registers[31] << 8); ptr--; sramWrite(m_sram, ptr, m_registers[op.operands.r]); m_registers[30] = ptr & 0xFF; m_registers[31] = (ptr >> 8) & 0xFF; goto dispatch_next; }
    L_OP_LDS: { m_registers[op.operands.d] = sramRead(m_sram, op.operands.addr_or_k); goto dispatch_next; }
    L_OP_STS: { sramWrite(m_sram, op.operands.addr_or_k, m_registers[op.operands.r]);
        if (op.operands.addr_or_k == 0x007A) {
            if (sramRead(m_sram, 0x007A) & (1 << 6)) {
                sramWrite(m_sram, 0x007A, sramRead(m_sram, 0x007A) & ~(1 << 6));
                sramWrite(m_sram, 0x007A, sramRead(m_sram, 0x007A) | (1 << 4));
                m_sram[0x0078] = 0x50; m_sram[0x0079] = 0x02;
            }
        }
        goto dispatch_next; 
    }
    L_OP_LD_X: { m_registers[op.operands.d] = sramRead(m_sram, m_registers[26] | (m_registers[27] << 8)); goto dispatch_next; }
    L_OP_LD_X_INC: { uint16_t ptr = m_registers[26] | (m_registers[27] << 8); m_registers[op.operands.d] = sramRead(m_sram, ptr); ptr++; m_registers[26] = ptr & 0xFF; m_registers[27] = (ptr >> 8) & 0xFF; goto dispatch_next; }
    L_OP_LD_X_DEC: { uint16_t ptr = m_registers[26] | (m_registers[27] << 8); ptr--; m_registers[op.operands.d] = sramRead(m_sram, ptr); m_registers[26] = ptr & 0xFF; m_registers[27] = (ptr >> 8) & 0xFF; goto dispatch_next; }
    L_OP_ST_X: { sramWrite(m_sram, m_registers[26] | (m_registers[27] << 8), m_registers[op.operands.r]); goto dispatch_next; }
    L_OP_ST_X_INC: { uint16_t ptr = m_registers[26] | (m_registers[27] << 8); sramWrite(m_sram, ptr, m_registers[op.operands.r]); ptr++; m_registers[26] = ptr & 0xFF; m_registers[27] = (ptr >> 8) & 0xFF; goto dispatch_next; }
    L_OP_ST_X_DEC: { uint16_t ptr = m_registers[26] | (m_registers[27] << 8); ptr--; sramWrite(m_sram, ptr, m_registers[op.operands.r]); m_registers[26] = ptr & 0xFF; m_registers[27] = (ptr >> 8) & 0xFF; goto dispatch_next; }
    L_OP_LD_Y_INC: { uint16_t ptr = m_registers[28] | (m_registers[29] << 8); m_registers[op.operands.d] = sramRead(m_sram, ptr); ptr++; m_registers[28] = ptr & 0xFF; m_registers[29] = (ptr >> 8) & 0xFF; goto dispatch_next; }
    L_OP_LD_Y_DEC: { uint16_t ptr = m_registers[28] | (m_registers[29] << 8); ptr--; m_registers[op.operands.d] = sramRead(m_sram, ptr); m_registers[28] = ptr & 0xFF; m_registers[29] = (ptr >> 8) & 0xFF; goto dispatch_next; }
    L_OP_ST_Y_INC: { uint16_t ptr = m_registers[28] | (m_registers[29] << 8); sramWrite(m_sram, ptr, m_registers[op.operands.r]); ptr++; m_registers[28] = ptr & 0xFF; m_registers[29] = (ptr >> 8) & 0xFF; goto dispatch_next; }
    L_OP_ST_Y_DEC: { uint16_t ptr = m_registers[28] | (m_registers[29] << 8); ptr--; sramWrite(m_sram, ptr, m_registers[op.operands.r]); m_registers[28] = ptr & 0xFF; m_registers[29] = (ptr >> 8) & 0xFF; goto dispatch_next; }

    L_OP_CPSE: { if (m_registers[op.operands.d] == m_registers[op.operands.r]) m_pc += s_instructionCache[m_pc].length; goto dispatch_next; }
    L_OP_JMP: { m_pc = op.operands.addr_or_k; goto dispatch_next; }
    L_OP_CALL: { uint16_t ret_addr = m_pc; uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8); sramWrite(m_sram, sp--, (ret_addr >> 8) & 0xFF); sramWrite(m_sram, sp--, ret_addr & 0xFF); writeIO(0x3D, sp & 0xFF); writeIO(0x3E, sp >> 8); m_pc = op.operands.addr_or_k; goto dispatch_next; }
    L_OP_RET: { uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8); sp++; uint8_t lb = sramRead(m_sram, sp); sp++; uint8_t hb = sramRead(m_sram, sp); m_pc = (hb << 8) | lb; writeIO(0x3D, sp & 0xFF); writeIO(0x3E, sp >> 8); goto dispatch_next; }
    L_OP_RETI: { uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8); sp++; uint8_t lb = sramRead(m_sram, sp); sp++; uint8_t hb = sramRead(m_sram, sp); m_pc = (hb << 8) | lb; writeIO(0x3D, sp & 0xFF); writeIO(0x3E, sp >> 8); m_sreg |= 0x80; goto dispatch_next; }
    L_OP_RCALL: { uint16_t ret_addr = m_pc; uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8); sramWrite(m_sram, sp--, (ret_addr >> 8) & 0xFF); sramWrite(m_sram, sp--, ret_addr & 0xFF); writeIO(0x3D, sp & 0xFF); writeIO(0x3E, sp >> 8); m_pc += op.operands.offset; goto dispatch_next; }
    L_OP_SBRC: { if (!(m_registers[op.operands.r] & (1 << op.operands.b))) m_pc += s_instructionCache[m_pc].length; goto dispatch_next; }
    L_OP_SBRS: { if (m_registers[op.operands.r] & (1 << op.operands.b)) m_pc += s_instructionCache[m_pc].length; goto dispatch_next; }
    L_OP_BRBS: { if (m_sreg & (1 << op.operands.b)) m_pc += op.operands.offset; goto dispatch_next; }
    L_OP_BRBC: { if (!(m_sreg & (1 << op.operands.b))) m_pc += op.operands.offset; goto dispatch_next; }

    L_OP_SBI: { writeIO(op.operands.A, readIO(op.operands.A) | (1 << op.operands.b)); goto dispatch_next; }
    L_OP_CBI: { writeIO(op.operands.A, readIO(op.operands.A) & ~(1 << op.operands.b)); goto dispatch_next; }
    L_OP_SBIS: { if (readIO(op.operands.A) & (1 << op.operands.b)) m_pc += s_instructionCache[m_pc].length; goto dispatch_next; }
    L_OP_SBIC: { if (!(readIO(op.operands.A) & (1 << op.operands.b))) m_pc += s_instructionCache[m_pc].length; goto dispatch_next; }
    L_OP_BSET: { m_sreg |= (1 << op.operands.b); goto dispatch_next; }
    L_OP_BCLR: { m_sreg &= ~(1 << op.operands.b); goto dispatch_next; }
    L_OP_ADIW: { uint16_t word = m_registers[op.operands.d] | (m_registers[op.operands.d+1] << 8); uint16_t res = word + op.operands.addr_or_k; m_registers[op.operands.d] = res & 0xFF; m_registers[op.operands.d+1] = res >> 8; bool word15 = word & 0x8000; bool res15 = res & 0x8000; if (!word15 && res15) m_sreg |= 0x08; else m_sreg &= ~0x08; if (!res15 && word15) m_sreg |= 0x01; else m_sreg &= ~0x01; if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; if (res15) m_sreg |= 0x04; else m_sreg &= ~0x04; if (((m_sreg & 0x04) >> 2) ^ ((m_sreg & 0x08) >> 3)) m_sreg |= 0x10; else m_sreg &= ~0x10; goto dispatch_next; }
    L_OP_SBIW: { uint16_t word = m_registers[op.operands.d] | (m_registers[op.operands.d+1] << 8); uint16_t res = word - op.operands.addr_or_k; m_registers[op.operands.d] = res & 0xFF; m_registers[op.operands.d+1] = res >> 8; bool word15 = word & 0x8000; bool res15 = res & 0x8000; if (word15 && !res15) m_sreg |= 0x08; else m_sreg &= ~0x08; if (res15 && !word15) m_sreg |= 0x01; else m_sreg &= ~0x01; if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; if (res15) m_sreg |= 0x04; else m_sreg &= ~0x04; if (((m_sreg & 0x04) >> 2) ^ ((m_sreg & 0x08) >> 3)) m_sreg |= 0x10; else m_sreg &= ~0x10; goto dispatch_next; }
    L_OP_LSR: { if (m_registers[op.operands.d] & 0x01) m_sreg |= 0x01; else m_sreg &= ~0x01; m_registers[op.operands.d] >>= 1; if (m_registers[op.operands.d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; goto dispatch_next; }
    L_OP_ASR: { if (m_registers[op.operands.d] & 0x01) m_sreg |= 0x01; else m_sreg &= ~0x01; m_registers[op.operands.d] = (m_registers[op.operands.d] & 0x80) | (m_registers[op.operands.d] >> 1); if (m_registers[op.operands.d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; goto dispatch_next; }
    L_OP_ROR: { uint8_t old_c = (m_sreg & 0x01) ? 0x80 : 0x00; if (m_registers[op.operands.d] & 0x01) m_sreg |= 0x01; else m_sreg &= ~0x01; m_registers[op.operands.d] = old_c | (m_registers[op.operands.d] >> 1); if (m_registers[op.operands.d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; goto dispatch_next; }
    L_OP_SWAP: { m_registers[op.operands.d] = (m_registers[op.operands.d] >> 4) | (m_registers[op.operands.d] << 4); goto dispatch_next; }
    L_OP_IN: { m_registers[op.operands.d] = readIO(op.operands.addr_or_k); goto dispatch_next; }
    L_OP_BST: { if (m_registers[op.operands.d] & (1 << op.operands.b)) m_sreg |= 0x40; else m_sreg &= ~0x40; goto dispatch_next; }
    L_OP_BLD: { if (m_sreg & 0x40) m_registers[op.operands.d] |= (1 << op.operands.b); else m_registers[op.operands.d] &= ~(1 << op.operands.b); goto dispatch_next; }
    L_OP_SLEEP: goto dispatch_next;
    L_OP_SPM: goto dispatch_next;

    L_OP_UNHANDLED: {
        static uint16_t last_unhandled = 0xFFFF;
        // m_pc was already incremented, so look back by op.length
        uint16_t fetch_pc = m_pc - op.length; 
        uint16_t raw_opcode = m_avrFlash[fetch_pc * 2] | (m_avrFlash[(fetch_pc * 2) + 1] << 8);
        
        if (raw_opcode != last_unhandled) {
            ESP_LOGE("Emulator", "FATAL UNHANDLED OPCODE: 0x%04X at PC: 0x%04X", raw_opcode, fetch_pc);
            last_unhandled = raw_opcode;
        }
        goto dispatch_next;
    }

    dispatch_next:
        if (unlikely(m_cycleAccumulator >= 64)) {
            m_cycleAccumulator -= 64;
            uint8_t tcnt0 = sramRead(m_sram, 0x46);
            sramWrite(m_sram, 0x46, tcnt0 + 1);
            if (tcnt0 == 0xFF) { 
                sramWrite(m_sram, 0x35, sramRead(m_sram, 0x35) | 0x01);
            }
            
            bool global_int_enabled = (m_sreg & 0x80) != 0;
            bool timer0_ovf_enabled = (sramRead(m_sram, 0x6E) & 0x01) != 0;
            bool timer0_ovf_flag = (sramRead(m_sram, 0x35) & 0x01) != 0;

            if (unlikely(global_int_enabled && timer0_ovf_enabled && timer0_ovf_flag)) {
                sramWrite(m_sram, 0x35, sramRead(m_sram, 0x35) & ~0x01);
                m_sreg &= ~0x80;
                
                uint16_t sp = sramRead(m_sram, 0x3D + 0x20) | (sramRead(m_sram, 0x3E + 0x20) << 8);
                sramWrite(m_sram, sp--, (m_pc >> 8) & 0xFF);
                sramWrite(m_sram, sp--, m_pc & 0xFF);
                
                writeIO(0x3D, sp & 0xFF);
                writeIO(0x3E, sp >> 8);
                
                m_pc = 0x002E; 
            }
        }
    }
}

void EmulatorState::onUpdate() {
    if (InputManager::getInstance().justPressed(5)) {
        m_isRunning = false;
        StateManager::getInstance().changeState(&MenuState::getInstance());
        return;
    }

    if (!m_isRunning) return;

    runCpuSlice();

    if (m_frameDirty) {
        DisplayManager::getInstance().drawArduboyFrame(m_displayBuffer);
        m_frameDirty = false;
    }

    vTaskDelay(1);
}

void EmulatorState::onDraw() { }
void EmulatorState::onExit() { }