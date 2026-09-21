"""Use Apple's recovery-session authorization, then verify a pinned image hash."""
import concurrent.futures, importlib.util, pathlib, sys, types, urllib.request

root = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('macrecovery', root / 'vendor/macrecovery.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
info = m.get_image_info(m.get_session(types.SimpleNamespace(verbose=False)), 'Mac-7BA5B2D9E42DDD94')
assert info[m.INFO_PRODUCT] == '696-28424'
url = info[m.INFO_IMAGE_LINK]
assert url == 'http://oscdn.apple.com/content/downloads/04/11/082-33203/orvwro1v8xhjrakr7tvl5hu1s1ew3epxne/RecoveryImage/BaseSystem.dmg'
size = 884317790
block = 4 * 1024 * 1024

def fetch(start):
    end = min(start + block, size) - 1
    headers = {'User-Agent': 'InternetRecovery/1.0', 'Connection': 'close', 'Cookie': 'AssetToken=' + info[m.INFO_IMAGE_SESS], 'Range': f'bytes={start}-{end}'}
    for retry in range(3):
        try:
            request = urllib.request.Request(url + f'?range={start}&retry={retry}', headers=headers)
            with urllib.request.urlopen(request, timeout=60) as response:
                assert response.status == 206
                assert response.headers['Content-Range'] == f'bytes {start}-{end}/{size}'
                data = response.read()
            assert len(data) == end - start + 1
            return start, data
        except Exception:
            if retry == 2: raise

with open(sys.argv[1], 'wb') as out, concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    out.truncate(size)
    done = 0
    for start, data in pool.map(fetch, range(0, size, block)):
        out.seek(start)
        out.write(data)
        done += len(data)
        if done % (64 * 1024 * 1024) == 0: print('Downloaded', done // (1024 * 1024), 'MiB', flush=True)
print('Download complete; caller must verify pinned SHA256.')
