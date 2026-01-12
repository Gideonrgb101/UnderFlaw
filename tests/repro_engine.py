
import subprocess
import time
import os

ENGINE_PATH = "build/bin/UnderFlaw.exe"
NNUE_PATH = "assets/chess_net.nnue"
FEN = "r1b5/1pkn1Q2/2p5/p6p/7P/PPb3P1/5PB1/1R3R1K b - - 0 33"
ILLEGAL_MOVE = "d7f8" # d7 to f8

def run_test():
    if not os.path.exists(ENGINE_PATH):
        print(f"Error: Engine not found at {ENGINE_PATH}")
        return
    
    nnue_path_to_use = NNUE_PATH
    # Check if NNUE exists
    if not os.path.exists(nnue_path_to_use):
        print(f"Error: NNUE network not found at {nnue_path_to_use}")
        # Try alternate location
        local_nnue = "chess_net.nnue"
        if os.path.exists(local_nnue):
            print("Found local chess_net.nnue, using that.")
            nnue_path_to_use = local_nnue
        else:
             print("Warning: Running without valid NNUE path might fail if engine requires it.")

    print(f"Starting engine: {ENGINE_PATH}")
    process = subprocess.Popen(
        [ENGINE_PATH],

        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=0 # Unbuffered
    )

    try:
        # Initial Handshake
        process.stdin.write("uci\n")
        
        # Wait for uciok
        while True:
            line = process.stdout.readline()
            if not line: break
            line = line.strip()
            # print(f"Engine: {line}")
            if line == "uciok":
                break
        
        print("Received uciok.")
        
        # Configure NNUE
        process.stdin.write("setoption name UseNNUE value true\n")
        # process.stdin.write(f"setoption name EvalFile value {NNUE_PATH}\n")
        # NOTE: EvalFile might be needed if default is not loaded.
        # But 'setoption' might trigger load.
        
        # Set Position
        print(f"Setting position: {FEN}")
        process.stdin.write(f"position fen {FEN}\n")
        
        # Search
        print("Starting search (go depth 4)...")
        process.stdin.write("go depth 4\n")
        
        # Wait for bestmove
        best_move = None
        start_time = time.time()
        while True:
            if time.time() - start_time > 10:
                print("Timeout waiting for bestmove")
                break
                
            line = process.stdout.readline()
            if not line: break
            line = line.strip()
            print(f"Engine: {line}")
            
            if line.startswith("bestmove"):
                best_move = line.split()[1]
                break
        
        print(f"Result: bestmove {best_move}")
        
        if best_move == ILLEGAL_MOVE:
            print("FAILURE: Engine played the illegal move d7f8!")
        else:
            print(f"SUCCESS: Engine played legal move {best_move}.")
            
    except Exception as e:
        print(f"Exception: {e}")
    finally:
        process.stdin.write("quit\n")
        process.wait()

if __name__ == "__main__":
    run_test()
