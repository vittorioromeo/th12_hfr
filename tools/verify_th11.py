"""Compatibility command; executable parsing is shared with verify_game.py."""
import argparse
import hashlib
import json
from pathlib import Path
from verify_game import identify

def verify(path):
    if identify(path)!=11:
        raise ValueError('Expected TH11 v1.00a')
    count=len(json.loads(Path(__file__).with_name('th11_signatures.json').read_text()))
    return count,hashlib.sha256(Path(path).read_bytes()).hexdigest()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executables',nargs='+')
    args=parser.parse_args();failed=False
    for path in args.executables:
        try:
            count,digest=verify(path)
            print(f'PASS {path}: {count} signatures; SHA256 {digest}')
        except (OSError,ValueError) as exc:
            print(f'FAIL {path}: {exc}');failed=True
    raise SystemExit(int(failed))
