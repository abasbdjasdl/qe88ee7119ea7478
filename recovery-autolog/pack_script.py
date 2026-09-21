"""Keep the existing 5241-byte HFS file extent; change script bytes only."""
import pathlib, sys

def pack(source):
    lines = source.decode('utf-8').splitlines()
    lines = [line.lstrip() for line in lines if line.strip() and (not line.lstrip().startswith('#') or line.startswith('#!'))]
    data = ('\n'.join(lines) + '\n').encode()
    remaining = 5241 - len(data)
    assert remaining >= 2, (len(data), 'Script exceeds existing file size')
    return data + b'#' + b' ' * (remaining - 2) + b'\n'

if __name__ == '__main__':
    output = pack(pathlib.Path(sys.argv[1]).read_bytes())
    pathlib.Path(sys.argv[2]).write_bytes(output)
    print('Packed script bytes:', len(output))
