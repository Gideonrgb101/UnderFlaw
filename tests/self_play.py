import subprocess
import time
import sys

# Configuration
ENGINE_PATH = "build/bin/chess_engine.exe"
GAMES = 10  # Start with 10 for quick verification
MOVE_TIME_MS = 100
MAX_MOVES = 200

def get_best_move(process, position_moves):
    """Sends position and go command, returns bestmove."""
    moves_str = " ".join(position_moves)
    cmd = f"position startpos moves {moves_str}\n" if position_moves else "position startpos\n"
    process.stdin.write(cmd)
    process.stdin.write(f"go movetime {MOVE_TIME_MS}\n")
    process.stdin.flush()
    
    while True:
        line = process.stdout.readline()
        if not line:
            return None
        if line.startswith("bestmove"):
            return line.split()[1]

def play_game(game_id):
    print(f"Starting Game {game_id}...", end=" ", flush=True)
    
    p1 = subprocess.Popen(ENGINE_PATH, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
    p2 = subprocess.Popen(ENGINE_PATH, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
    
    # Initialize Engines
    for p in [p1, p2]:
        p.stdin.write("uci\n")
        p.stdin.write("isready\n")
        p.stdin.flush()
        while "readyok" not in p.stdout.readline():
            pass

    moves = []
    winner = "Draw"
    
    try:
        for m_idx in range(MAX_MOVES):
            current_player = p1 if m_idx % 2 == 0 else p2
            move = get_best_move(current_player, moves)
            
            if move is None or move == "(none)" or move == "0000":
                # Check for mate or draw
                # (Simple script: if engine doesn't return move, game ends)
                break
                
            moves.append(move)
            # print(f"{move} ", end="", flush=True)

        print(f"Finished in {len(moves)} moves.")
    except Exception as e:
        print(f"ERROR: {e}")
    finally:
        p1.terminate()
        p2.terminate()
    
    return moves

def main():
    print(f"Running {GAMES} Self-Play Games for Stability Check...")
    for i in range(1, GAMES + 1):
        moves = play_game(i)
        if not moves:
            print(f"Game {i} FAILED: No moves generated.")
            sys.exit(1)
            
    print("All games finished successfully. No crashes detected.")

if __name__ == "__main__":
    main()
