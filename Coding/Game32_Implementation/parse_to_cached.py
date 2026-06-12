import re

with open("lib/StateMachine/States/EmulatorState.cpp", "r") as f:
    text = f.read()

# I will write the python script to generate the Cached Interpreter EmulatorState.cpp.
# Actually, it's safer to just write the file completely by hand if I know the opcodes.
