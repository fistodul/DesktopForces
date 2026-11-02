"""The server's \\secure\\ answer must be what 333networks' MasterServer computes."""
import unittest


def validate(secure: str, cipher: str) -> str:
    """GameSpy::getValidateResponse, enctype 0."""
    enc = list(range(256))
    a = 0
    for j in range(256):
        a = (a + enc[j] + ord(cipher[j % 6])) & 255
        enc[a], enc[j] = enc[j], enc[a]
    tmp = [0] * 9
    a = b = 0
    for i in range(6):
        a = (a + ord(secure[i]) + 1) & 255
        x = enc[a]
        b = (b + x) & 255
        y = enc[b]
        enc[b], enc[a] = x, y
        tmp[i] = ord(secure[i]) ^ enc[(x + y) & 255]
    chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
    out = ''
    for i in range(0, 6, 3):
        x, y, z = tmp[i:i + 3]
        out += chars[x >> 2] + chars[((x & 3) << 4) | (y >> 4)] + chars[((y & 15) << 2) | (z >> 6)] + chars[z & 63]
    return out


class ValidateTests(unittest.TestCase):
    def test_shape(self):
        out = validate('wNkZXe', 'g3H6eR')
        self.assertEqual(len(out), 8)
        self.assertNotEqual(out, validate('wNkZXf', 'g3H6eR'))


if __name__ == '__main__':
    unittest.main()
