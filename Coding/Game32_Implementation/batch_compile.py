#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import re
from pathlib import Path

# Force line-buffered output even when piped (e.g. `./batch_compile.py | tee log.txt`).
sys.stdout.reconfigure(line_buffering=True)

# ==========================================
# CONFIGURATION - ABSOLUTE PATHS
# ==========================================
# 1. The root of your Game32 ESP32 firmware project
GAME32_ROOT_DIR = Path("/home/vaddib/Projects/Github_Repositories/Game32/Coding/Game32_Implementation").resolve()

# We derive the lib and include folders from the root
GAME32_LIB_DIR = GAME32_ROOT_DIR / "lib"
GAME32_INCLUDE_DIR = GAME32_ROOT_DIR / "include"

# 2. The folder containing the 150+ raw Arduboy game folders
GAMES_DIR = Path("projects").resolve()

# 3. Where the script will spit out the finished .bin files
OUTPUT_DIR = Path("compiled_games").resolve()

# 4. The temporary directory for PlatformIO
WORKSPACE = Path("batch_workspace").resolve()
SRC_DIR = WORKSPACE / "src"

SOURCE_EXTENSIONS = {".ino", ".cpp", ".cc", ".c", ".h", ".hpp"}

def find_game_units(games_dir):
    units = []
    for entry in sorted(games_dir.iterdir()):
        if not entry.is_dir():
            continue

        has_own_ino = any(f.suffix.lower() == ".ino" for f in entry.glob("*"))
        sub_games = [
            d for d in sorted(entry.iterdir())
            if d.is_dir() and any(f.suffix.lower() == ".ino" for f in d.glob("*"))
        ]

        if not has_own_ino and len(sub_games) >= 2:
            print(f"[i] '{entry.name}' looks like a collection of {len(sub_games)} games.")
            for sub in sub_games:
                units.append((f"{entry.name}__{sub.name}", sub))
        else:
            units.append((entry.name, entry))
    return units


def setup_workspace():
    if WORKSPACE.exists():
        shutil.rmtree(WORKSPACE)
    WORKSPACE.mkdir()
    SRC_DIR.mkdir()
    OUTPUT_DIR.mkdir(exist_ok=True)
    print(f"[*] Workspace initialized at {WORKSPACE}")


def create_pio_ini(game_name, has_eeprom=False):
    safe_name = game_name.replace(" ", "_").replace("'", "").replace('"', '')

    # Claude's Fix #1 & Gemini's Fix: Generate sdkconfig.defaults to permanently kill ESP-IDF's -Werror
    with open(WORKSPACE / "sdkconfig.defaults", "w") as f:
        f.write("CONFIG_COMPILER_WARN_ERROR=n\nCONFIG_COMPILER_DISABLE_DEFAULT_ERRORS=y\n")

    extra_flags = ""
    if has_eeprom:
        extra_flags += "    -DHAS_EEPROM_STUBS\n"

    # Claude's Fix #2: Explicitly include every manager to bypass LDF guesswork
    ini_content = f"""[platformio]
src_dir = src

[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
board_build.flash_mode = dout
board_build.flash_size = 2MB
board_upload.flash_size = 2MB
lib_extra_dirs = {GAME32_LIB_DIR}
lib_ldf_mode = deep+
board_build.f_cpu = 240000000L
build_src_filter = +<*> -<**/*.c>
build_flags =
    -O3
    -w
{extra_flags}    -D PROGMEM=
    -D pgm_read_word_near=pgm_read_word
    -D pgm_read_byte_near=pgm_read_byte
    -I "{GAME32_INCLUDE_DIR}"
    -I "{GAME32_LIB_DIR}/NativeEngine"
    -I "{GAME32_LIB_DIR}/DisplayManager"
    -I "{GAME32_LIB_DIR}/InputManager"
    -I "{GAME32_LIB_DIR}/AudioEngine"
    -I "{GAME32_LIB_DIR}/BatteryManager"
    -I "{GAME32_LIB_DIR}/PowerManager"
    -I "{GAME32_LIB_DIR}/SDManager"
    -I "{GAME32_LIB_DIR}/StateMachine"
    -D GAME_NAME=\\"{safe_name}\\"
"""
    with open(WORKSPACE / "platformio.ini", "w") as f:
        f.write(ini_content)


def compile_game(game_name, game_path):
    print(f"\n{'='*50}")
    print(f"[*] Processing: {game_name}")
    print(f"{'='*50}")

    for item in SRC_DIR.iterdir():
        if item.is_file():
            try:
                item.unlink()
            except FileNotFoundError:
                pass
        elif item.is_dir():
            shutil.rmtree(item)

    pio_cache = WORKSPACE / ".pio"
    if pio_cache.exists():
        shutil.rmtree(pio_cache, ignore_errors=True)

    has_eeprom = False
    copied_files = 0
    
    for file_path in game_path.rglob("*"):
        if file_path.is_file() and file_path.suffix.lower() in SOURCE_EXTENSIONS:
            rel_path = file_path.relative_to(game_path)

            if any(part.startswith('.') for part in rel_path.parts):
                continue
                
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                if bool(re.search(r'^\s*(?:void|int|uint16_t|inline)\s+(initEEPROM|EEPROMWriteInt|EEPROMReadInt)\s*\(', content, re.MULTILINE)):
                    has_eeprom = True

                # Patch 1: Replace Arduino binary macros (B010) with standard C++14 binary literals (0b010)
                content = re.sub(r'\bB([01]{2,8})\b', r'0b\1', content)
                
                # Patch 2: Strip out fatal 16-bit pointer truncations in pgm_read_* on ESP32
                old_content = content
                content = re.sub(r'pgm_read_(word|byte|ptr|dword|float)\s*\(\s*\(uint16_t\)\s*&?', r'pgm_read_\1(&', content)
                content = re.sub(r'pgm_read_(word|byte|ptr|dword|float)\s*\(\s*\(uint16_t\)\s*', r'pgm_read_\1(', content)
                if content != old_content:
                    print(f"[DEBUG] Replaced pgm_read_* cast in {file_path.name}")

                # Patch 3: Rename POSIX colliding mode_t to arduboy_mode_t
                content = re.sub(r'\bmode_t\b', r'arduboy_mode_t', content)

            dest_file = SRC_DIR / rel_path
            dest_file.parent.mkdir(parents=True, exist_ok=True)
            
            with open(dest_file, 'w', encoding='utf-8') as f:
                f.write(content)
            
            copied_files += 1

    if copied_files == 0:
        print(f"[!] No source files found for {game_name}. Skipping.")
        return False

    create_pio_ini(game_name, has_eeprom=has_eeprom)

    try:
        process = subprocess.run(
            ["pio", "run"],
            cwd=WORKSPACE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True
        )

        if process.returncode == 0:
            compiled_bin = WORKSPACE / ".pio" / "build" / "esp32dev" / "firmware.bin"
            if compiled_bin.exists():
                final_bin = OUTPUT_DIR / f"{game_name}.bin"
                shutil.copy(compiled_bin, final_bin)
                print(f"[SUCCESS] Compiled and saved: {final_bin.name}")
                return True
            else:
                print(f"[ERROR] Compilation succeeded but firmware.bin is missing.")
                return False
        else:
            print(f"[FAILED] Compilation failed for {game_name}.")
            print(process.stdout)
            return False

    except FileNotFoundError:
        print("[!] 'pio' command not found. Ensure PlatformIO is in your PATH.")
        exit(1)


def main():
    if not GAME32_LIB_DIR.exists():
        print(f"[!] ERROR: Cannot find Game32 library directory at {GAME32_LIB_DIR}")
        exit(1)
    if not GAME32_INCLUDE_DIR.exists():
        print(f"[!] ERROR: Cannot find Game32 include directory at {GAME32_INCLUDE_DIR}")
        exit(1)

    setup_workspace()
    games = find_game_units(GAMES_DIR)
    
    success_count, fail_count = 0, 0
    for game_name, game_dir in games:
        if (OUTPUT_DIR / f"{game_name}.bin").exists():
            print(f"[-] Skipping {game_name}: Already compiled.")
            continue
        if compile_game(game_name, game_dir):
            success_count += 1
        else:
            fail_count += 1

    print(f"\n{'='*50}")
    print("BATCH COMPILATION COMPLETE")
    print(f"Successfully compiled: {success_count}")
    print(f"Failed to compile:   {fail_count}")
    print(f"Binaries saved in:   {OUTPUT_DIR.resolve()}")
    print(f"{'='*50}")

if __name__ == "__main__":
    main()