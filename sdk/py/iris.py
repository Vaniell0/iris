# SPDX-License-Identifier: MIT
# @file   sdk/py/iris.py
# @brief  Iris Python bindings — ctypes wrapper over the C ABI.
#
# Requires libiris.so compiled with IRIS_JAVA_BACKEND=OFF or ON.
# Set LIBIRIS env var to override the .so path.
#
# Example:
#   import iris
#   tid = iris.register_type("Point", [
#       {"name": "x", "kind": iris.KIND_I32, "offset": 0,  "size": 4},
#       {"name": "y", "kind": iris.KIND_I32, "offset": 4,  "size": 4},
#   ])
#   print(f"Point TypeId = {tid:#x}")

import ctypes
import os
import struct

_lib_path = os.environ.get("LIBIRIS", "libiris.so")
_lib = ctypes.CDLL(_lib_path)

# ── iris_kind_t constants ─────────────────────────────────────────────────────

KIND_VOID  = 0
KIND_BOOL  = 1
KIND_I8    = 2
KIND_I16   = 3
KIND_I32   = 4
KIND_I64   = 5
KIND_F32   = 6
KIND_F64   = 7
KIND_STR   = 8
KIND_BYTES = 9
KIND_CSTR  = 10  # null-terminated string in a fixed char[N] buffer; wire-safe

# ── iris_field_t ──────────────────────────────────────────────────────────────

class IrisField(ctypes.Structure):
    _fields_ = [
        ("name",     ctypes.c_char_p),
        ("kind",     ctypes.c_uint8),
        ("offset",   ctypes.c_uint32),
        ("size",     ctypes.c_uint32),
        ("jni_name", ctypes.c_char_p),
    ]

# ── C function signatures ─────────────────────────────────────────────────────

_lib.iris_type_id_compute.restype  = ctypes.c_uint64
_lib.iris_type_id_compute.argtypes = [
    ctypes.c_char_p, ctypes.POINTER(IrisField), ctypes.c_size_t
]

_lib.iris_type_register.restype  = ctypes.c_uint64
_lib.iris_type_register.argtypes = [
    ctypes.c_char_p, ctypes.POINTER(IrisField), ctypes.c_size_t, ctypes.c_size_t
]

_lib.iris_type_find_by_name.restype  = ctypes.c_uint64
_lib.iris_type_find_by_name.argtypes = [ctypes.c_char_p]

# ── Python helpers ────────────────────────────────────────────────────────────

def _to_c_fields(fields: list[dict]) -> tuple:
    """Convert list of dicts to (IrisField array, count)."""
    n = len(fields)
    arr = (IrisField * n)()
    for i, f in enumerate(fields):
        arr[i].name     = f["name"].encode() if isinstance(f["name"], str) else f["name"]
        arr[i].kind     = int(f["kind"])
        arr[i].offset   = int(f["offset"])
        arr[i].size     = int(f["size"])
        jni = f.get("jni_name") or b""
        arr[i].jni_name = jni.encode() if isinstance(jni, str) else jni
    return arr, n


def type_id_compute(name: str, fields: list[dict]) -> int:
    """Compute FNV-64 TypeId without touching the registry."""
    arr, n = _to_c_fields(fields)
    return _lib.iris_type_id_compute(name.encode(), arr, n)


def register_type(name: str, fields: list[dict], total_size: int = 0) -> int:
    """Register a type. Returns TypeId, or 0 if registry is frozen."""
    arr, n = _to_c_fields(fields)
    if total_size == 0:
        total_size = sum(f["size"] for f in fields)
    return _lib.iris_type_register(name.encode(), arr, n, total_size)


def find_type(name: str) -> int:
    """Look up TypeId by name. Returns 0 if not found."""
    return _lib.iris_type_find_by_name(name.encode())


# ── Smoke-test (run as script) ────────────────────────────────────────────────

if __name__ == "__main__":
    tid = register_type("PyPoint", [
        {"name": "x", "kind": KIND_I32, "offset": 0, "size": 4},
        {"name": "y", "kind": KIND_I32, "offset": 4, "size": 4},
    ])
    assert tid != 0, "register_type returned 0"
    assert find_type("PyPoint") == tid, "find_type mismatch"
    tid2 = register_type("PyPoint", [
        {"name": "x", "kind": KIND_I32, "offset": 0, "size": 4},
        {"name": "y", "kind": KIND_I32, "offset": 4, "size": 4},
    ])
    assert tid2 == tid, "re-registration must return same TypeId"
    print(f"PyPoint TypeId = {tid:#018x}  OK")
