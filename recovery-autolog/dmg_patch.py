"""Append-only UDIF chunk replacement for one hash-pinned recovery image.

The unchanged compressed data fork is retained. Only changed HFS chunks and a
new block map/footer are appended. No Apple image is uploaded as an artifact.
"""
import hashlib, json, pathlib, plistlib, struct, sys, zipfile, zlib

BASE_SHA = '7314eb401f5e84087f621b3599f0ad21ca3cdcc2685ea2da7f76806792328e20'
HFS_INDEX = 4

def sha(path):
    with open(path, 'rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

class Image:
    def __init__(self, path, pinned=True):
        if pinned:
            assert sha(path) == BASE_SHA, 'Unexpected recovery image'
        self.f = open(path, 'rb')
        self.f.seek(-512, 2)
        self.footer = bytearray(self.f.read())
        assert self.footer[:4] == b'koly'
        self.fork_offset, self.fork_length = struct.unpack_from('>QQ', self.footer, 24)
        assert self.fork_offset == 0
        xo, xl = struct.unpack_from('>QQ', self.footer, 216)
        self.f.seek(xo)
        self.plist = plistlib.loads(self.f.read(xl))
        self.blocks = self.plist['resource-fork']['blkx']
        assert 'Apple_HFS' in self.blocks[HFS_INDEX]['Name']
        self.table = bytearray(self.blocks[HFS_INDEX]['Data'])
        self.sectors = struct.unpack_from('>Q', self.table, 16)[0]
        self.data_offset = struct.unpack_from('>Q', self.table, 24)[0]
        n = struct.unpack_from('>I', self.table, 200)[0]
        assert len(self.table) == 204 + n * 40
        self.runs = [struct.unpack_from('>IIQQQQ', self.table, 204 + i * 40) for i in range(n)]

    def raw(self, run):
        kind, _, sector, count, off, length = run
        if kind in (0x7ffffffe, 0xffffffff):
            return b''
        assert count * 512 <= 64 * 1024 * 1024
        if kind in (0, 2):
            return bytes(count * 512)
        self.f.seek(self.fork_offset + self.data_offset + off)
        packed = self.f.read(length)
        assert len(packed) == length
        raw = zlib.decompress(packed) if kind == 0x80000005 else packed
        assert kind in (1, 0x80000005) and len(raw) == count * 512
        return raw

def make(base, modified_hfs, patch):
    image = Image(base)
    assert pathlib.Path(modified_hfs).stat().st_size == image.sectors * 512
    changes = []
    checksum = 0
    with open(modified_hfs, 'rb') as modified, zipfile.ZipFile(patch, 'w', compression=zipfile.ZIP_STORED) as out:
        for index, run in enumerate(image.runs):
            raw = image.raw(run)
            if not raw:
                continue
            modified.seek(run[2] * 512)
            replacement = modified.read(len(raw))
            assert len(replacement) == len(raw)
            # UDIF IGNORE runs are omitted from blkx CRC, even though their
            # zero-filled sectors remain part of the logical HFS image.
            if run[0] != 2 or raw != replacement:
                checksum = zlib.crc32(replacement, checksum)
            if raw != replacement:
                packed = zlib.compress(replacement, 6)
                name = 'chunks/%d.zlib' % index
                out.writestr(name, packed)
                changes.append(dict(index=index, name=name, raw_sha256=hashlib.sha256(replacement).hexdigest(), packed_sha256=hashlib.sha256(packed).hexdigest()))
        manifest = dict(base_sha256=BASE_SHA, hfs_sha256=sha(modified_hfs), hfs_crc32=checksum, changes=changes)
        out.writestr('patch.json', json.dumps(manifest, indent=2))
    print('Changed HFS chunks:', len(changes), 'patch bytes:', pathlib.Path(patch).stat().st_size)

def apply(base, patch, target):
    image = Image(base)
    crc = 0
    with zipfile.ZipFile(patch) as z, open(target, 'wb') as out:
        manifest = json.loads(z.read('patch.json'))
        assert manifest['base_sha256'] == BASE_SHA
        image.f.seek(0)
        remaining = image.fork_length
        while remaining:
            data = image.f.read(min(4 * 1024 * 1024, remaining))
            assert data
            out.write(data)
            crc = zlib.crc32(data, crc)
            remaining -= len(data)
        assert crc == struct.unpack_from('>I', image.footer, 88)[0]
        seen = set()
        for change in manifest['changes']:
            i = change['index']
            assert 0 <= i < len(image.runs) and i not in seen
            seen.add(i)
            kind, comment, sector, count, _, _ = image.runs[i]
            assert kind in (0, 1, 2, 0x80000005)
            packed = z.read(change['name'])
            assert hashlib.sha256(packed).hexdigest() == change['packed_sha256']
            raw = zlib.decompress(packed)
            assert len(raw) == count * 512 and hashlib.sha256(raw).hexdigest() == change['raw_sha256']
            offset = out.tell() - image.data_offset
            out.write(packed)
            crc = zlib.crc32(packed, crc)
            struct.pack_into('>IIQQQQ', image.table, 204 + i * 40, 0x80000005, comment, sector, count, offset, len(packed))
        struct.pack_into('>I', image.table, 72, manifest['hfs_crc32'])
        image.blocks[HFS_INDEX]['Data'] = bytes(image.table)
        # ASR restore optimization metadata describes the original filesystem.
        # A bootable UDIF only needs the accurate blkx map; remove stale metadata.
        xml = plistlib.dumps({'resource-fork': {'blkx': image.blocks}}, sort_keys=False)
        fork_length = out.tell()
        struct.pack_into('>Q', image.footer, 32, fork_length)
        struct.pack_into('>I', image.footer, 88, crc)
        struct.pack_into('>QQ', image.footer, 216, fork_length, len(xml))
        master = zlib.crc32(b''.join(b['Data'][72:76] for b in image.blocks))
        struct.pack_into('>I', image.footer, 360, master)
        out.write(xml)
        out.write(image.footer)
    verify = Image(target, pinned=False)
    digest = hashlib.sha256()
    checksum = 0
    for run in verify.runs:
        raw = verify.raw(run)
        digest.update(raw)
        if run[0] != 2:
            checksum = zlib.crc32(raw, checksum)
    assert digest.hexdigest() == manifest['hfs_sha256']
    assert checksum == manifest['hfs_crc32']
    result = dict(bytes=pathlib.Path(target).stat().st_size, sha256=sha(target), hfs_sha256=digest.hexdigest(), changed_chunks=len(seen))
    pathlib.Path(str(target) + '.verification.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result))

if __name__ == '__main__':
    {'make': make, 'apply': apply}[sys.argv[1]](*sys.argv[2:])
