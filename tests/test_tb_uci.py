import subprocess
import time

def test_syzygy_option():
    print("Testing SyzygyPath UCI option...")
    process = subprocess.Popen(
        ['build/bin/chess_engine.exe'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1
    )

    def send(cmd):
        # print(f"> {cmd}")
        process.stdin.write(f"{cmd}\n")
        process.stdin.flush()

    def expect(pattern, timeout=2):
        start = time.time()
        while time.time() - start < timeout:
            line = process.stdout.readline()
            if line:
                # print(f"< {line.strip()}")
                if pattern in line:
                    return line.strip()
            else:
                break
        return None

    send("uci")
    if expect("option name SyzygyPath type string"):
        print("PASS: SyzygyPath option found.")
    else:
        print("FAIL: SyzygyPath option NOT found.")

    send("setoption name SyzygyPath value dummy_path")
    # Our engine should print "info string Syzygy tablebases found: 0 pieces" or similar
    # or just not error out if path is invalid.
    # Actually, tb_init prints: "info string Syzygy tablebases found: %d pieces"
    # But ONLY if TB_LARGEST > 0.
    # If it fails, it might just return false.
    
    send("isready")
    if expect("readyok"):
        print("PASS: Engine remained stable after setting SyzygyPath.")
    else:
        print("FAIL: Engine did not respond after setting SyzygyPath.")

    send("quit")
    process.wait()

if __name__ == "__main__":
    test_syzygy_option()
