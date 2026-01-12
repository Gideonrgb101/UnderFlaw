import chess
import subprocess
import time
import sys

ENGINE_PATH = 'build/bin/UnderFlaw.exe'

def play_game():
    board = chess.Board()
    engine = subprocess.Popen(
        [ENGINE_PATH],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1
    )
    
    # Init
    engine.stdin.write('setoption name EvalFile value assets/chess_net.nnue\n')
    engine.stdin.write('setoption name UseNNUE value true\n')
    engine.stdin.write('isready\n')
    engine.stdin.flush()
    
    while True:
        line = engine.stdout.readline()
        if 'readyok' in line: break
        
    print("Engine ready. Playing game...")
    
    move_log = []
    
    while not board.is_game_over(claim_draw=True):
        # 1. Send Position
        cmd = f"position startpos moves {' '.join(move_log)}\n"
        engine.stdin.write(cmd)
        engine.stdin.flush()
        
        # 2. Go
        engine.stdin.write("go depth 4\n")
        engine.stdin.flush()
        
        # 3. Get Move
        best_move = None
        while True:
            line = engine.stdout.readline()
            if not line: break
            if 'bestmove' in line:
                best_move = line.split()[1]
                break
                
        if not best_move or best_move == '(none)':
            print("Engine returned no move!")
            break
            
        # 4. Validate
        try:
            move = chess.Move.from_uci(best_move)
            if move not in board.legal_moves:
                print(f"\nFATAL: Engine played ILLEGAL MOVE: {best_move}")
                print(f"FEN: {board.fen()}")
                print(f"Moves: {' '.join(move_log)}")
                return False
        except ValueError:
             print(f"\nFATAL: Engine played MALFORMED MOVE: {best_move}")
             return False
             
        # 5. Make Move
        board.push(move)
        move_log.append(best_move)
        
        print(f"{len(move_log)}. {best_move}", end=' ', flush=True)
        if len(move_log) % 10 == 0: print()
        
    print(f"\nGame Over. Result: {board.result()}")
    engine.kill()
    return True

if __name__ == "__main__":
    for i in range(5):
        print(f"\n=== Game {i+1} ===")
        if not play_game():
            sys.exit(1)
