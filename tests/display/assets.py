"""Authored bitmap font and independent integer test-card oracle; no font download."""
from pathlib import Path
import struct

# Original 5x7 diagnostic glyph patterns, scaled by 3. ASCII subset only.
GLYPHS = {
 ' ': ['00000'] * 7,
 '0': ['01110','10001','10011','10101','11001','10001','01110'],
 '1': ['00100','01100','00100','00100','00100','00100','01110'],
 '2': ['01110','10001','00001','00010','00100','01000','11111'],
 '3': ['11110','00001','00001','01110','00001','00001','11110'],
 '4': ['00010','00110','01010','10010','11111','00010','00010'],
 '5': ['11111','10000','10000','11110','00001','00001','11110'],
 '6': ['01110','10000','10000','11110','10001','10001','01110'],
 '7': ['11111','00001','00010','00100','01000','01000','01000'],
 '8': ['01110','10001','10001','01110','10001','10001','01110'],
 '9': ['01110','10001','10001','01111','00001','00001','01110'],
 ':': ['00000','00100','00100','00000','00100','00100','00000'],
 'A': ['01110','10001','10001','11111','10001','10001','10001'],
 'B': ['11110','10001','10001','11110','10001','10001','11110'],
 'C': ['01111','10000','10000','10000','10000','10000','01111'],
 'D': ['11110','10001','10001','10001','10001','10001','11110'],
 'E': ['11111','10000','10000','11110','10000','10000','11111'],
 'F': ['11111','10000','10000','11110','10000','10000','10000'],
 'G': ['01111','10000','10000','10111','10001','10001','01111'],
 'I': ['01110','00100','00100','00100','00100','00100','01110'],
 'L': ['10000','10000','10000','10000','10000','10000','11111'],
 'M': ['10001','11011','10101','10101','10001','10001','10001'],
 'N': ['10001','11001','10101','10011','10001','10001','10001'],
 'P': ['11110','10001','10001','11110','10000','10000','10000'],
 'R': ['11110','10001','10001','11110','10100','10010','10001'],
 'U': ['10001','10001','10001','10001','10001','10001','01110'],
 'X': ['10001','10001','01010','00100','01010','10001','10001'],
 'Y': ['10001','10001','01010','00100','00100','00100','00100'],
}

def font_bytes():
    items = sorted(GLYPHS.items())
    header = struct.pack('<IHH8B', 0x41464344, 3, len(items), 18, 24, 21, 24, 0, 0, 1, 0)
    cmap = b''.join(struct.pack('<IHBB', ord(c), i, 18, 0) for i, (c, _) in enumerate(items))
    bitmap = bytearray()
    for _, rows in items:
        for y in range(24):
            for x in range(18):
                bitmap.append(255 if x < 15 and y < 21 and rows[y // 3][x // 3] == '1' else 0)
    return header + cmap + bitmap

def reference(height, turn=0):
    """Expected RGB bytes from authored geometry, independent of Core/Presenter."""
    width = 1024
    image = bytearray(bytes((16, 20, 28)) * width * height)
    def rect(x, y, w, h, color):
        assert 0 <= x <= x+w <= width and 0 <= y <= y+h <= height
        row = bytes(color) * w
        for yy in range(y, y+h): image[(yy*width+x)*3:(yy*width+x+w)*3] = row
    def text(x, y, message):
        for c in message:
            for yy, row in enumerate(GLYPHS[c]):
                for xx, bit in enumerate(row):
                    if bit == '1': rect(x+xx*3, y+yy*3, 3, 3, (255,255,255))
            x += 18
    rect(0,0,width,1,(255,255,255)); rect(0,height-1,width,1,(255,255,255))
    rect(0,0,1,height,(255,255,255)); rect(width-1,0,1,height,(255,255,255))
    rect(1,1,8,8,(255,0,0)); rect(width-9,1,8,8,(0,255,0))
    rect(1,height-9,8,8,(0,0,255)); rect(width-9,height-9,8,8,(255,255,0))
    text(32,28,f'FRAMEBUFFER {width}X{height}')
    text(32,72,'RED'); text(344,72,'GREEN'); text(656,72,'BLUE')
    rect(32,104,280,96,(255,0,0)); rect(344,104,280,96,(0,255,0)); rect(656,104,280,96,(0,0,255))
    text(32,232,'CLIP'); text(344,232,'IMAGE'); text(504,232,'GRAY')
    rect(32,272,280,160,(48,48,48)); rect(272,322,40,70,(255,255,0))
    rect(344,272,64,64,(255,0,0)); rect(408,272,64,64,(0,255,0))
    rect(344,336,64,64,(0,0,255)); rect(408,336,64,64,(255,255,255))
    for i in range(16): rect(504+i*27,272,27,128,(i*17,)*3)
    text(32,height-112,'RGB 1:1'); rect(32,height-68,width-64,20,(48,48,48))
    rect(32+(turn*8)%(width-68),height-68,4,20,(255,255,255))
    return bytes(image)

def write_assets(destination):
    destination = Path(destination); destination.mkdir(parents=True, exist_ok=True)
    (destination / 'display-font.bin').write_bytes(font_bytes())
    (destination / 'display-scene.js').write_bytes(Path(__file__).with_name('display-scene.js').read_bytes())
    for h in (600,800):
        for turn in (0,2):
            (destination / f'expected-{h}-{turn}.ppm').write_bytes(f'P6\n1024 {h}\n255\n'.encode() + reference(h,turn))
