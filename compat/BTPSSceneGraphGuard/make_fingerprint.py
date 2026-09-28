"""Record the exact supported local BTPS DLL and its mapped traversal code."""
import hashlib, pathlib, struct, sys
source = pathlib.Path(sys.argv[1])
data = source.read_bytes()
pe = struct.unpack_from('<I', data, 0x3c)[0]
count = struct.unpack_from('<H', data, pe + 6)[0]
opt = struct.unpack_from('<H', data, pe + 20)[0]
rva, length = 0x67CB0, 0xC7
for i in range(count):
    virtual_size, start, raw_size, raw = struct.unpack_from('<IIII', data, pe + 24 + opt + 40*i + 8)
    if start <= rva and rva + length <= start + raw_size:
        function = data[raw+rva-start:raw+rva-start+length]
        break
else:
    raise RuntimeError('Traversal is outside the file-backed sections')
assert function[:14].hex() == '48895c241048896c241856574156'
digest = hashlib.sha256(data).hexdigest()
assert digest == '42e9107ed191cd92dad429af31bc181f7f55b21c2694e0f993e0c6e5e51ef4d7'
def values(blob): return ', '.join(f'0x{v:02X}' for v in blob)
output = f'''#pragma once
#include <array>
#include <cstddef>
namespace Supported {{
inline constexpr std::size_t fileSize = {len(data)};
inline constexpr std::size_t functionRva = 0x{rva:X};
inline constexpr std::array<unsigned char, 32> sha256{{{values(bytes.fromhex(digest))}}};
inline constexpr std::array<unsigned char, {length}> functionBytes{{{values(function)}}};
}}
'''
pathlib.Path(__file__).with_name('supported_btps.h').write_text(output)
print(f'Supported BTPS SHA-256 {digest}; traversal {length} bytes at +0x{rva:X}')
