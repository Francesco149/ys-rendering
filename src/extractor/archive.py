#!/usr/bin/env python3
"""
Falcom Ys VI / Ys Origin / Ys Felghana Archive (NA/NI) Extractor.
Parses NNI header, decrypts index with LCG cipher, decompresses .z chunks (zlib).
"""

import struct
import zlib
from pathlib import Path
from typing import List, Dict, Any, Optional

def decrypt_ni_bytes(data: bytearray) -> bytearray:
    num = 0x7C53F961
    out = bytearray(len(data))
    for i in range(len(data)):
        num = (num * 0x3D09) & 0xFFFFFFFF
        shift = (num >> 16) & 0xFF
        out[i] = (data[i] - shift) & 0xFF
    return out

class NiArchive:
    def __init__(self, ni_path: Path, na_path: Optional[Path] = None):
        self.ni_path = Path(ni_path)
        if na_path:
            self.na_path = Path(na_path)
        else:
            self.na_path = self.ni_path.with_suffix(".na")
        self.entries: List[Dict[str, Any]] = []
        self._parse()

    def _parse(self):
        with open(self.ni_path, "rb") as f:
            header_data = f.read(16)
            if len(header_data) < 16:
                raise ValueError("NI header too short")
            sign, tot, namesz, zero = struct.unpack("<4I", header_data)
            if sign != 0x00494E4E:
                raise ValueError(f"Invalid NI magic: 0x{sign:08X} (expected NNI\\0)")

            info_raw = bytearray(f.read(tot * 16))
            if len(info_raw) != tot * 16:
                raise ValueError("Unexpected end of NI info table")
            info_dec = decrypt_ni_bytes(info_raw)

            names_raw = bytearray(f.read(namesz))
            if len(names_raw) != namesz:
                raise ValueError("Unexpected end of NI names table")
            names_dec = decrypt_ni_bytes(names_raw)

        for i in range(tot):
            num, size, offset, nameoff = struct.unpack_from("<4I", info_dec, i * 16)
            if nameoff >= len(names_dec):
                continue
            null_pos = names_dec.find(b"\x00", nameoff)
            if null_pos == -1:
                null_pos = len(names_dec)
            name = names_dec[nameoff:null_pos].decode("cp932", errors="replace")

            is_compressed = name.lower().endswith(".z")
            clean_name = name[:-2] if is_compressed else name

            self.entries.append({
                "index": i,
                "num": num,
                "size": size,
                "offset": offset,
                "nameoff": nameoff,
                "archived_name": name,
                "clean_name": clean_name,
                "is_compressed": is_compressed,
            })

    def extract_file(self, entry: Dict[str, Any], na_file) -> bytes:
        na_file.seek(entry["offset"])
        raw_data = na_file.read(entry["size"])
        if len(raw_data) != entry["size"]:
            raise IOError(f"Read error for {entry['clean_name']}")

        if entry["is_compressed"]:
            if len(raw_data) < 8:
                return b""
            expected_crc, uncomp_size = struct.unpack("<2I", raw_data[:8])
            comp_data = raw_data[8:]
            decomp = zlib.decompress(comp_data)
            if len(decomp) != uncomp_size:
                print(f"Warning: size mismatch for {entry['clean_name']}: got {len(decomp)}, expected {uncomp_size}")
            return decomp
        else:
            return raw_data

    def extract_all(self, out_dir: Path, filter_pattern: Optional[str] = None):
        out_dir = Path(out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        with open(self.na_path, "rb") as na_file:
            for entry in self.entries:
                name = entry["clean_name"]
                if filter_pattern and filter_pattern.lower() not in name.lower():
                    continue
                data = self.extract_file(entry, na_file)
                norm_path = Path(*name.replace("\\", "/").split("/"))
                target_path = out_dir / norm_path
                target_path.parent.mkdir(parents=True, exist_ok=True)
                with open(target_path, "wb") as out_f:
                    out_f.write(data)
if __name__ == "__main__":
    import sys
    ni = NiArchive(Path(sys.argv[1]))
    print(f"Loaded {len(ni.entries)} entries from {sys.argv[1]}")
    for e in ni.entries[:20]:
        print(f"  {e['num']:08X} | {e['clean_name']} | size={e['size']} (compressed={e['is_compressed']})")
