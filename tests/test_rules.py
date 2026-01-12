import subprocess
import sys
import time
import re

ENGINE_PATH = "build/bin/chess_engine.exe"

def run_uci_test(commands):
    """Runs a sequence of UCI commands and collects output."""
    process = subprocess.Popen(
        ENGINE_PATH,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1
    )
    
    output_lines = []
    
    try:
        inputs = "\n".join(commands) + "\n"
        stdout, stderr = process.communicate(input=inputs, timeout=5)
        output_lines = stdout.splitlines()
    except subprocess.TimeoutExpired:
        process.kill()
        print("Engine timed out.")
    except Exception as e:
        print(f"Error running engine: {e}")
        
    return output_lines

def parse_score(lines):
    """Finds the score from the last info line."""
    score = None
    for line in reversed(lines):
        if line.startswith("info") and "score" in line:
            # Matches "score cp 0" or "score mate 5"
            match = re.search(r"score (cp|mate) (-?\d+)", line)
            if match:
                type_ = match.group(1)
                val = int(match.group(2))
                if type_ == "mate":
                    val = 10000 * (1 if val > 0 else -1) # Rough mate score
                score = val
                break
    return score

def test_threefold_repetition():
    print("Testing Threefold Repetition...", end=" ")
    # Simple repetition: 1. Nf3 Nf6 2. Ng1 Ng8 3. Nf3 Nf6 4. Ng1 Ng8
    moves = "g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1 f6g8"
    
    # We ask the engine to search. It should see the 3rd repetition and score 0.
    commands = [
        "uci",
        "ucinewgame",
        f"position startpos moves {moves}",
        "go depth 5",
        "quit"
    ]
    
    lines = run_uci_test(commands)
    score = parse_score(lines)
    
    if score is not None and abs(score) <= 50: # Allow small contempt variance
        print(f"PASS (Score: {score})")
        return True
    else:
        print(f"FAIL (Score: {score})")
        print("\n".join(lines))
        return False

def test_fifty_move_rule():
    print("Testing 50-Move Rule...", end=" ")
    # FEN with high halfmove clock (99). Next move should trigger draw score.
    # High halfmove clock: 8/8/8/8/8/8/4k3/4K3 w - - 99 1
    # White moves Kd1 (quiet). Clock becomes 100 -> Draw.
    
    fen = "8/8/8/8/8/8/4k3/4K3 w - - 99 1"
    
    commands = [
        "uci",
        "ucinewgame",
        f"position fen {fen}",
        "go depth 5", # Search should see immediate draw after 1 ply
        "quit"
    ]
    
    lines = run_uci_test(commands)
    score = parse_score(lines)
     
    if score is not None and abs(score) <= 50:
        print(f"PASS (Score: {score})")
        return True
    else:
        print(f"FAIL (Score: {score})")
        return False

if __name__ == "__main__":
    if not test_threefold_repetition():
        sys.exit(1)
    if not test_fifty_move_rule():
        sys.exit(1)
    print("All rule compliance tests passed.")
