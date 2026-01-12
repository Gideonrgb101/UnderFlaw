
import chess

moves_str = """
d4 d5 Qd3 Qd6 Nc3 c6 e4 Qg6 Nge2 dxe4 Nxe4 f6 Nf4 Qf5
g3 g5 Bh3 g4 Bg2 e5 Qe3 Bb4+ c3 Be7 dxe5 fxe5 Nd3 Nd7
O-O h6 c4 Kd8 b3 Rh7 c5 Qe6 Ba3 Rg7 Kh1 Kc7 Nd6 Bg5
Qe2 Ngf6 Bb2 Qxd6 cxd6+ Kxd6 Nxe5 Rg8 Nxg4 Nxg4 Qxg4 Kc7
h4 h5 Qc4 Bf6 Qxg8 Bxb2 Rab1 Bc3 Qf7 a5 a3
"""

board = chess.Board()

moves = moves_str.split()
print("Replaying game...")

for mp_idx, move_san in enumerate(moves):
    try:
        move = board.push_san(move_san)
    except ValueError:
        print(f"Error parsing move {mp_idx+1}: {move_san}")
        break

print("\nFinal Position (Move 33 Black to move):")
print(board)
print(f"FEN: {board.fen()}")

# Check legality of d7f8
try:
    illegal_move = chess.Move.from_uci("d7f8")
    print("\nAttempting illegal move: d7f8")
    if illegal_move in board.legal_moves:
        print("Wait... d7f8 IS legal according to python-chess?")
    else:
        print("CONFIRMED: d7f8 is ILLEGAL.")
        if board.is_pseudo_legal(illegal_move):
            print("But it IS pseudo-legal (geometry OK).")
            # Check why illegal
            board.push(illegal_move)
            if board.is_valid():
                print("Board state valid?")
            else:
                print("Board state INVALID after move (King in check?)")
            board.pop()
        else:
            print("It is NOT even pseudo-legal.")

except ValueError:
    print("Invalid UCI string")
