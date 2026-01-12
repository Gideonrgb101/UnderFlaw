import subprocess
import random
import numpy as np
import os
import struct
import multiprocessing
import time
import ctypes
import sys

# Configuration
NUM_GAMES = 100000000     # 100 Million games (effectively infinite)
TIME_LIMIT = None         # No time limit (run until stopped)
NUM_WORKERS = max(1, multiprocessing.cpu_count() - 2)
SEARCH_DEPTH = 7
OUTPUT_FILE = 'training_data_sparse.bin'

# Constants matching engine
PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING = 0, 1, 2, 3, 4, 5
WHITE, BLACK = 0, 1

# Prevent Windows Sleep
def prevent_sleep():
    if os.name != 'nt': return
    ES_CONTINUOUS = 0x80000000
    ES_SYSTEM_REQUIRED = 0x00000001
    ES_DISPLAY_REQUIRED = 0x00000002
    try:
        ctypes.windll.kernel32.SetThreadExecutionState(
            ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED
        )
        print("Sleep prevention enabled (High Performance Mode).")
    except:
        print("Warning: Could not enable sleep prevention.")

def allow_sleep():
    if os.name != 'nt': return
    ES_CONTINUOUS = 0x80000000
    try:
        ctypes.windll.kernel32.SetThreadExecutionState(ES_CONTINUOUS)
    except: pass

def parse_fen(fen):
    parts = fen.split()
    rows = parts[0].split('/')
    pieces = [] 
    kings = {WHITE: -1, BLACK: -1}
    
    sq = 56 
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
        sq -= 8 
    return pieces, kings

def get_halfkp_index(king_sq, piece_type, piece_color, sq):
    p_idx = piece_type + (0 if piece_color == WHITE else 5)
    return king_sq * 640 + p_idx * 64 + sq

def extract_features(fen):
    try:
        pieces, kings = parse_fen(fen)
    except: return None, None
    
    if kings[WHITE] == -1 or kings[BLACK] == -1:
        return None, None 
        
    white_indices = []
    black_indices = []
    
    w_king_sq = kings[WHITE]
    for sq, color, p_type in pieces:
        idx = get_halfkp_index(w_king_sq, p_type, color, sq)
        white_indices.append(idx)
        
    b_king_mirror = kings[BLACK] ^ 56
    for sq, color, p_type in pieces:
        idx = get_halfkp_index(b_king_mirror, p_type, color ^ 1, sq ^ 56)
        black_indices.append(idx)
        
    return white_indices, black_indices

def play_single_game(game_id):
    # Re-seed to avoid duplicates
    random.seed((os.getpid() * int(time.time() * 1000)) & 0xFFFFFFFF)
    
    # Determine engine path based on OS
    if os.name == 'nt':
        engine_path = 'build/bin/UnderFlaw.exe'
    else:
        # Linux paths (try standard build output)
        if os.path.exists('build/bin/UnderFlaw'):
            engine_path = 'build/bin/UnderFlaw'
        elif os.path.exists('build/UnderFlaw'):
            engine_path = 'build/UnderFlaw'
        elif os.path.exists('./UnderFlaw'):
            engine_path = './UnderFlaw'
        else:
             # Fallback to local
            engine_path = './UnderFlaw'
            
    try:
        engine = subprocess.Popen(
            [engine_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,            
            universal_newlines=True
        )
    except Exception as e:
        print(f"Worker {game_id}: Failed to start engine at {engine_path}: {e}")
        return []

    moves = []
    dataset_entries = []
    
    for move_num in range(random.randint(20, 80)):
        try:
            if moves:
                engine.stdin.write(f'position startpos moves {" ".join(moves)}\n')
            else:
                engine.stdin.write('position startpos\n')
            engine.stdin.flush()
            
            engine.stdin.write('d\n')
            engine.stdin.flush()
            fen = None
            for _ in range(10):
                line = engine.stdout.readline()
                if line.strip() and not line.startswith('info'):
                    fen = line.strip()
                    break
            
            if not fen: break
            
            # Simple soft timeout/check
            engine.stdin.write(f'go depth {SEARCH_DEPTH}\n')
            engine.stdin.flush()
            
            best_move = None
            score = 0
            timeout = 0
            while timeout < 150: # Increased timeout margin
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
                
            if not best_move or best_move == '(none)': break
            
            if move_num > 8:
                dataset_entries.append({'fen': fen, 'score': score})
            
            moves.append(best_move)
        except: break
    
    try:
        engine.stdin.write('quit\n')
        try:
            engine.wait(timeout=1)
        except:
            engine.kill()
    except:
        pass
            
    result = random.choice([1.0, 0.5, 0.0])
    
    processed_data = []
    for entry in dataset_entries:
        w_idx, b_idx = extract_features(entry['fen'])
        if w_idx is None: continue
        processed_data.append({
            'w_idx': w_idx, 'b_idx': b_idx,
            'score': entry['score'], 'result': result
        })
        
    return processed_data

def generate_positions():
    prevent_sleep() 
    print(f"Starting SPARSE generation on {NUM_WORKERS} cores.")
    print(f"Target: Running indefinitely (100M games limit). Stop with Ctrl+C...")
    
    # APPEND mode: Keep existing data, just add new games
    mode = 'ab' if os.path.exists(OUTPUT_FILE) else 'wb'
    if mode == 'ab':
        print(f"Appending to existing {OUTPUT_FILE}...")
    
    start_time = time.time()
    
    with multiprocessing.Pool(processes=NUM_WORKERS) as pool:
        # Use imap_unordered to process tasks one by one without enqueuing all 100M at once
        # This prevents the memory usage from ballooning on cloud runners
        results = pool.imap_unordered(play_single_game, range(NUM_GAMES), chunksize=1)
        
        games_done = 0
        total_pos = 0
        
        with open(OUTPUT_FILE, mode) as f:
            for batch in results:
                # Check time limit
                if TIME_LIMIT and (time.time() - start_time) > TIME_LIMIT:
                    print(f"\nTime limit reached ({TIME_LIMIT}s). Stopping...")
                    pool.terminate()
                    break
                
                if not batch: 
                    continue
                
                games_done += 1
                for item in batch:
                    target = 0.5 * (item['score'] / 1000.0) + 0.5 * (item['result'] - 0.5)
                    target = max(0, min(1, 0.5 + target/2.0))
                    
                    f.write(struct.pack('f', target))
                    f.write(struct.pack('i', len(item['w_idx'])))
                    for idx in item['w_idx']: f.write(struct.pack('i', int(idx)))
                    f.write(struct.pack('i', len(item['b_idx'])))
                    for idx in item['b_idx']: f.write(struct.pack('i', int(idx)))
                
                total_pos += len(batch)
                if games_done % 10 == 0:
                    elapsed = time.time() - start_time
                    size_mb = os.path.getsize(OUTPUT_FILE) / (1024*1024)
                    print(f"Progress: {games_done} games ({total_pos} pos) - {elapsed/60:.1f} min - {size_mb:.2f} MB")
                
                if games_done >= NUM_GAMES:
                    break
    
    allow_sleep() 
    print("Done.")

import argparse

if __name__ == '__main__':
    multiprocessing.freeze_support()
    
    parser = argparse.ArgumentParser(description='Generate NNUE training data.')
    parser.add_argument('--games', type=int, default=100000000, help='Max games to generate')
    parser.add_argument('--time', type=int, default=None, help='Time limit in seconds')
    parser.add_argument('--workers', type=int, default=max(1, multiprocessing.cpu_count() - 2), help='Number of worker processes')
    
    args = parser.parse_args()
    
    NUM_GAMES = args.games
    TIME_LIMIT = args.time
    NUM_WORKERS = args.workers
    
    generate_positions()
