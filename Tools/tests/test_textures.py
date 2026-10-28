"""Texture decoding must preserve group-qualified palette references and soft alpha."""
from pathlib import Path
from types import SimpleNamespace
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from mfport.mflib.textures import decode_p8


class PaletteTests(unittest.TestCase):
    def test_grouped_palette_does_not_use_same_named_root_palette(self):
        wrong = bytes([3, 140, 140, 140, 255, 150, 150, 150, 255, 160, 160, 160, 255])
        correct = bytes([3, 0, 0, 0, 0, 72, 5, 2, 128, 72, 5, 2, 255])
        mip = bytes([1, 3, 0, 1, 2]) + struct.pack('<II', 3, 1) + bytes([2, 0])
        exports = [SimpleNamespace(index=1, offset=0),
                   SimpleNamespace(index=2, offset=len(wrong)),
                   SimpleNamespace(index=3, offset=len(wrong) + len(correct))]
        package = SimpleNamespace(
            data=wrong + correct + mip, exports=exports, version=62,
            exports_of=lambda kind: exports[:2],
            ref_path=lambda index: ['Palette25', 'BloodSpang.Palette25'][index - 1],
            read_object=lambda export: SimpleNamespace(native_offset=export.offset),
        )
        image = decode_p8(package, {'palette': 'bloodspang.palette25', 'export': 3}, True)
        self.assertEqual(list(image.getdata()), [(0, 0, 0, 0), (72, 5, 2, 128), (72, 5, 2, 255)])


if __name__ == '__main__':
    unittest.main()
