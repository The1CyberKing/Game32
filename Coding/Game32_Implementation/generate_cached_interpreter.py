import re

with open("lib/StateMachine/States/EmulatorState.cpp", "r") as f:
    orig = f.read()

# Extract executeOpcode block
start = orig.find("void EmulatorState::executeOpcode(uint16_t opcode) {")
end = orig.find("void EmulatorState::onUpdate() {")
exec_block = orig[start:end]

blocks = re.split(r'\} else if \(|\} else \{', exec_block)

pre_decode = []
run_slice = []
dispatch_table = []

def clean_comment(text):
    for line in text.split('\n'):
        if line.strip().startswith("//"):
            return line.strip()[2:].strip()
    return "UNHANDLED"

def get_op_name(comment):
    return "OP_" + comment.split()[0].replace("+", "_INC").replace("-", "_DEC").replace(",", "_").upper()

for i, block in enumerate(blocks):
    if i == 0: continue
    
    if "{" in block:
        cond, body = block.split("{", 1)
        cond = cond.strip()
        if cond.endswith(")"): cond = cond[:-1].strip()
    else:
        continue
        
    comment = clean_comment(body)
    op_name = get_op_name(comment)
    if "UNHANDLED" in op_name: op_name = "OP_UNHANDLED"
    if "NOP" in op_name: op_name = "OP_NOP"
    if "FALLBACK" in op_name: op_name = "OP_UNHANDLED"
    if "TIME_WARPING" in op_name: continue
        
    decoding = []
    executing = []
    
    lines = body.split('\n')
    for line in lines:
        line = line.strip()
        if line.startswith("//"): continue
        if not line: continue
        if line == "}": continue
        
        if re.match(r'uint8_t [A-Za-z] = .*\(opcode.*?;', line) or re.match(r'int16_t [A-Za-z]+ = .*\(opcode.*?;', line):
            var_name = line.split('=')[0].split()[1]
            val = line.split('=', 1)[1].strip()
            if var_name in ['d', 'r', 'k', 'A', 'K', 'b', 's', 'q', 'offset']:
                if var_name == 'offset':
                    decoding.append(f"op.operands.offset = {val}")
                    executing.append(f"int16_t offset = op.operands.offset;")
                elif var_name in ['A', 'K', 'b', 's', 'q']: 
                    decoding.append(f"op.operands.addr_or_k = {val}")
                    executing.append(f"uint8_t {var_name} = op.operands.addr_or_k;")
                elif var_name == 'k':
                    decoding.append(f"op.operands.addr_or_k = {val}")
                    executing.append(f"uint16_t {var_name} = op.operands.addr_or_k;")
                else:
                    decoding.append(f"op.operands.{var_name} = {val}")
                    executing.append(f"uint8_t {var_name} = op.operands.{var_name};")
            else:
                executing.append(line)
        elif "m_pc++" in line or "m_pc +=" in line or "skip_next" in line:
            if "next_word" in line:
                executing.append(line)
            elif "skip_next" in line:
                executing.append("m_pc += m_instructionCache[m_pc].length;")
            else:
                pass
        else:
            executing.append(line)
            
    length = 2 if op_name in ['OP_JMP', 'OP_CALL', 'OP_LDS', 'OP_STS'] else 1
    cycles = 2 if op_name in ['OP_RJMP', 'OP_JMP'] else 1 # basic heuristic
    
    pre_decode_body = f"op.opcode_class = {op_name};\n" + "\n".join(decoding) + f"\nop.length = {length};\nop.cycles = {cycles};"
    pre_decode.append(f"        else if ({cond}) {{\n            {pre_decode_body.replace(chr(10), chr(10)+'            ')}\n        }}")
    run_slice.append(f"    L_{op_name}: {{\n        " + "\n        ".join(executing) + "\n        goto dispatch_next;\n    }")

cpp_header = """#include "EmulatorState.h"
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
                m_frameDirty = true; // DEFERRED RENDERING!
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
            if (address + byteCount > sizeof(m_avrFlash)) {
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
    
    if (m_instructionCache == nullptr) {
        m_instructionCache = new DecodedOp[32768 / 2];
    }
    
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
        DecodedOp& op = m_instructionCache[pc];
        
        if (opcode == 0x0000) {
            op.opcode_class = OP_NOP;
            op.length = 1; op.cycles = 1;
        }
"""

cpp_mid = """
        else {
            op.opcode_class = OP_UNHANDLED;
            op.length = 1; op.cycles = 1;
        }
    }
}

void IRAM_ATTR EmulatorState::runCpuSlice() {
    static void* dispatch_table[] = {
        &&L_OP_NOP, &&L_OP_CPC, &&L_OP_ADD, &&L_OP_SBC, &&L_OP_MOVW, &&L_OP_MULS, &&L_OP_MULSU, &&L_OP_FMUL, &&L_OP_FMULS, &&L_OP_FMULSU,
        &&L_OP_CP, &&L_OP_ADC, &&L_OP_SUB, &&L_OP_CPSE, &&L_OP_AND, &&L_OP_OR, &&L_OP_EOR, &&L_OP_MOV, &&L_OP_CPI, &&L_OP_SBCI,
        &&L_OP_SUBI, &&L_OP_ORI, &&L_OP_ANDI, &&L_OP_ST_Z, &&L_OP_STD_Z, &&L_OP_LDD_Z, &&L_OP_LDD_Y, &&L_OP_STD_Y, &&L_OP_LD_Y,
        &&L_OP_ST_Y, &&L_OP_SLEEP, &&L_OP_RETI, &&L_OP_RET, &&L_OP_JMP, &&L_OP_CALL, &&L_OP_DEC, &&L_OP_LD_Z_INC, &&L_OP_LPM_Z,
        &&L_OP_LPM_Z_INC, &&L_OP_INC, &&L_OP_PUSH, &&L_OP_POP, &&L_OP_ST_Z_INC, &&L_OP_ST_Z_DEC, &&L_OP_LDS, &&L_OP_STS,
        &&L_OP_LD_X, &&L_OP_LD_X_INC, &&L_OP_LD_X_DEC, &&L_OP_ST_X, &&L_OP_ST_X_INC, &&L_OP_ST_X_DEC, &&L_OP_LD_Y_INC,
        &&L_OP_LD_Y_DEC, &&L_OP_ST_Y_INC, &&L_OP_ST_Y_DEC, &&L_OP_SBI, &&L_OP_CBI, &&L_OP_SBIS, &&L_OP_SBIC, &&L_OP_BSET,
        &&L_OP_BCLR, &&L_OP_ADIW, &&L_OP_SBIW, &&L_OP_LSR, &&L_OP_ASR, &&L_OP_ROR, &&L_OP_MUL, &&L_OP_SWAP, &&L_OP_COM, &&L_OP_NEG,
        &&L_OP_OUT, &&L_OP_IN, &&L_OP_RJMP, &&L_OP_RCALL, &&L_OP_LDI, &&L_OP_SBRS, &&L_OP_SBRC, &&L_OP_BRBS, &&L_OP_BRBC,
        &&L_OP_BST, &&L_OP_BLD, &&L_OP_SPM,
        &&L_OP_UNHANDLED
    };

    uint32_t slice_cycles = 0;
    while (slice_cycles < 25000 && m_isRunning) {
        const DecodedOp& op = m_instructionCache[m_pc];
        m_pc += op.length;
        m_cycleAccumulator += op.cycles;
        slice_cycles += op.cycles;
        
        goto *dispatch_table[op.opcode_class];

    L_OP_NOP:
        goto dispatch_next;
"""

cpp_end = """
    L_OP_UNHANDLED:
        // NOP Fallback
        goto dispatch_next;

    dispatch_next:
        // Global Auto-Completers
        if (unlikely(m_sram[0x007A] & (1 << 6))) {
            m_sram[0x007A] &= ~(1 << 6);
            m_sram[0x007A] |= (1 << 4);
            m_sram[0x0078] = 0x50;
            m_sram[0x0079] = 0x02;
        }
        
        // Cycle-Accurate Timer0
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
                
                m_pc = 0x002E; // Jump to TIMER0_OVF vector
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

    // Run the high-performance execution slice in IRAM
    runCpuSlice();

    // Deferred Rendering! Push display over SPI only once per frame outside CPU execution
    if (m_frameDirty) {
        DisplayManager::getInstance().drawArduboyFrame(m_displayBuffer);
        m_frameDirty = false;
    }

    vTaskDelay(1);
}

void EmulatorState::onDraw() { }
void EmulatorState::onExit() { }
"""

with open("lib/StateMachine/States/EmulatorState.cpp", "w") as f:
    f.write(cpp_header)
    f.write("\n".join(pre_decode))
    f.write(cpp_mid)
    f.write("\n".join(run_slice))
    f.write(cpp_end)

print("Generated full Cached EmulatorState.cpp!")
