#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>

#define ESP_LOGE(tag, format, ...) printf(format "\n", ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) printf(format "\n", ##__VA_ARGS__)
#define TAG "MOCK"

struct DisplayManager {
    static DisplayManager& getInstance() { static DisplayManager d; return d; }
    void drawText(int x, int y, const char* t) {}
    void renderPipelinePush() {}
    void drawArduboyFrame(const uint8_t* b) {}
    void clearBuffer() {}
};

struct InputManager {
    static InputManager& getInstance() { static InputManager i; return i; }
    bool justPressed(int b) { return false; }
    bool isPressed(int b) { return false; }
    bool isHeld(int b) { return false; }
};

#define IRAM_ATTR

namespace StateMachine {
    class State {
    public:
        virtual ~State() {}
    };
}
using namespace StateMachine;

class EmulatorState : public State {
public:
    EmulatorState(const std::string& romPath);
    void onEnter();
    void onUpdate();
    void onDraw() {}
    void onExit() {}
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
    void skip_next();
};

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
    m_sram[io_addr + 0x20] = value;
    
    // Intercept SPI writes to SPDR
    if (io_addr == 0x2F) {
        // Break the SPI Deadlock by instantly asserting the SPIF bit in SPSR (0x2D)
        m_sram[0x2D + 0x20] |= 0x80;
        
        // Check PORTD (0x0B) Bit 4 to distinguish Data vs Command
        bool isData = (m_sram[0x0B + 0x20] & 0x10) != 0;
        
        if (isData) {
            spi_data_bytes++;
            m_displayBuffer[m_displayIndex++] = value;
            if (m_displayIndex == 1024) {
                ESP_LOGI(TAG, ">>> PUSHING FULL 1024-BYTE ARDUBOY FRAME TO OLED <<<");
                DisplayManager::getInstance().drawArduboyFrame(m_displayBuffer);
                m_displayIndex = 0;
            }
        } else {
            spi_cmd_bytes++;
        }
    }
}

uint8_t EmulatorState::readIO(uint8_t io_addr) {
    if (io_addr == 0x00) { // PINF (D-Pad)
        uint8_t val = 0xFF;
        if (InputManager::getInstance().isHeld(0)) val &= ~(1 << 7); // UP
        if (InputManager::getInstance().isHeld(1)) val &= ~(1 << 6); // DOWN
        if (InputManager::getInstance().isHeld(2)) val &= ~(1 << 5); // LEFT
        if (InputManager::getInstance().isHeld(3)) val &= ~(1 << 4); // RIGHT
        return val;
    } else if (io_addr == 0x0C) { // PINE (A Button)
        uint8_t val = 0xFF;
        if (InputManager::getInstance().isHeld(4)) val &= ~(1 << 6); // A
        return val;
    } else if (io_addr == 0x03) { // PINB (B Button)
        uint8_t val = 0xFF;
        if (InputManager::getInstance().isHeld(5)) val &= ~(1 << 4); // B
        return val;
    }
    
    // --- Hardware Auto-Completer ---
    
    // USB PLL Lock: Arduino init waits for PLOCK (Bit 0)
    if (io_addr == 0x29) return m_sram[0x29 + 0x20] | 0x01;
    
    // SPI Status: Ensure SPIF (Bit 7) reads as complete
    if (io_addr == 0x2D) return m_sram[0x2D + 0x20] | 0x80;
    
    // EEPROM Control: Wait for EEPE (Bit 1) to clear
    if (io_addr == 0x1F) return m_sram[0x1F + 0x20] & ~0x02;
    
    return m_sram[io_addr + 0x20];
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
        // NOP - Do nothing
    } else if (opcode == 0x9588) {
        // SLEEP (Idle Mode)
        // Do nothing, but technically the CPU halts until an interrupt fires.
        // Since our onUpdate loop is governed by FreeRTOS,
        // we can just proceed to the next fetch.
    } else if (opcode == 0x9518) {
        // RETI (Return from Interrupt)
        uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8);
        
        sp++;
        uint8_t low_byte = m_sram[sp];
        sp++;
        uint8_t high_byte = m_sram[sp];
        
        m_pc = (high_byte << 8) | low_byte;
        
        writeIO(0x3D, sp & 0xFF);
        writeIO(0x3E, sp >> 8);
        
        m_sreg |= 0x80; // Re-enable Global Interrupts
    } else if (opcode == 0x9508) {
        // RET (Return)
        uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8);
        
        sp++;
        uint8_t low_byte = m_sram[sp];
        sp++;
        uint8_t high_byte = m_sram[sp];
        
        m_pc = (high_byte << 8) | low_byte;
        
        writeIO(0x3D, sp & 0xFF);
        writeIO(0x3E, sp >> 8);
    } else if ((opcode & 0xF000) == 0xC000) {
        // RJMP (Relative Jump)
        int16_t offset = opcode & 0x0FFF;
        // Sign extend the 12-bit offset
        if (offset & 0x0800) {
            offset |= 0xF000;
        }
        m_pc += offset;
    } else if ((opcode & 0xF000) == 0xE000) {
        // LDI Rd, K
        uint8_t d = ((opcode >> 4) & 0x0F) + 16;
        uint8_t k = (opcode & 0x0F) | ((opcode >> 4) & 0xF0);
        m_registers[d] = k;
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
    } else if ((opcode & 0xF000) == 0xD000) {
        // RCALL k
        int16_t offset = opcode & 0x0FFF;
        if (offset & 0x0800) {
            offset |= 0xF000;
        }
        
        uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8);
        
        m_sram[sp] = (m_pc >> 8) & 0xFF; // High byte
        sp--;
        m_sram[sp] = m_pc & 0xFF; // Low byte
        sp--;
        
        writeIO(0x3D, sp & 0xFF);
        writeIO(0x3E, sp >> 8);
        
        m_pc += offset;
    } else if ((opcode & 0xFE0E) == 0x940C) {
        // JMP (Absolute Jump, 32-bit)
        uint16_t next_word = m_avrFlash[m_pc * 2] | (m_avrFlash[(m_pc * 2) + 1] << 8);
        m_pc = next_word;
    } else if ((opcode & 0xFE0E) == 0x940E) {
        // CALL (Absolute Call, 32-bit)
        uint16_t next_word = m_avrFlash[m_pc * 2] | (m_avrFlash[(m_pc * 2) + 1] << 8);
        uint16_t ret_addr = m_pc + 1; // Instruction after the 32-bit CALL
        
        uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8);
        
        m_sram[sp] = (ret_addr >> 8) & 0xFF; // High byte
        sp--;
        m_sram[sp] = ret_addr & 0xFF; // Low byte
        sp--;
        
        writeIO(0x3D, sp & 0xFF);
        writeIO(0x3E, sp >> 8);
        
        m_pc = next_word;
    } else if ((opcode & 0xF000) == 0x3000) {
        // CPI Rd, K
        uint8_t d = ((opcode >> 4) & 0x0F) + 16;
        uint8_t k = (opcode & 0x0F) | ((opcode >> 4) & 0xF0);
        uint8_t res = m_registers[d] - k;
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02;
    } else if ((opcode & 0xFC00) == 0x1400) {
        // CP Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t res = m_registers[d] - m_registers[r];
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02;
    } else if ((opcode & 0xFC00) == 0x0400) {
        // CPC Rd, Rr (Compare with Carry)
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        int16_t res = (int16_t)m_registers[d] - m_registers[r] - (m_sreg & 0x01);
        if (res < 0) m_sreg |= 0x01; else m_sreg &= ~0x01; // Carry
        if ((res & 0xFF) != 0) m_sreg &= ~0x02; // Zero (Cleared if non-zero, untouched otherwise)
    } else if ((opcode & 0xFC07) == 0xF401) {
        // BRNE k (Replaced by unified BRBC, but kept for legacy fallback if needed, actually let's safely remove it since BRBC covers it perfectly)
        // Wait, BRNE is BRBC 1, which matches 0xF400 mask. I will remove the old BRNE below to avoid conflicts.
    } else if ((opcode & 0xFE0F) == 0x940A) {
        // DEC Rd
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d]--;
        uint8_t res = m_registers[d];
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02;
    } else if ((opcode & 0xFE0F) == 0x9001) {
        // LD Rd, Z+
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t z_ptr = m_registers[30] | (m_registers[31] << 8);
        m_registers[d] = m_sram[z_ptr];
        z_ptr++;
        m_registers[30] = z_ptr & 0xFF;
        m_registers[31] = (z_ptr >> 8) & 0xFF;
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
        m_registers[30] = z_ptr & 0xFF;
        m_registers[31] = (z_ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFC00) == 0x0C00) {
        // ADD Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t res = (uint16_t)m_registers[d] + m_registers[r];
        if (res > 255) m_sreg |= 0x01; else m_sreg &= ~0x01; // Carry
        m_registers[d] = res & 0xFF;
        if (m_registers[d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Zero
    } else if ((opcode & 0xFC00) == 0x1C00) {
        // ADC Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t c = (m_sreg & 0x01) ? 1 : 0;
        uint16_t res = (uint16_t)m_registers[d] + m_registers[r] + c;
        if (res > 255) m_sreg |= 0x01; else m_sreg &= ~0x01; // Carry
        m_registers[d] = res & 0xFF;
        if (m_registers[d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Zero
    } else if ((opcode & 0xFC00) == 0x1800) {
        // SUB Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t res = (uint16_t)m_registers[d] - m_registers[r];
        if (m_registers[d] < m_registers[r]) m_sreg |= 0x01; else m_sreg &= ~0x01; // Carry (Borrow)
        m_registers[d] = res & 0xFF;
        if (m_registers[d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Zero
    } else if ((opcode & 0xFC00) == 0x0800) {
        // SBC Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t c = (m_sreg & 0x01) ? 1 : 0;
        uint16_t res = (uint16_t)m_registers[d] - m_registers[r] - c;
        if (res > 255) m_sreg |= 0x01; else m_sreg &= ~0x01; // Carry (Borrow underflow)
        m_registers[d] = res & 0xFF;
        if (m_registers[d] != 0) m_sreg &= ~0x02; // Zero (Cleared if non-zero, unaltered otherwise)
    } else if ((opcode & 0xFC00) == 0x2000) {
        // AND Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d] = m_registers[d] & m_registers[r];
        if (m_registers[d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02;
    } else if ((opcode & 0xFC00) == 0x2800) {
        // OR Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d] = m_registers[d] | m_registers[r];
        if (m_registers[d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02;
    } else if ((opcode & 0xFC00) == 0x2400) {
        // EOR Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d] = m_registers[d] ^ m_registers[r];
        if (m_registers[d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02;
    } else if ((opcode & 0xF000) == 0x5000) {
        // SUBI Rd, K
        uint8_t d = 16 + ((opcode >> 4) & 0x0F);
        uint8_t K = ((opcode >> 4) & 0xF0) | (opcode & 0x0F);
        int16_t res = m_registers[d] - K;
        if (res < 0) m_sreg |= 0x01; else m_sreg &= ~0x01; // Carry
        m_registers[d] = res & 0xFF;
        if (m_registers[d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Zero
    } else if ((opcode & 0xF000) == 0x4000) {
        // SBCI Rd, K
        uint8_t d = 16 + ((opcode >> 4) & 0x0F);
        uint8_t K = ((opcode >> 4) & 0xF0) | (opcode & 0x0F);
        int16_t res = m_registers[d] - K - (m_sreg & 0x01);
        if (res < 0) m_sreg |= 0x01; else m_sreg &= ~0x01; // Carry
        m_registers[d] = res & 0xFF;
        if (m_registers[d] != 0) m_sreg &= ~0x02; // Z is cleared if non-zero, unchanged if zero
    } else if ((opcode & 0xF000) == 0x7000) {
        // ANDI Rd, K
        uint8_t d = 16 + ((opcode >> 4) & 0x0F);
        uint8_t K = ((opcode >> 4) & 0xF0) | (opcode & 0x0F);
        m_registers[d] = m_registers[d] & K;
        if (m_registers[d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Zero
    } else if ((opcode & 0xF000) == 0x6000) {
        // ORI Rd, K
        uint8_t d = 16 + ((opcode >> 4) & 0x0F);
        uint8_t K = ((opcode >> 4) & 0xF0) | (opcode & 0x0F);
        m_registers[d] = m_registers[d] | K;
        if (m_registers[d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Zero
    } else if ((opcode & 0xFE0F) == 0x9403) {
        // INC Rd
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d]++;
        if (m_registers[d] == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Zero
    } else if ((opcode & 0xFC00) == 0x2C00) {
        // MOV Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d] = m_registers[r];
    } else if ((opcode & 0xFF00) == 0x0100) {
        // MOVW Rd, Rr
        uint8_t r = (opcode & 0x0F) * 2;
        uint8_t d = ((opcode >> 4) & 0x0F) * 2;
        m_registers[d] = m_registers[r];
        m_registers[d+1] = m_registers[r+1];
    } else if ((opcode & 0xFE0F) == 0x920F) {
        // PUSH Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8);
        m_sram[sp] = m_registers[r];
        sp--;
        writeIO(0x3D, sp & 0xFF);
        writeIO(0x3E, sp >> 8);
    } else if ((opcode & 0xFE0F) == 0x900F) {
        // POP Rd
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t sp = readIO(0x3D) | (readIO(0x3E) << 8);
        sp++;
        m_registers[d] = m_sram[sp];
        writeIO(0x3D, sp & 0xFF);
        writeIO(0x3E, sp >> 8);
    } else if ((opcode & 0xFE0F) == 0x8200) {
        // ST Z, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t z_ptr = m_registers[30] | (m_registers[31] << 8);
        m_sram[z_ptr] = m_registers[r];
    } else if ((opcode & 0xD208) == 0x8200) {
        // STD Z+q, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint8_t q = (opcode & 0x07) | ((opcode >> 7) & 0x18) | ((opcode >> 8) & 0x20);
        uint16_t z_ptr = m_registers[30] | (m_registers[31] << 8);
        m_sram[z_ptr + q] = m_registers[r];
    } else if ((opcode & 0xD208) == 0x8000) {
        // LDD Rd, Z+q
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t q = (opcode & 0x07) | ((opcode >> 7) & 0x18) | ((opcode >> 8) & 0x20);
        uint16_t z_ptr = m_registers[30] | (m_registers[31] << 8);
        m_registers[d] = m_sram[z_ptr + q];
    } else if ((opcode & 0xD208) == 0x8008) {
        // LDD Rd, Y+q
        uint8_t d = (opcode >> 4) & 0x1F;
        uint8_t q = (opcode & 0x07) | ((opcode >> 7) & 0x18) | ((opcode >> 8) & 0x20);
        uint16_t y_ptr = m_registers[28] | (m_registers[29] << 8);
        m_registers[d] = m_sram[y_ptr + q];
    } else if ((opcode & 0xD208) == 0x8208) {
        // STD Y+q, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint8_t q = (opcode & 0x07) | ((opcode >> 7) & 0x18) | ((opcode >> 8) & 0x20);
        uint16_t y_ptr = m_registers[28] | (m_registers[29] << 8);
        m_sram[y_ptr + q] = m_registers[r];
    } else if ((opcode & 0xFE0F) == 0x9201) {
        // ST Z+, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t z_ptr = m_registers[30] | (m_registers[31] << 8);
        m_sram[z_ptr] = m_registers[r];
        z_ptr++;
        m_registers[30] = z_ptr & 0xFF;
        m_registers[31] = (z_ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x9202) {
        // ST -Z, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t z_ptr = m_registers[30] | (m_registers[31] << 8);
        z_ptr--;
        m_sram[z_ptr] = m_registers[r];
        m_registers[30] = z_ptr & 0xFF;
        m_registers[31] = (z_ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x9000) {
        // LDS Rd, k (Load Direct from Data Space, 32-bit)
        uint16_t k = m_avrFlash[m_pc * 2] | (m_avrFlash[(m_pc * 2) + 1] << 8);
        m_pc++; // Skip the 16-bit address word
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d] = m_sram[k];
    } else if ((opcode & 0xFE0F) == 0x9200) {
        // STS k, Rr (Store Direct to Data Space, 32-bit)
        uint16_t k = m_avrFlash[m_pc * 2] | (m_avrFlash[(m_pc * 2) + 1] << 8);
        m_pc++; // Skip the 16-bit address word
        uint8_t r = (opcode >> 4) & 0x1F;
        m_sram[k] = m_registers[r];
    } else if ((opcode & 0xFE0F) == 0x900C) {
        // LD Rd, X
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[26] | (m_registers[27] << 8);
        m_registers[d] = m_sram[ptr];
    } else if ((opcode & 0xFE0F) == 0x900D) {
        // LD Rd, X+
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[26] | (m_registers[27] << 8);
        m_registers[d] = m_sram[ptr];
        ptr++;
        m_registers[26] = ptr & 0xFF; m_registers[27] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x900E) {
        // LD Rd, -X
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[26] | (m_registers[27] << 8);
        ptr--;
        m_registers[d] = m_sram[ptr];
        m_registers[26] = ptr & 0xFF; m_registers[27] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x920C) {
        // ST X, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[26] | (m_registers[27] << 8);
        m_sram[ptr] = m_registers[r];
    } else if ((opcode & 0xFE0F) == 0x920D) {
        // ST X+, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[26] | (m_registers[27] << 8);
        m_sram[ptr] = m_registers[r];
        ptr++;
        m_registers[26] = ptr & 0xFF; m_registers[27] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x920E) {
        // ST -X, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[26] | (m_registers[27] << 8);
        ptr--;
        m_sram[ptr] = m_registers[r];
        m_registers[26] = ptr & 0xFF; m_registers[27] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x8008) {
        // LD Rd, Y
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[28] | (m_registers[29] << 8);
        m_registers[d] = m_sram[ptr];
    } else if ((opcode & 0xFE0F) == 0x9009) {
        // LD Rd, Y+
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[28] | (m_registers[29] << 8);
        m_registers[d] = m_sram[ptr];
        ptr++;
        m_registers[28] = ptr & 0xFF; m_registers[29] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x900A) {
        // LD Rd, -Y
        uint8_t d = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[28] | (m_registers[29] << 8);
        ptr--;
        m_registers[d] = m_sram[ptr];
        m_registers[28] = ptr & 0xFF; m_registers[29] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x8208) {
        // ST Y, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[28] | (m_registers[29] << 8);
        m_sram[ptr] = m_registers[r];
    } else if ((opcode & 0xFE0F) == 0x9209) {
        // ST Y+, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[28] | (m_registers[29] << 8);
        m_sram[ptr] = m_registers[r];
        ptr++;
        m_registers[28] = ptr & 0xFF; m_registers[29] = (ptr >> 8) & 0xFF;
    } else if ((opcode & 0xFE0F) == 0x920A) {
        // ST -Y, Rr
        uint8_t r = (opcode >> 4) & 0x1F;
        uint16_t ptr = m_registers[28] | (m_registers[29] << 8);
        ptr--;
        m_sram[ptr] = m_registers[r];
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
    } else if ((opcode & 0xFF08) == 0x9408) {
        // BSET s
        uint8_t s = (opcode >> 4) & 0x07;
        m_sreg |= (1 << s);
    } else if (opcode == 0x9580) {
        // Alias/Fallback for SEI (Global Interrupts) based on specific ROM telemetry
        m_sreg |= 0x80;
    } else if ((opcode & 0xFF08) == 0x9488) {
        // BCLR s
        uint8_t s = (opcode >> 4) & 0x07;
        m_sreg &= ~(1 << s);
    } else if ((opcode & 0xFF00) == 0x9600) {
        // ADIW Rd+1:Rd, K
        uint8_t d = 24 + ((opcode >> 4) & 0x03) * 2;
        uint8_t K = (opcode & 0x0F) | ((opcode >> 2) & 0x30);
        uint16_t word = m_registers[d] | (m_registers[d+1] << 8);
        uint16_t res = word + K;
        m_registers[d] = res & 0xFF; 
        m_registers[d+1] = res >> 8;
        
        bool word15 = word & 0x8000;
        bool res15 = res & 0x8000;
        if (!word15 && res15) m_sreg |= 0x08; else m_sreg &= ~0x08; // V
        if (!res15 && word15) m_sreg |= 0x01; else m_sreg &= ~0x01; // C
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Z
        if (res15) m_sreg |= 0x04; else m_sreg &= ~0x04; // N
        if (((m_sreg & 0x04) >> 2) ^ ((m_sreg & 0x08) >> 3)) m_sreg |= 0x10; else m_sreg &= ~0x10; // S
    } else if ((opcode & 0xFF00) == 0x9700) {
        // SBIW Rd+1:Rd, K
        uint8_t d = 24 + ((opcode >> 4) & 0x03) * 2;
        uint8_t K = (opcode & 0x0F) | ((opcode >> 2) & 0x30);
        uint16_t word = m_registers[d] | (m_registers[d+1] << 8);
        uint16_t res = word - K;
        m_registers[d] = res & 0xFF; 
        m_registers[d+1] = res >> 8;
        
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
    } else if ((opcode & 0xFC00) == 0x1000) {
        // CPSE Rd, Rr
        uint8_t r = ((opcode >> 5) & 0x10) | (opcode & 0x0F);
        uint8_t d = (opcode >> 4) & 0x1F;
        if (m_registers[d] == m_registers[r]) skip_next();
    } else if ((opcode & 0xFE0F) == 0x9402) {
        // SWAP Rd
        uint8_t d = (opcode >> 4) & 0x1F;
        m_registers[d] = (m_registers[d] >> 4) | (m_registers[d] << 4);
    } else if ((opcode & 0xFF00) == 0x0200) {
        // MULS Rd, Rr
        uint8_t d = 16 + ((opcode >> 4) & 0x0F);
        uint8_t r = 16 + (opcode & 0x0F);
        int16_t res = (int16_t)(int8_t)m_registers[d] * (int16_t)(int8_t)m_registers[r];
        m_registers[0] = res & 0xFF; m_registers[1] = (res >> 8) & 0xFF;
        if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01; // Carry = bit 15
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Z
    } else if ((opcode & 0xFF88) == 0x0300) {
        // MULSU Rd, Rr
        uint8_t d = 16 + ((opcode >> 4) & 0x07);
        uint8_t r = 16 + (opcode & 0x07);
        int16_t res = (int16_t)(int8_t)m_registers[d] * (uint16_t)m_registers[r];
        m_registers[0] = res & 0xFF; m_registers[1] = (res >> 8) & 0xFF;
        if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01; // Carry = bit 15
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Z
    } else if ((opcode & 0xFF88) == 0x0308) {
        // FMUL Rd, Rr
        uint8_t d = 16 + ((opcode >> 4) & 0x07);
        uint8_t r = 16 + (opcode & 0x07);
        uint16_t res = ((uint16_t)m_registers[d] * m_registers[r]) << 1;
        m_registers[0] = res & 0xFF; m_registers[1] = res >> 8;
        if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01; // Carry = bit 15
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Z
    } else if ((opcode & 0xFF88) == 0x0380) {
        // FMULS Rd, Rr
        uint8_t d = 16 + ((opcode >> 4) & 0x07);
        uint8_t r = 16 + (opcode & 0x07);
        int16_t res = ((int16_t)(int8_t)m_registers[d] * (int16_t)(int8_t)m_registers[r]) << 1;
        m_registers[0] = res & 0xFF; m_registers[1] = (res >> 8) & 0xFF;
        if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01; // Carry = bit 15
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Z
    } else if ((opcode & 0xFF88) == 0x0388) {
        // FMULSU Rd, Rr
        uint8_t d = 16 + ((opcode >> 4) & 0x07);
        uint8_t r = 16 + (opcode & 0x07);
        int16_t res = ((int16_t)(int8_t)m_registers[d] * (uint16_t)m_registers[r]) << 1;
        m_registers[0] = res & 0xFF; m_registers[1] = (res >> 8) & 0xFF;
        if (res & 0x8000) m_sreg |= 0x01; else m_sreg &= ~0x01; // Carry = bit 15
        if (res == 0) m_sreg |= 0x02; else m_sreg &= ~0x02; // Z
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
    } else if ((opcode & 0xFA08) == 0xFA00) {
        // BST Rd, b
        uint8_t b = opcode & 0x07;
        uint8_t d = (opcode >> 4) & 0x1F;
        if (m_registers[d] & (1 << b)) m_sreg |= 0x40; else m_sreg &= ~0x40; // T
    } else if ((opcode & 0xF808) == 0xF800) {
        // BLD Rd, b
        uint8_t b = opcode & 0x07;
        uint8_t d = (opcode >> 4) & 0x1F;
        if (m_sreg & 0x40) m_registers[d] |= (1 << b); else m_registers[d] &= ~(1 << b);
    } else if (opcode == 0x95A8) {
        // WDR
    } else if (opcode == 0x9598) {
        // BREAK
    } else if (opcode == 0x95E8) {
        // SPM
    } else {
        ESP_LOGE(TAG, "UNHANDLED OPCODE: 0x%04X at PC: 0x%04X", opcode, m_pc - 1);
        m_isRunning = false;
    }
}

void EmulatorState::onUpdate() {
    // Press B to return to menu
    if (InputManager::getInstance().justPressed(5)) { // BTN_B
        m_isRunning = false;
        
        return;
    }
    
    if (m_isRunning) {
        // Execute batch of instructions to maintain 60FPS UI but realistic MHz
        for (int i = 0; i < 10000; i++) {
            // Emulate Timer0 Prescaler Tick
            if ((i % 64) == 0) {
                m_sram[0x46]++; // TCNT0 is I/O 0x26, so SRAM 0x46
                if (m_sram[0x46] == 0) {
                    m_sram[0x35] |= 0x01; // Set TOV0 flag in TIFR0
                }
            }
            
            // --- HARDWARE INTERRUPT DISPATCHER ---
            bool global_int_enabled = (m_sreg & 0x80) != 0;
            bool timer0_ovf_enabled = (m_sram[0x6E] & 0x01) != 0; // TIMSK0 TOIE0
            bool timer0_ovf_flag = (m_sram[0x35] & 0x01) != 0;    // TIFR0 TOV0
            
            if (global_int_enabled && timer0_ovf_enabled && timer0_ovf_flag) {
                // Execute Interrupt Sequence
                m_sram[0x35] &= ~0x01; // Clear TOV0 flag
                m_sreg &= ~0x80;       // Disable Global Interrupts
                
                // Push current PC to stack (High byte first, then Low byte)
                uint16_t sp = m_sram[0x3D + 0x20] | (m_sram[0x3E + 0x20] << 8);
                m_sram[sp] = m_pc >> 8;
                sp--;
                m_sram[sp] = m_pc & 0xFF;
                sp--;
                
                // Update SP
                m_sram[0x3D + 0x20] = sp & 0xFF;
                m_sram[0x3E + 0x20] = sp >> 8;
                
                // Jump to TIMER0_OVF vector (Word address 0x002E)
                m_pc = 0x002E;
            }
            
            // Fetch (AVR is little-endian, PC points to 16-bit words)
            uint16_t opcode = m_avrFlash[m_pc * 2] | (m_avrFlash[(m_pc * 2) + 1] << 8);
            m_pc++;
            
            // Decode & Execute
            executeOpcode(opcode);
            
            if (!m_isRunning) break;
        }
        
        static int frameCount = 0;
        frameCount++;
        if (frameCount >= 60) {
            ESP_LOGI(TAG, "SPI Heartbeat | Data Bytes: %u | Cmd Bytes: %u | Buffer Index: %d", spi_data_bytes, spi_cmd_bytes, m_displayIndex);
            frameCount = 0;
        }
    }
}

void EmulatorState::onDraw() {
    // We already drew the loading message during onEnter. 
    // For now, no dynamic updates.
}

void EmulatorState::onExit() {
    ESP_LOGI(TAG, "Exiting Emulator State...");
}
int main() {
    EmulatorState state("/Users/bhargmac/Downloads/Blackjack.hex");
    state.onEnter();
    
    // Simulate 200 frames to see where it loops (1 frame = 10,000 insts)
    for (int i=0; i<200; i++) {
        state.onUpdate();
        if (i % 10 == 0) {
            printf("Frame %d | PC: %04X\n", i, state.getPC());
        }
    }
    return 0;
}
