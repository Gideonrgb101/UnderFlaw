import subprocess
import random
import numpy as np
import os
import struct
import multiprocessing
import time

# Configuration
NUM_GAMES = 2000     # Generate 2000 games
NUM_WORKERS = max(1, multiprocessing.cpu_count() - 2)
SEARCH_DEPTH = 5
OUTPUT_FILE = 'training_data.bin'

# Constants matching engine
PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING = 0, 1, 2, 3, 4, 5
WHITE, BLACK = 0, 1

def parse_fen(fen):
    """Parse FEN string into board representation"""
    parts = fen.split()
    rows = parts[0].split('/')
    pieces = [] # List of (sq, color, type)
    kings = {WHITE: -1, BLACK: -1}
    
    sq = 56 # Start at a8 (56)
    for row in rows:
        col = 0
        for char in row:
            if char.isdigit():
                col += int(char)
            else:
                color = WHITE if char.isupper() else BLACK
                type_map = {'p':0, 'n':1, 'b':2, 'r':3, 'q':4, 'k':5}
                p_type = type_map[char.lower()]
                curr_sq = sq + col
                
                if p_type == KING:
                    kings[color] = curr_sq
                else:
                    pieces.append((curr_sq, color, p_type))
                col += 1
        sq -= 8 # Move down one rank
    return pieces, kings

def get_halfkp_index(king_sq, piece_type, piece_color, sq):
    """Calculate Half-KP index: king_sq * 640 + piece_idx * 64 + sq"""
    # piece_idx: 0-4 for White pieces, 5-9 for Black pieces
    # relative to the perspective of the king color
    
    # Engine logic:
    # int p_idx = piece + (color == WHITE ? 0 : 5);
    # return king_sq * 640 + p_idx * 64 + sq;
    
    p_idx = piece_type + (0 if piece_color == WHITE else 5)
    return king_sq * 640 + p_idx * 64 + sq

def extract_features(fen):
    """Extract Half-KP features for both perspectives"""
    pieces, kings = parse_fen(fen)
    
    if kings[WHITE] == -1 or kings[BLACK] == -1:
        return None, None # Invalid position
        
    white_indices = []
    black_indices = []
    
    # White Perspective (King = kings[WHITE])
    w_king_sq = kings[WHITE]
    for sq, color, p_type in pieces:
        # get_halfkp_index(w_king_sq, piece, color, sq)
        idx = get_halfkp_index(w_king_sq, p_type, color, sq)
        white_indices.append(idx)
        
    # Black Perspective (King = kings[BLACK])
    # Mirrored: b_king_sq ^ 56, color ^ 1, sq ^ 56
    b_king_mirror = kings[BLACK] ^ 56
    for sq, color, p_type in pieces:
        # get_halfkp_index(b_king_sq_mirrored, piece, color ^ 1, sq ^ 56)
        idx = get_halfkp_index(b_king_mirror, p_type, color ^ 1, sq ^ 56)
        black_indices.append(idx)
        
    return white_indices, black_indices

def play_single_game(game_id):
    random.seed(os.getpid() * time.time())
    
    try:
        engine = subprocess.Popen(
            ['build/bin/UnderFlaw.exe'],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1
        )
    except:
        return []

    moves = []
    dataset_entries = []
    
    # Play
    for move_num in range(random.randint(20, 80)):
        if moves:
            engine.stdin.write(f'position startpos moves {" ".join(moves)}\n')
        else:
            engine.stdin.write('position startpos\n')
        engine.stdin.flush()
        
        # Get FEN
        engine.stdin.write('d\n')
        engine.stdin.flush()
        fen = None
        for _ in range(10):
            try:
                line = engine.stdout.readline()
                if line.strip() and not line.startswith('info'):
                    fen = line.strip()
                    break
            except:
                break
        
        if not fen: break
        
        # Search
        engine.stdin.write(f'go depth {SEARCH_DEPTH}\n')
        engine.stdin.flush()
        
        best_move = None
        score = 0
        timeout = 0
        while timeout < 100:
            try:
                line = engine.stdout.readline()
                if not line: break
                if 'score cp' in line:
                    try:
                        score = int(line.split('score cp')[1].split()[0])
                    except: pass
                if 'bestmove' in line:
                    best_move = line.split()[1]
                    break
                timeout += 1
            except: break
            
        if not best_move or best_move == '(none)': break
        
        # Save entry (after opening)
        if move_num > 8:
            dataset_entries.append({
                'fen': fen,
                'score': score
            })
        
        moves.append(best_move)
    
    try:
        engine.stdin.write('quit\n')
        engine.wait(timeout=1)
    except:
        if engine.poll() is None: engine.kill()
            
    # Assign result
    result = random.choice([1.0, 0.5, 0.0]) # Simplified
    
    processed_data = []
    for entry in dataset_entries:
        w_idx, b_idx = extract_features(entry['fen'])
        if w_idx is None: continue
        
        processed_data.append({
            'w_idx': w_idx,
            'b_idx': b_idx,
            'score': entry['score'],
            'result': result
        })
        
    return processed_data

def generate_positions():
    print(f"Starting CORRECTED generation on {NUM_WORKERS} cores...")
    
    if os.path.exists(OUTPUT_FILE):
        try: os.remove(OUTPUT_FILE)
        except: pass
        
    # Use multiprocessing
    with multiprocessing.Pool(processes=NUM_WORKERS) as pool:
        results = [pool.apply_async(play_single_game, (i,)) for i in range(NUM_GAMES)]
        
        games_done = 0
        total_pos = 0
        
        with open(OUTPUT_FILE, 'wb') as f:
            for res in results:
                batch = res.get()
                games_done += 1
                
                for item in batch:
                    # Write Dense Vector (Floats) - Slow but compatible with tutorial
                    # White Features
                    w_vec = np.zeros(40960, dtype=np.float32)
                    w_vec[item['w_idx']] = 1.0
                    w_vec.tofile(f)
                    
                    # Black Features
                    b_vec = np.zeros(40960, dtype=np.float32)
                    b_vec[item['b_idx']] = 1.0
                    b_vec.tofile(f)
                    
                    # Target
                    target = 0.5 * (item['score'] / 1000.0) + 0.5 * (item['result'] - 0.5)
                    # Clamp target 0-1
                    target = max(0.0, min(1.0, 0.5 + target/2.0)) # Rough scaling
                    np.array([target], dtype=np.float32).tofile(f)
                
                total_pos += len(batch)
                if games_done % 10 == 0:
                    print(f"Progress: {games_done}/{NUM_GAMES} games ({total_pos} positions)")

if __name__ == '__main__':
    multiprocessing.freeze_support()
    generate_positions()
