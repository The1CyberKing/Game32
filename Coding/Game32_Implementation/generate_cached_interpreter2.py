import re

with open("lib/StateMachine/States/EmulatorState.cpp", "r") as f:
    orig = f.read()

start = orig.find("void EmulatorState::executeOpcode(uint16_t opcode) {")
end = orig.find("void EmulatorState::onUpdate() {")
exec_block = orig[start:end]

blocks = re.split(r'\} else if \(|\} else \{', exec_block)

def clean_comment(text):
    for line in text.split('\n'):
        if line.strip().startswith("//"):
            return line.strip()[2:].strip()
    return "UNHANDLED"

op_list = []
pre_decode = []
run_slice = []

for i, block in enumerate(blocks):
    if i == 0: continue
    
    if "{" in block:
        cond, body = block.split("{", 1)
        cond = cond.strip()
        if cond.endswith(")"): cond = cond[:-1].strip()
    else:
        continue
        
    comment = clean_comment(body)
    base_name = comment.split()[0].replace("+", "_INC").replace("-", "_DEC").replace(",", "_").upper()
    
    # Extract hex masks
    masks = re.findall(r'0x[0-9a-fA-F]+', cond)
    if len(masks) == 2:
        op_name = f"OP_{base_name}_{masks[0][2:]}_{masks[1][2:]}"
    elif len(masks) == 1:
        op_name = f"OP_{base_name}_{masks[0][2:]}"
    else:
        if "FALLBACK" in base_name or "UNHANDLED" in base_name:
            op_name = "OP_UNHANDLED"
        else:
            op_name = f"OP_{base_name}_{i}"
            
    if op_name == "OP_NOP_0000": op_name = "OP_NOP"
    if op_name not in op_list:
        op_list.append(op_name)
        
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
            
    length = 2 if base_name in ['JMP', 'CALL', 'LDS', 'STS'] else 1
    cycles = 2 if base_name in ['RJMP', 'JMP', 'CALL', 'RCALL', 'RET', 'RETI'] else 1 
    
    pre_decode_body = f"op.opcode_class = {op_name};\n" + "\n".join(decoding) + f"\nop.length = {length};\nop.cycles = {cycles};"
    pre_decode.append(f"        else if ({cond}) {{\n            {pre_decode_body.replace(chr(10), chr(10)+'            ')}\n        }}")
    if op_name != "OP_UNHANDLED":
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
"""
cpp_mid += "        " + ", ".join([f"&&L_{op}" for op in op_list if op != "OP_UNHANDLED"]) + ",\n        &&L_OP_UNHANDLED\n    };\n"

cpp_mid += """
    uint32_t slice_cycles = 0;
    while (slice_cycles < 25000 && m_isRunning) {
        const DecodedOp& op = m_instructionCache[m_pc];
        m_pc += op.length;
        m_cycleAccumulator += op.cycles;
        slice_cycles += op.cycles;
        
        goto *dispatch_table[op.opcode_class];
"""

cpp_end = """
    L_OP_UNHANDLED:
        // NOP Fallback
        goto dispatch_next;

    dispatch_next:
        if (unlikely(m_sram[0x007A] & (1 << 6))) {
            m_sram[0x007A] &= ~(1 << 6);
            m_sram[0x007A] |= (1 << 4);
            m_sram[0x0078] = 0x50;
            m_sram[0x0079] = 0x02;
        }
        
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
"""

with open("lib/StateMachine/States/EmulatorState.cpp", "w") as f:
    f.write(cpp_header)
    f.write("\n".join(pre_decode))
    f.write(cpp_mid)
    f.write("\n".join(run_slice))
    f.write(cpp_end)

# Also generate EmulatorState.h to match the op_list
h_orig = """#ifndef EMULATOR_STATE_H
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

enum OpcodeClass : uint16_t {
"""
h_mid = "    " + ", ".join(op_list) + ",\n    OP_UNHANDLED\n};\n"
h_end = """
struct DecodedOp {
    union {
        struct { uint8_t d, r; };
        struct { uint16_t addr_or_k; };
        struct { int16_t offset; };
    } operands;
    uint8_t length;
    uint8_t cycles;
    uint16_t opcode_class;
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
    
    DecodedOp* m_instructionCache = nullptr;
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
"""

with open("lib/StateMachine/States/EmulatorState.h", "w") as f:
    f.write(h_orig + h_mid + h_end)

print("Generated BOTH files!")
