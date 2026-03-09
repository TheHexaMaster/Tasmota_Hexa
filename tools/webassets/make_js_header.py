from pathlib import Path
import sys
import gzip

def write_header(data: bytes, out_path: Path, symbol: str):
    with out_path.open("w", encoding="utf-8", newline="\n") as f:
        f.write("#pragma once\n")
        f.write("#include <Arduino.h>\n\n")
        f.write(f"const uint8_t {symbol}[] PROGMEM = {{\n")

        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            line = ", ".join(f"0x{b:02X}" for b in chunk)
            f.write(f"  {line},\n")

        f.write("};\n")
        f.write(f"const size_t {symbol}_len = {len(data)};\n")

def main():
    if len(sys.argv) != 5:
        print("usage:")
        print("  python make_js_header.py <raw|gzip> <input.js> <output.h> <symbol>")
        sys.exit(1)

    mode = sys.argv[1].strip().lower()
    input_path = Path(sys.argv[2])
    output_path = Path(sys.argv[3])
    symbol = sys.argv[4].strip()

    if not input_path.exists():
        print(f"Input file not found: {input_path}")
        sys.exit(2)

    raw = input_path.read_bytes()

    if mode == "raw":
        data = raw
    elif mode == "gzip":
        data = gzip.compress(raw, compresslevel=9, mtime=0)
    else:
        print("Invalid mode. Use raw or gzip.")
        sys.exit(3)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    write_header(data, output_path, symbol)
    print(f"Written: {output_path}")
    print(f"Bytes: {len(data)}")

if __name__ == "__main__":
    main()