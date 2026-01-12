import sys
import os

def bin_to_c_header(bin_path, header_path, array_name="nnue_data"):
    if not os.path.exists(bin_path):
        print(f"Error: {bin_path} not found")
        return

    with open(bin_path, "rb") as f:
        data = f.read()

    with open(header_path, "w") as f:
        f.write(f"#ifndef NNUE_EMBEDDED_H\n")
        f.write(f"#define NNUE_EMBEDDED_H\n\n")
        f.write(f"#include <stdint.h>\n\n")
        f.write(f"const uint8_t {array_name}[] = {{\n")
        
        for i, byte in enumerate(data):
            f.write(f"0x{byte:02x}, ")
            if (i + 1) % 12 == 0:
                f.write("\n  ")
        
        f.write(f"\n}};\n\n")
        f.write(f"const uint32_t {array_name}_len = {len(data)};\n\n")
        f.write(f"#endif // NNUE_EMBEDDED_H\n")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python embed_net.py <input.nnue> <output.h>")
    else:
        bin_to_c_header(sys.argv[1], sys.argv[2])
