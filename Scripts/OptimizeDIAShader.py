import argparse
from pathlib import Path
import struct
import subprocess


TRACE = b'Trace(vf3;vf3;f1;b1;b1;'
SOFTWARE_TRACE = b'TraceSoftware(vf3;vf3;f1;b1;b1;'
TRACE_PASSES = {'DIACache.frag.spv', 'DIATrace.frag.spv', 'DIATraceLitOne.frag.spv',
                'DIAResolve.frag.spv', 'DIAResolveFallback.frag.spv', 'DIAResolveFallbackLit.frag.spv'}
RAY_PASSES = {name.replace('.frag.spv', 'RT.frag.spv') for name in TRACE_PASSES}
TRACE_PASSES |= RAY_PASSES


def check_trace_count(name, count):
    if count != int(name in TRACE_PASSES):
        raise ValueError(f'Unexpected Trace function count in {name}: {count}')


def instructions(words):
    index = 5
    while index < len(words):
        count, opcode = words[index] >> 16, words[index] & 65535
        if count == 0 or index + count > len(words):
            raise ValueError('Invalid SPIR-V instruction length')
        yield index, opcode, count
        index += count


def prepare(data, function_name=TRACE):
    if len(data) < 20 or len(data) % 4:
        raise ValueError('Invalid SPIR-V module size')
    words = list(struct.unpack(f'<{len(data) // 4}I', data))
    if words[0] != 0x07230203:
        raise ValueError('Expected little-endian SPIR-V')
    targets = set()
    for index, opcode, count in instructions(words):
        if opcode == 5:
            if count < 3:
                raise ValueError('Invalid SPIR-V OpName')
            name = struct.pack(f'<{count - 2}I', *words[index + 2:index + count])
            if b'\0' not in name:
                raise ValueError('Unterminated SPIR-V OpName')
            if name.split(b'\0', 1)[0] == function_name:
                targets.add(words[index + 1])
    if len(targets) > 1:
        raise ValueError('Ambiguous controlled trace function')
    found = set()
    for index, opcode, count in instructions(words):
        if opcode == 54 and count == 5 and words[index + 2] in targets:
            if words[index + 3] & 1:
                raise ValueError('Controlled trace already requests inlining')
            words[index + 3] |= 2
            found.add(words[index + 2])
    if found != targets:
        raise ValueError('Controlled trace declaration is missing')
    return struct.pack(f'<{len(words)}I', *words), len(targets)


def self_check():
    def instruction(opcode, *operands):
        return [(len(operands) + 1) << 16 | opcode, *operands]

    def name(identity, text):
        encoded = text + b'\0'
        encoded += b'\0' * (-len(encoded) % 4)
        return instruction(5, identity, *struct.unpack(f'<{len(encoded) // 4}I', encoded))

    words = [0x07230203, 0x10000, 0, 10, 0]
    words += name(2, TRACE) + name(3, b'Trace(vf3;vf3;f1;b1;')
    first = len(words)
    words += instruction(54, 1, 2, 0, 4)
    words += instruction(54, 1, 3, 0, 5)
    original = struct.pack(f'<{len(words)}I', *words)
    expected = words.copy()
    expected[first + 3] = 2
    marked, count = prepare(original)
    assert count == 1
    assert marked == struct.pack(f'<{len(expected)}I', *expected)
    assert prepare(marked) == (marked, 1)
    empty = struct.pack('<5I', 0x07230203, 0x10000, 0, 1, 0)
    assert prepare(empty) == (empty, 0)
    software_words = words + name(6, SOFTWARE_TRACE) + name(7, b'TraceSoftware(vf3;vf3;f1;b1;')
    software_index = len(software_words)
    software_words += instruction(54, 1, 6, 0, 4) + instruction(54, 1, 7, 0, 4)
    software_data = struct.pack(f'<{len(software_words)}I', *software_words)
    software_expected = software_words.copy()
    software_expected[software_index + 3] = 2
    software_marked, software_count = prepare(software_data, SOFTWARE_TRACE)
    assert software_count == 1 and software_marked == struct.pack(f'<{len(software_expected)}I', *software_expected)
    assert prepare(software_marked, SOFTWARE_TRACE) == (software_marked, 1)
    assert prepare(empty, SOFTWARE_TRACE) == (empty, 0)
    both, trace_count = prepare(software_marked)
    software_expected[first + 3] = 2
    assert trace_count == 1 and both == struct.pack(f'<{len(software_expected)}I', *software_expected)
    assert prepare(both, SOFTWARE_TRACE) == (both, 1)
    for name in TRACE_PASSES:
        check_trace_count(name, 1)
    for name in ('DIAHistory.frag.spv', 'DIADenoise.frag.spv', 'DIACompose.frag.spv'):
        check_trace_count(name, 0)
    for name, count in (('DIATrace.frag.spv', 0), ('DIACache.frag.spv', 2), ('DIADenoise.frag.spv', 1)):
        try:
            check_trace_count(name, count)
        except ValueError:
            pass
        else:
            raise AssertionError('Accepted an unexpected Trace function count')
    for invalid in (b'', original[:-1], b'\0' * 20, empty + b'\0' * 4, empty + struct.pack('<I', 2 << 16)):
        try:
            prepare(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError('Accepted malformed SPIR-V')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('path', type=Path, nargs='?')
    parser.add_argument('--self-check', action='store_true')
    args = parser.parse_args()
    if args.self_check:
        self_check()
        print('PASS Trace function control and SPIR-V format checks')
        return
    if args.path is None:
        parser.error('A SPIR-V path is required')
    marked, count = prepare(args.path.read_bytes())
    check_trace_count(args.path.name, count)
    software_count = 0
    if args.path.name in RAY_PASSES:
        marked, software_count = prepare(marked, SOFTWARE_TRACE)
        if software_count != 1:
            raise ValueError(f'Unexpected software trace count in {args.path.name}: {software_count}')
    args.path.write_bytes(marked)
    subprocess.run(['spirv-opt', '-Os', str(args.path), '-o', str(args.path)], check=True)
    optimized = args.path.read_bytes()
    verified, remaining = prepare(optimized)
    if verified != optimized or remaining != count:
        raise ValueError('Optimizer did not preserve Trace function control')
    if software_count:
        verified, remaining = prepare(optimized, SOFTWARE_TRACE)
        if verified != optimized or remaining != software_count:
            raise ValueError('Optimizer did not preserve software trace function control')


if __name__ == '__main__':
    main()
