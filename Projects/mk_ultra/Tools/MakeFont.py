"""TODO(engine UI): temporary. Delete this script and Content/UI once the engine's C++ UI framework
(Blazor-style) lands; see Source/Hud.h.

Generates the HUD font: Content/UI/Font.png and one Unlit material per character in Content/UI/Glyphs.

The engine has no text rendering, so the HUD draws each character as a sprite whose material picks
one cell of the font atlas with Tiling and Offset. Hand drawn 5x7 pixel glyphs, so no font licence
to worry about. Run from anywhere: python3 Projects/mk_ultra/Tools/MakeFont.py
"""
import os
import struct
import zlib

GLYPHS = {
    ' ': ['.....'] * 7,
    '0': ['.###.', '#...#', '#..##', '#.#.#', '##..#', '#...#', '.###.'],
    '1': ['..#..', '.##..', '..#..', '..#..', '..#..', '..#..', '.###.'],
    '2': ['.###.', '#...#', '....#', '...#.', '..#..', '.#...', '#####'],
    '3': ['####.', '....#', '....#', '.###.', '....#', '....#', '####.'],
    '4': ['...#.', '..##.', '.#.#.', '#..#.', '#####', '...#.', '...#.'],
    '5': ['#####', '#....', '####.', '....#', '....#', '#...#', '.###.'],
    '6': ['..##.', '.#...', '#....', '####.', '#...#', '#...#', '.###.'],
    '7': ['#####', '....#', '...#.', '..#..', '.#...', '.#...', '.#...'],
    '8': ['.###.', '#...#', '#...#', '.###.', '#...#', '#...#', '.###.'],
    '9': ['.###.', '#...#', '#...#', '.####', '....#', '...#.', '.##..'],
    'A': ['.###.', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    'B': ['####.', '#...#', '#...#', '####.', '#...#', '#...#', '####.'],
    'C': ['.###.', '#...#', '#....', '#....', '#....', '#...#', '.###.'],
    'D': ['###..', '#..#.', '#...#', '#...#', '#...#', '#..#.', '###..'],
    'E': ['#####', '#....', '#....', '####.', '#....', '#....', '#####'],
    'F': ['#####', '#....', '#....', '####.', '#....', '#....', '#....'],
    'G': ['.###.', '#...#', '#....', '#.###', '#...#', '#...#', '.####'],
    'H': ['#...#', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    'I': ['.###.', '..#..', '..#..', '..#..', '..#..', '..#..', '.###.'],
    'J': ['..###', '...#.', '...#.', '...#.', '...#.', '#..#.', '.##..'],
    'K': ['#...#', '#..#.', '#.#..', '##...', '#.#..', '#..#.', '#...#'],
    'L': ['#....', '#....', '#....', '#....', '#....', '#....', '#####'],
    'M': ['#...#', '##.##', '#.#.#', '#.#.#', '#...#', '#...#', '#...#'],
    'N': ['#...#', '#...#', '##..#', '#.#.#', '#..##', '#...#', '#...#'],
    'O': ['.###.', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
    'P': ['####.', '#...#', '#...#', '####.', '#....', '#....', '#....'],
    'Q': ['.###.', '#...#', '#...#', '#...#', '#.#.#', '#..#.', '.##.#'],
    'R': ['####.', '#...#', '#...#', '####.', '#.#..', '#..#.', '#...#'],
    'S': ['.####', '#....', '#....', '.###.', '....#', '....#', '####.'],
    'T': ['#####', '..#..', '..#..', '..#..', '..#..', '..#..', '..#..'],
    'U': ['#...#', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
    'V': ['#...#', '#...#', '#...#', '#...#', '#...#', '.#.#.', '..#..'],
    'W': ['#...#', '#...#', '#...#', '#.#.#', '#.#.#', '#.#.#', '.#.#.'],
    'X': ['#...#', '#...#', '.#.#.', '..#..', '.#.#.', '#...#', '#...#'],
    'Y': ['#...#', '#...#', '.#.#.', '..#..', '..#..', '..#..', '..#..'],
    'Z': ['#####', '....#', '...#.', '..#..', '.#...', '#....', '#####'],
    '.': ['.....', '.....', '.....', '.....', '.....', '.##..', '.##..'],
    ',': ['.....', '.....', '.....', '.....', '.##..', '..#..', '.#...'],
    ':': ['.....', '.##..', '.##..', '.....', '.##..', '.##..', '.....'],
    '%': ['##...', '##..#', '...#.', '..#..', '.#...', '#..##', '...##'],
    '/': ['.....', '....#', '...#.', '..#..', '.#...', '#....', '.....'],
    '-': ['.....', '.....', '.....', '#####', '.....', '.....', '.....'],
    '+': ['.....', '..#..', '..#..', '#####', '..#..', '..#..', '.....'],
    '(': ['...#.', '..#..', '.#...', '.#...', '.#...', '..#..', '...#.'],
    ')': ['.#...', '..#..', '...#.', '...#.', '...#.', '..#..', '.#...'],
    '[': ['.###.', '.#...', '.#...', '.#...', '.#...', '.#...', '.###.'],
    ']': ['.###.', '...#.', '...#.', '...#.', '...#.', '...#.', '.###.'],
    '!': ['..#..', '..#..', '..#..', '..#..', '..#..', '.....', '..#..'],
    '?': ['.###.', '#...#', '....#', '...#.', '..#..', '.....', '..#..'],
    '>': ['.#...', '..#..', '...#.', '....#', '...#.', '..#..', '.#...'],
    '<': ['...#.', '..#..', '.#...', '#....', '.#...', '..#..', '...#.'],
    '^': ['..#..', '.#.#.', '#...#', '.....', '.....', '.....', '.....'],
    '=': ['.....', '.....', '#####', '.....', '#####', '.....', '.....'],
    "'": ['..#..', '..#..', '.#...', '.....', '.....', '.....', '.....'],
    '"': ['.#.#.', '.#.#.', '.....', '.....', '.....', '.....', '.....'],
    '#': ['.#.#.', '.#.#.', '#####', '.#.#.', '#####', '.#.#.', '.#.#.'],
    '*': ['.....', '#.#.#', '.###.', '#####', '.###.', '#.#.#', '.....'],
    '_': ['.....', '.....', '.....', '.....', '.....', '.....', '#####'],
}
# Lower case draws as upper case.

COLUMNS, ROWS, FIRST = 16, 6, 32  # ASCII 32..127

# Material file names for symbols; letters and digits use themselves (A.material, 7.material).
# Keep in sync with GlyphName in Source/Hud.cpp.
SYMBOL_NAMES = {
    '.': 'Period', ',': 'Comma', ':': 'Colon', '%': 'Percent', '/': 'Slash', '-': 'Minus', '+': 'Plus',
    '(': 'OpenParen', ')': 'CloseParen', '[': 'OpenBracket', ']': 'CloseBracket', '!': 'Exclamation',
    '?': 'Question', '>': 'Greater', '<': 'Less', '^': 'Caret', '=': 'Equals', "'": 'Apostrophe',
    '"': 'Quote', '#': 'Hash', '*': 'Star', '_': 'Underscore',
}
CELL = 8          # font pixels per cell (glyph is 5x7 at column 1, row 0)
SCALE = 8         # texels per font pixel, so linear filtering stays crisp
SIZE_X, SIZE_Y = COLUMNS * CELL * SCALE, ROWS * CELL * SCALE


def write_png(path, width, height, rgba):
    rows = b''.join(b'\x00' + bytes(rgba[y * width * 4:(y + 1) * width * 4]) for y in range(height))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)


def main():
    content = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Content', 'UI')
    os.makedirs(os.path.join(content, 'Glyphs'), exist_ok=True)

    pixels = bytearray([255, 255, 255, 0] * SIZE_X * SIZE_Y)  # white, fully transparent
    for char, rows in GLYPHS.items():
        assert len(rows) == 7 and all(len(r) == 5 for r in rows), char
        index = ord(char) - FIRST
        cx, cy = (index % COLUMNS) * CELL, (index // COLUMNS) * CELL
        for gy, row in enumerate(rows):
            for gx, bit in enumerate(row):
                if bit != '#':
                    continue
                for sy in range(SCALE):
                    for sx in range(SCALE):
                        x = (cx + 1 + gx) * SCALE + sx
                        y = (cy + gy) * SCALE + sy
                        pixels[(y * SIZE_X + x) * 4 + 3] = 255
    write_png(os.path.join(content, 'Font.png'), SIZE_X, SIZE_Y, pixels)

    glyphs = os.path.join(content, 'Glyphs')
    for old in os.listdir(glyphs):
        os.remove(os.path.join(glyphs, old))
    for char in GLYPHS:
        if char == ' ':
            continue  # the HUD skips spaces
        index = ord(char) - FIRST
        name = char if char.isalnum() else SYMBOL_NAMES[char]
        with open(os.path.join(glyphs, f'{name}.material'), 'w') as f:
            f.write('# FirstPrinciples material\n')
            f.write('Shader "Unlit"\nBaseColor 1 1 1 1\nBaseColorMap "UI/Font.png"\n')
            f.write(f'Tiling {1 / COLUMNS} {1 / ROWS}\nOffset {(index % COLUMNS) / COLUMNS} {(index // COLUMNS) / ROWS}\n')
            f.write('EmissiveColor 0 0 0 1\nEmissiveIntensity 0\nAlphaCutoff 0.5\n')


if __name__ == '__main__':
    main()
