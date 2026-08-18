#!/usr/bin/env python3
"""
Falcom YCO (Ys Collision Object) Binary Format Parser.
Parses walkable ground (__s.yco), wall/barrier (__w.yco), and camera/trigger (__c.yco) collision geometry.
Extracts 3D collision triangles, surface normals, and material/surface flags.
"""

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple
import numpy as np

@dataclass
class CollisionTriangle:
    v0: Tuple[float, float, float]
    v1: Tuple[float, float, float]
    v2: Tuple[float, float, float]
    normal: Tuple[float, float, float]
    plane_d: float
    flags: int

@dataclass
class YcoCollisionMesh:
    filename: str
    collision_type: str  # "walkable", "wall", "camera"
    triangles: List[CollisionTriangle]
    positions: np.ndarray  # (N*3, 3) float32
    normals: np.ndarray    # (N*3, 3) float32
    indices: np.ndarray    # (N, 3) uint32

class YcoParser:
    POLYGON_RECORD_SIZE = 96

    @classmethod
    def parse_file(cls, path: Path) -> YcoCollisionMesh:
        path = Path(path)
        data = path.read_bytes()
        return cls.parse_bytes(data, filename=path.name)

    @classmethod
    def parse_bytes(cls, data: bytes, filename: str = "collision.yco") -> YcoCollisionMesh:
        if len(data) < 28:
            raise ValueError(f"YCO file too short: {len(data)} bytes")

        magic = data[:4]
        if magic != b"YCO\x00":
            raise ValueError(f"Invalid YCO magic: {magic}")

        ver, poly_count = struct.unpack("<2I", data[4:12])

        # Determine collision type from filename
        fn_lower = filename.lower()
        if "__s" in fn_lower or "_s." in fn_lower:
            coll_type = "walkable"
        elif "__w" in fn_lower or "_w." in fn_lower:
            coll_type = "wall"
        elif "__c" in fn_lower or "_c." in fn_lower:
            coll_type = "camera"
        else:
            coll_type = "generic"

        triangles: List[CollisionTriangle] = []
        pos_list = []
        norm_list = []
        idx_list = []

        pos = 0x001C
        for i in range(poly_count):
            if pos + cls.POLYGON_RECORD_SIZE > len(data):
                break
            chunk = data[pos:pos + cls.POLYGON_RECORD_SIZE]

            # v0, v1, v2 (3 x 12 bytes = 36 bytes)
            x0, y0, z0, x1, y1, z1, x2, y2, z2 = struct.unpack_from("<9f", chunk, 0)
            # normal (12 bytes)
            nx, ny, nz = struct.unpack_from("<3f", chunk, 36)
            # plane_d (4 bytes)
            plane_d = struct.unpack_from("<f", chunk, 48)[0]
            # flags (at 0x48 / offset 72)
            flags = struct.unpack_from("<I", chunk, 72)[0]

            v0 = (x0, y0, z0)
            v1 = (x1, y1, z1)
            v2 = (x2, y2, z2)
            normal = (nx, ny, nz)

            triangles.append(CollisionTriangle(
                v0=v0, v1=v1, v2=v2, normal=normal, plane_d=plane_d, flags=flags
            ))

            base_idx = len(pos_list)
            pos_list.extend([v0, v1, v2])
            norm_list.extend([normal, normal, normal])
            idx_list.append([base_idx, base_idx + 1, base_idx + 2])

            pos += cls.POLYGON_RECORD_SIZE

        positions = np.array(pos_list, dtype=np.float32) if pos_list else np.zeros((0, 3), dtype=np.float32)
        normals = np.array(norm_list, dtype=np.float32) if norm_list else np.zeros((0, 3), dtype=np.float32)
        indices = np.array(idx_list, dtype=np.uint32) if idx_list else np.zeros((0, 3), dtype=np.uint32)

        return YcoCollisionMesh(
            filename=filename,
            collision_type=coll_type,
            triangles=triangles,
            positions=positions,
            normals=normals,
            indices=indices
        )

if __name__ == "__main__":
    import sys
    p = Path(sys.argv[1])
    coll = YcoParser.parse_file(p)
    print(f"Loaded {coll.filename}: type={coll.collision_type}, {len(coll.triangles)} triangles ({len(coll.positions)} vertices)")
    if len(coll.positions) > 0:
        print(f"  Bounds min: {coll.positions.min(axis=0)}")
        print(f"  Bounds max: {coll.positions.max(axis=0)}")
