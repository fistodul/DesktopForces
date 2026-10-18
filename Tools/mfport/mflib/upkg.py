"""Reader for Mobile Forces Unreal packages (.u .umf .utx .uax .umx), file version 83.

Mobile Forces runs a modified Unreal Engine 1 (build 436, "UE 1.5"). Its package
layout is the UE1 one: a header, a name table, an import table and an export
table, followed by each export's serialized data (tagged properties, then the
class's native data).

This module only does the generic part (tables + tagged properties) plus a few
native readers that the pipeline needs and UCC.exe cannot export for us
(see `readers.py`). Everything UCC *can* export (textures, sounds, static
meshes, whole levels as T3D) is exported with UCC under Wine instead, because
that is guaranteed to match what the game sees (see `ucc.py`).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

PACKAGE_TAG = 0x9E2A83C1

# UE1 property types (low nibble of the info byte)
PT_BYTE, PT_INT, PT_BOOL, PT_FLOAT, PT_OBJECT, PT_NAME, PT_STRING, PT_CLASS, \
    PT_ARRAY, PT_STRUCT, PT_VECTOR, PT_ROTATOR, PT_STR, PT_MAP, PT_FIXEDARRAY = range(1, 16)


class Reader:
    """Little-endian cursor over a bytes buffer with UE1 compact indices."""

    def __init__(self, data: bytes, pos: int = 0):
        self.b = data
        self.p = pos

    def u8(self) -> int:
        v = self.b[self.p]
        self.p += 1
        return v

    def u16(self) -> int:
        v, = struct.unpack_from('<H', self.b, self.p)
        self.p += 2
        return v

    def i16(self) -> int:
        v, = struct.unpack_from('<h', self.b, self.p)
        self.p += 2
        return v

    def u32(self) -> int:
        v, = struct.unpack_from('<I', self.b, self.p)
        self.p += 4
        return v

    def i32(self) -> int:
        v, = struct.unpack_from('<i', self.b, self.p)
        self.p += 4
        return v

    def u64(self) -> int:
        v, = struct.unpack_from('<Q', self.b, self.p)
        self.p += 8
        return v

    def f32(self) -> float:
        v, = struct.unpack_from('<f', self.b, self.p)
        self.p += 4
        return v

    def vec(self) -> tuple[float, float, float]:
        v = struct.unpack_from('<3f', self.b, self.p)
        self.p += 12
        return v

    def raw(self, n: int) -> bytes:
        v = self.b[self.p:self.p + n]
        self.p += n
        return v

    def index(self) -> int:
        """UE1 compact index: sign bit + 6 bits, then 7 bits per continuation byte."""
        x = self.u8()
        neg = x & 0x80
        v = x & 0x3F
        if x & 0x40:
            shift = 6
            while True:
                x = self.u8()
                v |= (x & 0x7F) << shift
                shift += 7
                if not x & 0x80:
                    break
        return -v if neg else v

    def fstring(self) -> str:
        n = self.index()
        if n == 0:
            return ''
        if n < 0:  # UTF-16 (not seen in MF, kept for completeness)
            s = self.raw(-n * 2).decode('utf-16-le')
        else:
            s = self.raw(n).decode('latin-1')
        return s.rstrip('\0')


@dataclass
class Import:
    class_package: str
    class_name: str
    outer: int
    name: str


@dataclass
class Export:
    index: int          # 1-based export index
    class_ref: int
    super_ref: int
    outer: int
    name: str
    flags: int
    size: int
    offset: int


@dataclass
class UObjectData:
    """Tagged properties + offset where the native part begins."""
    props: dict = field(default_factory=dict)
    native_offset: int = 0


class Package:
    def __init__(self, path: str):
        self.path = path
        with open(path, 'rb') as f:
            self.data = f.read()
        r = Reader(self.data)
        if r.u32() != PACKAGE_TAG:
            raise ValueError(f'{path}: not an Unreal package')
        self.version = r.u16()
        self.licensee = r.u16()
        self.flags = r.u32()
        name_count, name_off = r.u32(), r.u32()
        export_count, export_off = r.u32(), r.u32()
        import_count, import_off = r.u32(), r.u32()

        self.names: list[str] = []
        r.p = name_off
        for _ in range(name_count):
            if self.version >= 64:
                self.names.append(r.fstring())
            else:
                end = self.data.index(b'\0', r.p)
                self.names.append(self.data[r.p:end].decode('latin-1'))
                r.p = end + 1
            r.u32()  # object flags of the name

        self.imports: list[Import] = []
        r.p = import_off
        for _ in range(import_count):
            cpkg, cname = r.index(), r.index()
            outer = r.i32()
            name = r.index()
            self.imports.append(Import(self.names[cpkg], self.names[cname], outer, self.names[name]))

        self.exports: list[Export] = []
        r.p = export_off
        for i in range(export_count):
            cls, sup = r.index(), r.index()
            outer = r.i32()
            name = r.index()
            flags = r.u32()
            size = r.index()
            off = r.index() if size > 0 else 0
            self.exports.append(Export(i + 1, cls, sup, outer, self.names[name], flags, size, off))

    # --- object references -------------------------------------------------
    def ref_name(self, ref: int) -> str | None:
        if ref == 0:
            return None
        if ref < 0:
            return self.imports[-ref - 1].name
        return self.exports[ref - 1].name

    def ref_path(self, ref: int) -> str | None:
        """Dotted path of an object reference (negative = import, positive = export)."""
        if ref == 0:
            return None
        parts = []
        while ref:
            if ref < 0:
                imp = self.imports[-ref - 1]
                parts.append(imp.name)
                ref = imp.outer
            else:
                exp = self.exports[ref - 1]
                parts.append(exp.name)
                ref = exp.outer
        return '.'.join(reversed(parts))

    @property
    def name(self) -> str:
        """Package name = file name without extension (what other packages import it as)."""
        import os
        return os.path.splitext(os.path.basename(self.path))[0]

    def full_path(self, ref: int) -> str | None:
        """Like ref_path, but exports are prefixed with this package's name, so the
        result is the same string other packages would use to import the object."""
        path = self.ref_path(ref)
        if path is None or ref < 0:
            return path
        return f'{self.name}.{path}'

    def class_of(self, exp: Export) -> str:
        return 'Class' if exp.class_ref == 0 else self.ref_name(exp.class_ref)

    def exports_of(self, class_name: str) -> list[Export]:
        cl = class_name.lower()
        return [e for e in self.exports if self.class_of(e).lower() == cl]

    def find(self, name: str, class_name: str | None = None) -> Export | None:
        for e in self.exports:
            if e.name.lower() == name.lower() and (class_name is None or self.class_of(e).lower() == class_name.lower()):
                return e
        return None

    # --- tagged properties ---------------------------------------------------
    def read_object(self, exp: Export, has_stack: bool = False) -> UObjectData:
        """Parse an export's tagged properties. Returns props + native data offset.

        Values are decoded for simple types; structs / arrays keep raw bytes
        (decode them in the caller when needed).
        """
        r = Reader(self.data, exp.offset)
        if has_stack or exp.flags & 0x02000000:  # RF_HasStack: FStateFrame
            node = r.index()
            r.index()            # StateNode
            r.u64()              # ProbeMask
            r.u32()              # LatentAction
            if node != 0:
                r.index()        # code Offset
        props: dict = {}
        while True:
            name = self.names[r.index()]
            if name == 'None':
                break
            info = r.u8()
            ptype = info & 0x0F
            size_code = (info >> 4) & 0x07
            is_array = bool(info & 0x80)
            struct_name = self.names[r.index()] if ptype == PT_STRUCT else None
            size = {0: 1, 1: 2, 2: 4, 3: 12, 4: 16}.get(size_code)
            if size is None:
                size = {5: r.u8, 6: r.u16, 7: r.u32}[size_code]()
            array_index = 0
            if is_array and ptype != PT_BOOL:
                b = r.u8()
                if b & 0x80 == 0:
                    array_index = b
                elif b & 0xC0 == 0x80:
                    array_index = ((b & 0x7F) << 8) | r.u8()
                else:
                    array_index = ((b & 0x3F) << 24) | (r.u8() << 16) | (r.u8() << 8) | r.u8()
            start = r.p
            if ptype == PT_BOOL:
                value = is_array
            elif ptype == PT_BYTE:
                value = r.u8()
            elif ptype == PT_INT:
                value = r.i32()
            elif ptype == PT_FLOAT:
                value = r.f32()
            elif ptype in (PT_OBJECT, PT_CLASS):
                value = self.ref_path(r.index())
            elif ptype == PT_NAME:
                value = self.names[r.index()]
            elif ptype == PT_STR:
                value = r.fstring()
            elif ptype == PT_VECTOR:
                value = r.vec()
            elif ptype == PT_ROTATOR:
                value = (r.i32(), r.i32(), r.i32())
            else:
                value = (struct_name, r.raw(size))
            r.p = start + size if ptype != PT_BOOL else r.p
            key = name if array_index == 0 and name not in props else f'{name}[{array_index}]'
            props[key] = value
        return UObjectData(props, r.p)
