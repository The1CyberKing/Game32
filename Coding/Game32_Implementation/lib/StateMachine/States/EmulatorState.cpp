#include "InputManager.h"
#include "EmulatorState.h"
#include "MenuState.h"
#include "StateManager.h"
#include "DisplayManager.h"
#include "InputManager.h"
#include "types.h"
#include <esp_log.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "EmulatorState";

static uint32_t spi_data_bytes = 0;
static uint32_t spi_cmd_bytes = 0;

static bool enable_trace = false;
static uint32_t trace_count = 0;

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
    
    // Clear previous ghost data
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
        
        enable_trace = false;
        trace_count = 0;
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
    
    // Initialize Stack Pointer to top of ATMega32u4 SRAM (0x0AFF)
    writeIO(0x3E, 0x0A); // SPH
    writeIO(0x3D, 0xFF); // SPL
    
    m_displayIndex = 0;
    memset(m_displayBuffer, 0, sizeof(m_displayBuffer));
}

void EmulatorState::writeIO(uint8_t io_addr, uint8_t value) {
    // Intercept PORTD (0x0B) to sync Display Frames
    if (io_addr == 0x0B) {
        bool wasData = (m_sram[0x0B + 0x20] & 0x10) != 0;
        bool isData = (value & 0x10) != 0;
        if (!wasData && isData) {
            // The D/C pin just went HIGH. A new frame/data burst is beginning!
            if (m_displayIndex > 0) {
                ESP_LOGW(TAG, "D/C->HIGH: discarding %u partial data bytes", m_displayIndex);
            }
            m_displayIndex = 0;
        }
    }

    // Intercept SREG (0x3F) writes to update m_sreg directly
    if (io_addr == 0x3F) {
        m_sreg = value;
    }
    
    sramWrite(io_addr + 0x20, value);

    // Intercept SPI writes to SPDR
    if (io_addr == 0x2E) {
        // Break the SPI Deadlock by instantly asserting the SPIF bit in SPSR (0x2D)
        m_sram[0x2D + 0x20] |= 0x80;
        
        // Check PORTD (0x0B) Bit 4 to distinguish Data vs Command
        bool isData = (m_sram[0x0B + 0x20] & 0x10) != 0;
        
        if (isData) {
            spi_data_bytes++;
            if (m_displayIndex < 1024) {
                m_displayBuffer[m_displayIndex++] = value;
            }
            if (m_displayIndex == 1024) {
                ESP_LOGI(TAG, ">>> PUSHING FULL 1024-BYTE FRAME <<<");
                DisplayManager::getInstance().drawArduboyFrame(m_displayBuffer);
                m_displayIndex = 0;
            }
        } else {
            // SPI Command byte
            spi_cmd_bytes++;
        }
    }
}

#include "InputManager.h"

uint8_t EmulatorState::readIO(uint8_t io_addr) {
    if (io_addr == 0x3F) return m_sreg;
    
    if (io_addr == 0x0F) { // PINF (D-Pad)
        uint8_t val = 0xFF;
        if (InputManager::getInstance().isHeld(0)) val &= ~(1 << 7); // UP
        if (InputManager::getInstance().isHeld(1)) val &= ~(1 << 4); // DOWN (Arduboy uses PF4 for DOWN, PF5 for LEFT, PF6 for RIGHT)
        if (InputManager::getInstance().isHeld(2)) val &= ~(1 << 5); // LEFT
        if (InputManager::getInstance().isHeld(3)) val &= ~(1 << 6); // RIGHT
        return val;
    }
    
    if (io_addr == 0x0C) { // PINE (A Button)
        uint8_t val = 0xFF;
        if (InputManager::getInstance().isHeld(4)) val &= ~(1 << 6); // A
        return val;
    }
    
    if (io_addr == 0x03) { // PINB (B Button)
        uint8_t val = 0xFF;
        if (InputManager::getInstance().isHeld(5)) val &= ~(1 << 4); // B
        return val;
    }

    // Hardware Auto-Completer for special registers
    if (io_addr == 0x04) return m_sram[0x04 + 0x20] | 0xE0; // DDRB: Bits 5, 6, 7 as Output
    if (io_addr == 0x05) return m_sram[0x05 + 0x20] | 0x80; // PORTB: Set bit 7 (CS) high
    if (io_addr == 0x0B) return m_sram[0x0B + 0x20] | 0x10; // PORTD: Bit 4 = Reset Pin High
    if (io_addr == 0x24) return 0x50; // ADCL: Mock battery voltage
    if (io_addr == 0x25) return 0x02; // ADCH: Mock battery voltage
    if (io_addr == 0x29) return m_sram[0x29 + 0x20] | 0x01; // USB PLL Lock
    if (io_addr == 0x2D) return m_sram[0x2D + 0x20] | 0x80; // SPSR: SPIF Bit
    if (io_addr == 0x1F) return m_sram[0x1F + 0x20] & ~0x02; // EECR: Wait for EEPE
    
    return sramRead(io_addr + 0x20);
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
    if (!f) {
        ESP_LOGE(TAG, "Failed to open hex file: %s", path.c_str());
        return false;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != ':') continue;

        unsigned int byteCount, address, recordType;
        if (sscanf(line, ":%02x%04x%02x", &byteCount, &address, &recordType) != 3) {
            continue;
        }

        if (recordType == 0x01) {
            break; // End Of File record
        }
        
        if (recordType == 0x00) { // Data record
            if (address + byteCount > sizeof(m_avrFlash)) {
                ESP_LOGE(TAG, "Hex data out of bounds at address 0x%04X", address);
                fclose(f);
                return false;
            }

            const char* dataPtr = line + 9;
            for (unsigned int i = 0; i < byteCount; i++) {
                m_avrFlash[address + i] = hex2byte(dataPtr);
                dataPtr += 2;
            }
            m_loadedBytes += byteCount;
        }
    }
    
    fclose(f);
    return true;
}

// Flag Helper Functions
static inline void update_flags_add(uint8_t& sreg, uint8_t rd, uint8_t rr, uint16_t res) {
    uint8_t r = res & 0xFF;
    bool H = (rd & rr & 0x08) | (rr & ~r & 0x08) | (~r & rd & 0x08);
    bool V = (rd & rr & ~r & 0x80) | (~rd & ~rr & r & 0x80);
    bool N = r & 0x80;
    bool S = N ^ V;
    bool Z = (r == 0);
    bool C = (rd & rr & 0x80) | (rr & ~r & 0x80) | (~r & rd & 0x80);

    sreg = (sreg & 0xC0) | (H ? 0x20 : 0) | (S ? 0x10 : 0) | (V ? 0x08 : 0) | (N ? 0x04 : 0) | (Z ? 0x02 : 0) | (C ? 0x01 : 0);
}

static inline void update_flags_sub(uint8_t& sreg, uint8_t rd, uint8_t rr, uint16_t res) {
    uint8_t r = res & 0xFF;
    bool H = (~rd & rr & 0x08) | (rr & r & 0x08) | (r & ~rd & 0x08);
    bool V = (rd & ~rr & ~r & 0x80) | (~rd & rr & r & 0x80);
    bool N = r & 0x80;
    bool S = N ^ V;
    bool Z = (r == 0);
    bool C = (~rd & rr & 0x80) | (rr & r & 0x80) | (r & ~rd & 0x80);

    sreg = (sreg & 0xC0) | (H ? 0x20 : 0) | (S ? 0x10 : 0) | (V ? 0x08 : 0) | (N ? 0x04 : 0) | (Z ? 0x02 : 0) | (C ? 0x01 : 0);
}

static inline void update_flags_sbc(uint8_t& sreg, uint8_t rd, uint8_t rr, uint16_t res) {
    uint8_t r = res & 0xFF;
    bool H = (~rd & rr & 0x08) | (rr & r & 0x08) | (r & ~rd & 0x08);
    bool V = (rd & ~rr & ~r & 0x80) | (~rd & rr & r & 0x80);
    bool N = r & 0x80;
    bool S = N ^ V;
    bool Z = (r == 0) && (sreg & 0x02);
    bool C = (~rd & rr & 0x80) | (rr & r & 0x80) | (r & ~rd & 0x80);

    sreg = (sreg & 0xC0) | (H ? 0x20 : 0) | (S ? 0x10 : 0) | (V ? 0x08 : 0) | (N ? 0x04 : 0) | (Z ? 0x02 : 0) | (C ? 0x01 : 0);
}

static inline void update_flags_logic(uint8_t& sreg, uint8_t res) {
    bool N = res & 0x80;
    bool V = false; // V is always cleared
    bool S = N ^ V;
    bool Z = (res == 0);
    
    sreg = (sreg & 0xE1) | (S ? 0x10 : 0) | (V ? 0x08 : 0) | (N ? 0x04 : 0) | (Z ? 0x02 : 0);
}

void EmulatorState::executeOpcode(uint16_t opcode) {
    auto skip_next = [&]() {
        uint16_t next_op = m_avrFlash[m_pc * 2] | (m_avrFlash[(m_pc * 2) + 1] << 8);
        bool is_32bit = false;
        if ((next_op & 0xFE0E) == 0x940C) is_32bit = true; // JMP
        else if ((next_op & 0xFE0E) == 0x940E) is_32bit = true; // CALL
        else if ((next_op & 0xFE0F) == 0x9000) is_32bit = true; // LDS
        else if ((next_op & 0xFE0F) == 0x9200) is_32bit = true; // STS
        m_pc += is_32bit ? 2 : 1;
    };

    if (opcode == 0x0000) {
        // NOP
    } else if ((opcode & 0xFC00) == 0x0400) {
        // CPC Rd, Rr (Compare with Carry)
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t c = m_sreg & 0x01;
        uint16_t res = (uint16_t)m_registers[d] - m_registers[r] - c;
        update_flags_sbc(m_sreg, m_registers[d], m_registers[r], res);
    } else if ((opcode & 0xFC00) == 0x0C00) {
        // ADD Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t res = (uint16_t)m_registers[d] + m_registers[r];
        update_flags_add(m_sreg, m_registers[d], m_registers[r], res);
        m_registers[d] = res & 0xFF;
    } else if ((opcode & 0xFC00) == 0x0800) {
        // SBC Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t c = m_sreg & 0x01;
        uint16_t res = (uint16_t)m_registers[d] - m_registers[r] - c;
        update_flags_sbc(m_sreg, m_registers[d], m_registers[r], res);
        m_registers[d] = res & 0xFF;
    } else if ((opcode & 0xFF00) == 0x0100) {
        // MOVW Rd, Rr
        uint8_t r = (opcode & 0x0F) * 2;
        uint8_t d = ((opcode >> 4) & 0x0F) * 2;
        m_registers[d] = m_registers[r];
        m_registers[d+1] = m_registers[r+1];
    } else if ((opcode & 0xFF00) == 0x0200) {
        // MULS Rd, Rr
        uint8_t d = 16 + ((opcode >> 4) & 0x0F);
        uint8_t r = 16 + (opcode & 0x0F);
        int16_t res = (int16_t)(int8_t)m_registers[d] * (int16_t)(int8_t)m_registers[r];
        m_registers[0] = res & 0xFF; m_registers[1] = (res >> 8) & 0xFF;
        if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01;
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02;
    } else if ((opcode & 0xFF88) == 0x0300) {
        // MULSU Rd, Rr
        uint8_t d = 16 + ((opcode >> 4) & 0x07);
        uint8_t r = 16 + (opcode & 0x07);
        int16_t res = (int16_t)(int8_t)m_registers[d] * (uint16_t)m_registers[r];
        m_registers[0] = res & 0xFF; m_registers[1] = (res >> 8) & 0xFF;
        if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01;
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02;
    } else if ((opcode & 0xFF88) == 0x0308) {
        // FMUL Rd, Rr
        uint8_t d = 16 + ((opcode >> 4) & 0x07);
        uint8_t r = 16 + (opcode & 0x07);
        uint16_t res = ((uint16_t)m_registers[d] * m_registers[r]) << 1;
        m_registers[0] = res & 0xFF; m_registers[1] = res >> 8;
        if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01;
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02;
    } else if ((opcode & 0xFF88) == 0x0380) {
        // FMULS Rd, Rr
        uint8_t d = 16 + ((opcode >> 4) & 0x07);
        uint8_t r = 16 + (opcode & 0x07);
        int16_t res = ((int16_t)(int8_t)m_registers[d] * (int16_t)(int8_t)m_registers[r]) << 1;
        m_registers[0] = res & 0xFF; m_registers[1] = (res >> 8) & 0xFF;
        if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01;
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02;
    } else if ((opcode & 0xFF88) == 0x0388) {
        // FMULSU Rd, Rr
        uint8_t d = 16 + ((opcode >> 4) & 0x07);
        uint8_t r = 16 + (opcode & 0x07);
        int16_t res = ((int16_t)(int8_t)m_registers[d] * (uint16_t)m_registers[r]) << 1;
        m_registers[0] = res & 0xFF; m_registers[1] = (res >> 8) & 0xFF;
        if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01;
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02;
    } else if ((opcode & 0xFC00) == 0x1400) {
        // CP Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t res = (uint16_t)m_registers[d] - m_registers[r];
        update_flags_sub(m_sreg, m_registers[d], m_registers[r], res);
    } else if ((opcode & 0xFC00) == 0x1C00) {
        // ADC Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t c = m_sreg & 0x01;
        uint16_t res = (uint16_t)m_registers[d] + m_registers[r] + c;
        update_flags_add(m_sreg, m_registers[d], m_registers[r], res);
        m_registers[d] = res & 0xFF;
    } else if ((opcode & 0xFC00) == 0x1800) {
        // SUB Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t res = (uint16_t)m_registers[d] - m_registers[r];
        update_flags_sub(m_sreg, m_registers[d], m_registers[r], res);
        m_registers[d] = res & 0xFF;
    } else if ((opcode & 0xFC00) == 0x1000) {
        // CPSE Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        if (m_registers[d] == m_registers[r]) skip_next();
    } else if ((opcode & 0xFC00) == 0x2000) {
        // AND Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d] = m_registers[d] & m_registers[r];
        update_flags_logic(m_sreg, m_registers[d]);
    } else if ((opcode & 0xFC00) == 0x2800) {
        // OR Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d] = m_registers[d] | m_registers[r];
        update_flags_logic(m_sreg, m_registers[d]);
    } else if ((opcode & 0xFC00) == 0x2400) {
        // EOR Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d] = m_registers[d] ^ m_registers[r];
        update_flags_logic(m_sreg, m_registers[d]);
    } else if ((opcode & 0xFC00) == 0x2C00) {
        // MOV Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d] = m_registers[r];
    } else if ((opcode & 0xF000) == 0x3000) {
        // CPI Rd, K
        uint8_t d = ((opcode >> 4) & 0x0F) + 16;
        uint8_t k = (opcode & 0x0F) | ((opcode >> 4) & 0xF0);
        uint16_t res = (uint16_t)m_registers[d] - k;
        update_flags_sub(m_sreg, m_registers[d], k, res);
    } else if ((opcode & 0xF000) == 0x4000) {
        // SBCI Rd, K
        uint8_t d = 16 + ((opcode >> 4) & 0x0F);
        uint8_t K = ((opcode >> 4) & 0xF0) | (opcode & 0x0F);
        uint8_t c = m_sreg & 0x01;
        uint16_t res = (uint16_t)m_registers[d] - K - c;
        update_flags_sbc(m_sreg, m_registers[d], K, res);
        m_registers[d] = res & 0xFF;
    } else if ((opcode & 0xF000) == 0x5000) {
        // SUBI Rd, K
        uint8_t d = 16 + ((opcode >> 4) & 0x0F);
        uint8_t K = ((opcode >> 4) & 0xF0) | (opcode & 0x0F);
        uint16_t res = (uint16_t)m_registers[d] - K;
        update_flags_sub(m_sreg, m_registers[d], K, res);
        m_registers[d] = res & 0xFF;
    } else if ((opcode & 0xF000) == 0x6000) {
        // ORI Rd, K
        uint8_t d = 16 + ((opcode >> 4) & 0x0F);
        uint8_t K = ((opcode >> 4) & 0xF0) | (opcode & 0x0F);
        m_registers[d] = m_registers[d] | K;
        update_flags_logic(m_sreg, m_registers[d]);
    } else if ((opcode & 0xF000) == 0x7000) {
        // ANDI Rd, K
        uint8_t d = 16 + ((opcode >> 4) & 0x0F);
        uint8_t K = ((opcode >> 4) & 0xF0) | (opcode & 0x0F);
        m_registers[d] = m_registers[d] & K;
        update_flags_logic(m_sreg, m_registers[d]);
    } else if ((opcode & 0xFE0F) == 0x8200) {
        // ST Z, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t z_ptr = m_registers[30] | (m_registers[31] << 8);
        sramWrite(z_ptr, m_registers[r]);
    } else if ((opcode & 0xD208) == 0x8200) {
        // STD Z+q, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint8_t q = (opcode & 0x07) | ((opcode >> 7) & 0x18) | ((opcode >> 8) & 0x20);
        uint16_t z_ptr = m_registers[30] | (m_registers[31] << 8);
        sramWrite(z_ptr + q, m_registers[r]);
    } else if ((opcode & 0xD208) == 0x8000) {
        // LDD Rd, Z+q
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t q = (opcode & 0x07) | ((opcode >> 7) & 0x18) | ((opcode >> 8) & 0x20);
        uint16_t z_ptr = m_registers[30] | (m_registers[31] << 8);
        m_registers[d] = sramRead(z_ptr + q);
    } else if ((opcode & 0xD208) == 0x8008) {
        // LDD Rd, Y+q
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t q = (opcode & 0x07) | ((opcode >> 7) & 0x18) | ((opcode >> 8) & 0x20);
        uint16_t y_ptr = m_registers[28] | (m_registers[29] << 8);
        m_registers[d] = sramRead(y_ptr + q);
    } else if ((opcode & 0xD208) == 0x8208) {
        // STD Y+q, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint8_t q = (opcode & 0x07) | ((opcode >> 7) & 0x18) | ((opcode >> 8) & 0x20);
        uint16_t y_ptr = m_registers[28] | (m_registers[29] << 8);
        sramWrite(y_ptr + q, m_registers[r]);
    } else if ((opcode & 0xFE0F) == 0x8008) {
        // LD Rd, Y
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[28] | (m_registers[29] << 8);
        m_registers[d] = sramRead(ptr);
    } else if ((opcode & 0xFE0F) == 0x8208) {
        // ST Y, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[28] | (m_registers[29] << 8);
        sramWrite(ptr, m_registers[r]);
    } else if (opcode == 0x9588) {
        // SLEEP
    } else if (opcode == 0x9518) {
        // RETI
        uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8);
        sp++; uint8_t low_byte = sramRead(sp);
        sp++; uint8_t high_byte = sramRead(sp);
        m_pc = (high_byte << 8) | low_byte;
        writeIO(0x3D, sp & 0xFF); writeIO(0x3E, sp >> 8);
        m_sreg |= 0x80;
    } else if (opcode == 0x9508) {
        // RET
        uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8);
        sp++; uint8_t low_byte = sramRead(sp);
        sp++; uint8_t high_byte = sramRead(sp);
        m_pc = (high_byte << 8) | low_byte;
        writeIO(0x3D, sp & 0xFF); writeIO(0x3E, sp >> 8);
    } else if ((opcode & 0xFE0E) == 0x940C) {
        // JMP
        uint16_t next_word = m_avrFlash[m_pc * 2] | (m_avrFlash[(m_pc * 2) + 1] << 8);
        m_pc = next_word;
    } else if ((opcode & 0xFE0E) == 0x940E) {
        // CALL
        uint16_t next_word = m_avrFlash[m_pc * 2] | (m_avrFlash[(m_pc * 2) + 1] << 8);
        uint16_t ret_addr = m_pc + 1;
        uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8);
        sramWrite(sp--, (ret_addr >> 8) & 0xFF);
        sramWrite(sp--, ret_addr & 0xFF);
        writeIO(0x3D, sp & 0xFF); writeIO(0x3E, sp >> 8);
        m_pc = next_word;
    } else if ((opcode & 0xFE0F) == 0x940A) {
        // DEC
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t res = m_registers[d] - 1;
        bool V = (m_registers[d] == 0x80);
        bool N = res & 0x80;
        bool S = N ^ V;
        bool Z = (res == 0);
        m_sreg = (m_sreg & 0xE1) | (S ? 0x10 : 0) | (V ? 0x08 : 0) | (N ? 0x04 : 0) | (Z ? 0x02 : 0);
        m_registers[d] = res;
    } else if ((opcode & 0xFE0F) == 0x9001) {
        // LD Rd, Z+
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t z_ptr = m_registers[30] | (m_registers[31] << 8);
        m_registers[d] = sramRead(z_ptr);
        z_ptr++;
        m_registers[30] = z_ptr & 0xFF; m_registers[31] = (z_ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x9004) {
        // LPM Rd, Z
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t z_ptr = m_registers[30] | (m_registers[31] << 8);
        m_registers[d] = m_avrFlash[z_ptr];
    } else if ((opcode & 0xFE0F) == 0x9005) {
        // LPM Rd, Z+
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t z_ptr = m_registers[30] | (m_registers[31] << 8);
        m_registers[d] = m_avrFlash[z_ptr];
        z_ptr++;
        m_registers[30] = z_ptr & 0xFF; m_registers[31] = (z_ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x9403) {
        // INC
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t res = m_registers[d] + 1;
        bool V = (m_registers[d] == 0x7F);
        bool N = res & 0x80;
        bool S = N ^ V;
        bool Z = (res == 0);
        m_sreg = (m_sreg & 0xE1) | (S ? 0x10 : 0) | (V ? 0x08 : 0) | (N ? 0x04 : 0) | (Z ? 0x02 : 0);
        m_registers[d] = res;
    } else if ((opcode & 0xFE0F) == 0x920F) {
        // PUSH
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8);
        sramWrite(sp--, m_registers[r]);
        writeIO(0x3D, sp & 0xFF); writeIO(0x3E, sp >> 8);
    } else if ((opcode & 0xFE0F) == 0x900F) {
        // POP
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8);
        m_registers[d] = sramRead(++sp);
        writeIO(0x3D, sp & 0xFF); writeIO(0x3E, sp >> 8);
    } else if ((opcode & 0xFE0F) == 0x9201) {
        // ST Z+, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t z_ptr = m_registers[30] | (m_registers[31] << 8);
        sramWrite(z_ptr++, m_registers[r]);
        m_registers[30] = z_ptr & 0xFF; m_registers[31] = (z_ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x9202) {
        // ST -Z, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t z_ptr = m_registers[30] | (m_registers[31] << 8);
        sramWrite(--z_ptr, m_registers[r]);
        m_registers[30] = z_ptr & 0xFF; m_registers[31] = (z_ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x9000) {
        // LDS Rd, k
        uint16_t k = m_avrFlash[m_pc * 2] | (m_avrFlash[(m_pc * 2) + 1] << 8);
        m_pc++;
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d] = sramRead(k);
    } else if ((opcode & 0xFE0F) == 0x9200) {
        // STS k, Rr
        uint16_t k = m_avrFlash[m_pc * 2] | (m_avrFlash[(m_pc * 2) + 1] << 8);
        m_pc++;
        uint8_t r = (opcode >> 4) & 0x1F;
        sramWrite(k, m_registers[r]);
        
        // Hardware intercept for ADC
        if (k == 0x007A) {
            if (sramRead(k) & (1 << 6)) {
                sramWrite(k, sramRead(k) & ~(1 << 6));
                sramWrite(k, sramRead(k) | (1 << 4));
                m_sram[0x0078] = 0x50; // Mock ADCL
                m_sram[0x0079] = 0x02; // Mock ADCH
            }
        }
    } else if ((opcode & 0xFE0F) == 0x900C) {
        // LD Rd, X
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[26] | (m_registers[27] << 8);
        m_registers[d] = sramRead(ptr);
    } else if ((opcode & 0xFE0F) == 0x900D) {
        // LD Rd, X+
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[26] | (m_registers[27] << 8);
        m_registers[d] = sramRead(ptr++);
        m_registers[26] = ptr & 0xFF; m_registers[27] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x900E) {
        // LD Rd, -X
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[26] | (m_registers[27] << 8);
        m_registers[d] = sramRead(--ptr);
        m_registers[26] = ptr & 0xFF; m_registers[27] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x920C) {
        // ST X, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[26] | (m_registers[27] << 8);
        sramWrite(ptr, m_registers[r]);
    } else if ((opcode & 0xFE0F) == 0x920D) {
        // ST X+, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[26] | (m_registers[27] << 8);
        sramWrite(ptr++, m_registers[r]);
        m_registers[26] = ptr & 0xFF; m_registers[27] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x920E) {
        // ST -X, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[26] | (m_registers[27] << 8);
        sramWrite(--ptr, m_registers[r]);
        m_registers[26] = ptr & 0xFF; m_registers[27] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x9009) {
        // LD Rd, Y+
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[28] | (m_registers[29] << 8);
        m_registers[d] = sramRead(ptr++);
        m_registers[28] = ptr & 0xFF; m_registers[29] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x900A) {
        // LD Rd, -Y
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[28] | (m_registers[29] << 8);
        m_registers[d] = sramRead(--ptr);
        m_registers[28] = ptr & 0xFF; m_registers[29] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x9209) {
        // ST Y+, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[28] | (m_registers[29] << 8);
        sramWrite(ptr++, m_registers[r]);
        m_registers[28] = ptr & 0xFF; m_registers[29] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x920A) {
        // ST -Y, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[28] | (m_registers[29] << 8);
        sramWrite(--ptr, m_registers[r]);
        m_registers[28] = ptr & 0xFF; m_registers[29] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFF00) == 0x9A00) {
        // SBI A, b
        uint8_t A = (opcode >> 3) & 0x1F;
        uint8_t b = opcode & 0x07;
        writeIO(A, readIO(A) | (1 << b));
    } else if ((opcode & 0xFF00) == 0x9800) {
        // CBI A, b
        uint8_t A = (opcode >> 3) & 0x1F;
        uint8_t b = opcode & 0x07;
        writeIO(A, readIO(A) & ~(1 << b));
    } else if ((opcode & 0xFF00) == 0x9B00) {
        // SBIS A, b
        uint8_t A = (opcode >> 3) & 0x1F;
        uint8_t b = opcode & 0x07;
        if (readIO(A) & (1 << b)) skip_next();
    } else if ((opcode & 0xFF00) == 0x9900) {
        // SBIC A, b
        uint8_t A = (opcode >> 3) & 0x1F;
        uint8_t b = opcode & 0x07;
        if (!(readIO(A) & (1 << b))) skip_next();
    } else if ((opcode & 0xFF08) == 0x9408) {
        // BSET s
        uint8_t s = (opcode >> 4) & 0x07;
        m_sreg |= (1 << s);
    } else if ((opcode & 0xFF08) == 0x9488) {
        // BCLR s
        uint8_t s = (opcode >> 4) & 0x07;
        m_sreg &= ~(1 << s);
    } else if ((opcode & 0xFF00) == 0x9600) {
        // ADIW
        uint8_t d = 24 + ((opcode >> 4) & 0x03) * 2;
        uint8_t K = (opcode & 0x0F) | ((opcode >> 2) & 0x30);
        uint16_t word = m_registers[d] | (m_registers[d+1] << 8);
        uint16_t res = word + K;
        m_registers[d] = res & 0xFF; m_registers[d+1] = res >> 8;
        
        bool word15 = word & 0x8000;
        bool res15 = res & 0x8000;
        if (!word15 && res15) m_sreg |= 0x08; else m_sreg &= ~0x08; // V
        if (!res15 && word15) m_sreg |= 0x01; else m_sreg &= ~0x01; // C
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Z
        if (res15) m_sreg |= 0x04; else m_sreg &= ~0x04; // N
        if (((m_sreg & 0x04) >> 2) ^ ((m_sreg & 0x08) >> 3)) m_sreg |= 0x10; else m_sreg &= ~0x10; // S
    } else if ((opcode & 0xFF00) == 0x9700) {
        // SBIW
        uint8_t d = 24 + ((opcode >> 4) & 0x03) * 2;
        uint8_t K = (opcode & 0x0F) | ((opcode >> 2) & 0x30);
        uint16_t word = m_registers[d] | (m_registers[d+1] << 8);
        uint16_t res = word - K;
        m_registers[d] = res & 0xFF; m_registers[d+1] = res >> 8;
        
        bool word15 = word & 0x8000;
        bool res15 = res & 0x8000;
        if (word15 && !res15) m_sreg |= 0x08; else m_sreg &= ~0x08; // V
        if (res15 && !word15) m_sreg |= 0x01; else m_sreg &= ~0x01; // C
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Z
        if (res15) m_sreg |= 0x04; else m_sreg &= ~0x04; // N
        if (((m_sreg & 0x04) >> 2) ^ ((m_sreg & 0x08) >> 3)) m_sreg |= 0x10; else m_sreg &= ~0x10; // S
    } else if ((opcode & 0xFE0F) == 0x9406) {
        // LSR Rd
        uint8_t d = (opcode >> 4) & 0x1F;
        if (m_registers[d] & 0x01) m_sreg |= 0x01; else m_sreg &= ~0x01; // C
        m_registers[d] >>= 1;
        if (m_registers[d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Z
    } else if ((opcode & 0xFE0F) == 0x9405) {
        // ASR Rd
        uint8_t d = (opcode >> 4) & 0x1F;
        if (m_registers[d] & 0x01) m_sreg |= 0x01; else m_sreg &= ~0x01; // C
        m_registers[d] = (m_registers[d] & 0x80) | (m_registers[d] >> 1);
        if (m_registers[d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Z
    } else if ((opcode & 0xFE0F) == 0x9407) {
        // ROR Rd
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t old_c = (m_sreg & 0x01) ? 0x80 : 0x00;
        if (m_registers[d] & 0x01) m_sreg |= 0x01; else m_sreg &= ~0x01; // C
        m_registers[d] = old_c | (m_registers[d] >> 1);
        if (m_registers[d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Z
    } else if ((opcode & 0xFC00) == 0x9C00) {
        // MUL Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t res = (uint16_t)m_registers[d] * m_registers[r];
        m_registers[0] = res & 0xFF;
        m_registers[1] = (res >> 8) & 0xFF;
        if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01; // Carry
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Zero
    } else if ((opcode & 0xFE0F) == 0x9402) {
        // SWAP Rd
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d] = (m_registers[d] >> 4) | (m_registers[d] << 4);
    } else if ((opcode & 0xFE0F) == 0x9400) {
        // COM Rd
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d] = 0xFF - m_registers[d];
        m_sreg |= 0x01; // C = 1
        if (m_registers[d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Z
        if (m_registers[d] & 0x80) m_sreg |= 0x04; else m_sreg &= ~0x04; // N
        m_sreg &= ~0x08; // V = 0
        if (m_sreg & 0x04) m_sreg |= 0x10; else m_sreg &= ~0x10; // S = N ^ V = N
    } else if ((opcode & 0xFE0F) == 0x9401) {
        // NEG Rd
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t res = -m_registers[d];
        if (res != 0) m_sreg |= 0x01; else m_sreg &= ~0x01; // C
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Z
        if (res & 0x80) m_sreg |= 0x04; else m_sreg &= ~0x04; // N
        if (res == 0x80) m_sreg |= 0x08; else m_sreg &= ~0x08; // V
        if (((m_sreg & 0x04) >> 2) ^ ((m_sreg & 0x08) >> 3)) m_sreg |= 0x10; else m_sreg &= ~0x10; // S
        m_registers[d] = res;
    } else if ((opcode & 0xF800) == 0xB800) {
        // OUT A, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint8_t A = (opcode & 0x0F) | ((opcode >> 5) & 0x30);
        writeIO(A, m_registers[r]);
    } else if ((opcode & 0xF800) == 0xB000) {
        // IN Rd, A
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t A = (opcode & 0x0F) | ((opcode >> 5) & 0x30);
        m_registers[d] = readIO(A);
    } else if ((opcode & 0xF000) == 0xC000) {
        // RJMP
        int16_t offset = opcode & 0x0FFF;
        if (offset & 0x0800) offset |= 0xF000;
        m_pc += offset;
    } else if ((opcode & 0xF000) == 0xD000) {
        // RCALL
        int16_t offset = opcode & 0x0FFF;
        if (offset & 0x0800) offset |= 0xF000;
        uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8);
        sramWrite(sp--, (m_pc >> 8) & 0xFF);
        sramWrite(sp--, m_pc & 0xFF);
        writeIO(0x3D, sp & 0xFF); writeIO(0x3E, sp >> 8);
        m_pc += offset;
    } else if ((opcode & 0xF000) == 0xE000) {
        // LDI Rd, K
        uint8_t d = ((opcode >> 4) & 0x0F) + 16;
        uint8_t k = (opcode & 0x0F) | ((opcode >> 4) & 0xF0);
        m_registers[d] = k;
    } else if ((opcode & 0xFE08) == 0xFE00) {
        // SBRS Rr, b
        uint8_t r = (opcode >> 4) & 0x1F;
        uint8_t b = opcode & 0x07;
        if (m_registers[r] & (1 << b)) skip_next();
    } else if ((opcode & 0xFE08) == 0xFC00) {
        // SBRC Rr, b
        uint8_t r = (opcode >> 4) & 0x1F;
        uint8_t b = opcode & 0x07;
        if (!(m_registers[r] & (1 << b))) skip_next();
    } else if ((opcode & 0xFC00) == 0xF000) {
        // BRBS s, k
        uint8_t s = opcode & 0x07;
        int16_t offset = (opcode >> 3) & 0x7F;
        if (offset & 0x40) offset |= 0xFF80;
        if (m_sreg & (1 << s)) m_pc += offset;
    } else if ((opcode & 0xFC00) == 0xF400) {
        // BRBC s, k
        uint8_t s = opcode & 0x07;
        int16_t offset = (opcode >> 3) & 0x7F;
        if (offset & 0x40) offset |= 0xFF80;
        if (!(m_sreg & (1 << s))) m_pc += offset;
    } else if ((opcode & 0xFA08) == 0xFA00) {
        // BST Rd, b
        uint8_t b = opcode & 0x07;
        uint8_t d = (opcode >> 4) & 0x1F;
        if (m_registers[d] & (1 << b)) m_sreg |= 0x40; else m_sreg &= ~0x40;
    } else if ((opcode & 0xF808) == 0xF800) {
        // BLD Rd, b
        uint8_t b = opcode & 0x07;
        uint8_t d = (opcode >> 4) & 0x1F;
        if (m_sreg & 0x40) m_registers[d] |= (1 << b); else m_registers[d] &= ~(1 << b);
    } else if (opcode == 0x95E8) {
        // SPM (Skip)
    } else {
        // Fallback for missing/unhandled opcodes
        static uint16_t last_unhandled = 0xFFFF;
        if (opcode != last_unhandled) {
            ESP_LOGW(TAG, "UNHANDLED OPCODE: 0x%04X at PC: 0x%04X (NOP'd)", opcode, m_pc - 1);
            last_unhandled = opcode;
        }
    }
}

void EmulatorState::onUpdate() {
    if (InputManager::getInstance().justPressed(5)) { // BTN_B
        ESP_LOGW(TAG, "EXIT: B button pressed");
        m_isRunning = false;
        StateManager::getInstance().changeState(&MenuState::getInstance());
        return;
    }

    if (!m_isRunning) {
        static bool logged_not_running = false;
        if (!logged_not_running) {
            ESP_LOGW(TAG, "onUpdate called but m_isRunning is FALSE");
            logged_not_running = true;
        }
        return;
    }

    static bool first_frame = true;
    if (first_frame) {
        ESP_LOGI(TAG, "CPU LOOP ACTIVE | m_pc: 0x%04X | m_isRunning: %d", m_pc, m_isRunning);
        first_frame = false;
    }

    // Time-Warping Engine: ~25,000 instructions per 33ms FreeRTOS tick
    for (int i = 0; i < 25000; i++) {
        
        // Cycle-accurate Timer0 Approximation
        if ((i % 42) == 0) {
            uint8_t tcnt0 = sramRead(0x46);
            sramWrite(0x46, tcnt0 + 1);
            if (tcnt0 == 0xFF) { 
                sramWrite(0x35, sramRead(0x35) | 0x01); // Set TOV0 flag in TIFR0
            }
        }

        // Hardware Interrupt Dispatcher
        bool global_int_enabled = (m_sreg & 0x80) != 0;
        bool timer0_ovf_enabled = (sramRead(0x6E) & 0x01) != 0; // TIMSK0
        bool timer0_ovf_flag = (sramRead(0x35) & 0x01) != 0;    // TIFR0

        if (global_int_enabled && timer0_ovf_enabled && timer0_ovf_flag) {
            sramWrite(0x35, sramRead(0x35) & ~0x01); // Clear TOV0
            m_sreg &= ~0x80; // Disable Global Interrupts
            
            uint16_t sp = sramRead(0x3D + 0x20) | (sramRead(0x3E + 0x20) << 8);
            sramWrite(sp--, (m_pc >> 8) & 0xFF);
            sramWrite(sp--, m_pc & 0xFF);
            
            writeIO(0x3D, sp & 0xFF);
            writeIO(0x3E, sp >> 8);
            
            m_pc = 0x002E; // Jump to TIMER0_OVF vector
        }

        // Fetch
        uint16_t opcode = m_avrFlash[m_pc * 2] | (m_avrFlash[(m_pc * 2) + 1] << 8);
        m_pc++;
        
        // Execute
        executeOpcode(opcode);

        if (!m_isRunning) {
            ESP_LOGW(TAG, "EXIT: m_isRunning went false at PC: 0x%04X", m_pc);
            break;
        }
    }

    // Telemetry Heartbeat (~ Every 1 second if onUpdate fires every 33ms)
    static int frameCount = 0;
    frameCount++;
    if (frameCount % 30 == 0) { 
        ESP_LOGI(TAG, "Heartbeat | PC: %04X | SREG: %02X | Data: %lu | Cmd: %lu | BufIdx: %u", 
                 m_pc, m_sreg, spi_data_bytes, spi_cmd_bytes, m_displayIndex);
    }

    // Feed the FreeRTOS Task Watchdog
    vTaskDelay(1);
}

void EmulatorState::onDraw() {
    // Left intentionally blank per previous configuration
}

void EmulatorState::onExit() {
    ESP_LOGI(TAG, "Exiting Emulator State...");
}