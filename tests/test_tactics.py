import subprocess
import sys
import re
import time

ENGINE_PATH = "build/bin/chess_engine.exe"
EPD_PATH = "tests/tactics.epd"
TIME_LIMIT_MS = 3000  # 3 second per problem

def run_uci_command(process, command):
    process.stdin.write(command + "\n")
    process.stdin.flush()

def read_output(process, timeout):
    start_time = time.time()
    lines = []
    while time.time() - start_time < timeout:
        line = process.stdout.readline()
        if not line:
            break
        line = line.strip()
        lines.append(line)
        if line.startswith("bestmove"):
            return lines
    return lines

def solve_position(fen, best_moves):
    process = subprocess.Popen(
        ENGINE_PATH,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1
    )
    
    try:
        run_uci_command(process, "uci")
        run_uci_command(process, "ucinewgame")
        run_uci_command(process, f"position fen {fen}")
        run_uci_command(process, f"go movetime {TIME_LIMIT_MS}")
        
        lines = read_output(process, (TIME_LIMIT_MS / 1000) + 1.0)
        
        found_move = None
        for line in lines:
            if line.startswith("bestmove"):
                found_move = line.split()[1]
                break
        
        # Normalize move (remove + or # from SAN if present, but engine outputs UCI usually)
        # The EPD has SAN (e.g. Nf6+), but engine outputs UCI (d5f6).
        # We need to rely on the engine finding the COORDINATE move.
        # Wait, the EPD has SAN "bm Nf6+". My engine outputs UCI "d5f6".
        # Mapping SAN to UCI is hard without a library.
        # I should probably update the EPD to use UCI for simplicity or try to match.
        
        # Let's just print the found move and let the user verify for now, 
        # or update the EPD to UCI.
        # Update: I will update the EPD to UCI to make this automated test robust.
        
        print(f"Engine found: {found_move}")
        process.kill()
        return found_move
        
    except Exception as e:
        print(f"Error: {e}")
        process.kill()
        return None

def main():
    print("Running Tactical Tests...")
    
    with open(EPD_PATH, "r") as f:
        lines = f.readlines()
        
    for line in lines:
        parts = line.strip().split(";")
        fen = parts[0].strip()
        bm_part = [p for p in parts if "bm" in p][0]
        id_part = [p for p in parts if "id" in p][0]
        
        target = bm_part.split("bm")[1].strip()
        name = id_part.split("id")[1].strip().strip('"')
        
        print(f"Solving: {name}")
        found = solve_position(fen, target)
        
        if found == target:
            print(f"PASS: Found {found}")
        else:
            print(f"FAIL: Expected {target}, Found {found}")
        
if __name__ == "__main__":
    main()
