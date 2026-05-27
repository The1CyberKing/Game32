with open("optimized_execute.cpp", "r") as f:
    lines = f.readlines()

out = []
in_block = False

for line in lines:
    stripped = line.strip()
    if not stripped:
        out.append(line)
        continue
        
    if stripped.startswith("case ") or stripped.startswith("switch ") or stripped == "break;" or stripped == "}" or "auto skip_next" in line or "m_pc +=" in line or "uint16_t next_op" in line or "bool is_32bit" in line or "is_32bit = true;" in line:
        if in_block and (stripped.startswith("case ") or stripped == "break;" or stripped == "}"):
            if stripped != "}":
                out.append("            }\n")
            in_block = False
        out.append(line)
        continue
    
    if (stripped.startswith("if (") or stripped.startswith("} else if (")) and ("opcode &" in stripped or "opcode ==" in stripped):
        if stripped.startswith("}"):
            stripped = stripped[1:].strip()
            if in_block:
                out.append("            } " + stripped + "\n")
            else:
                out.append("            " + stripped + "\n")
        else:
            if in_block:
                out.append("            }\n")
            out.append("            " + stripped + "\n")
        
        in_block = True
        continue
    
    if in_block:
        out.append("                " + stripped + "\n")
    else:
        out.append(line)

with open("fixed_execute.cpp", "w") as f:
    f.writelines(out)

print("Done")
