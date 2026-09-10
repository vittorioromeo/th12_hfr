"""Identify a local supported game using the release's frozen signatures (stdlib)."""
import json
import struct
import sys
from pathlib import Path

def identify(path):
    data=Path(path).read_bytes()
    try:
        if data[:2]!=b'MZ':raise ValueError('Not PE')
        pe=struct.unpack_from('<I',data,0x3c)[0]
        if data[pe:pe+4]!=b'PE\0\0':raise ValueError('Not PE')
        machine,count=struct.unpack_from('<HH',data,pe+4)
        optional=pe+24;optsize=struct.unpack_from('<H',data,pe+20)[0]
        magic=struct.unpack_from('<H',data,optional)[0]
        base=struct.unpack_from('<I',data,optional+28)[0]
        image_size=struct.unpack_from('<I',data,optional+56)[0]
        if (machine,magic,base)!=(0x14c,0x10b,0x400000):raise ValueError('Unsupported PE layout')
        sections=[struct.unpack_from('<IIII',data,optional+optsize+i*40+8) for i in range(count)]
        for game,sizes in ((10,(0x9c000,)),(11,(0xcd000,)),(12,(0xd9000,)),(13,(0xe9000,0xea000))):
            if image_size not in sizes:continue   # th13e.exe carries an extra section
            signatures=json.loads(Path(__file__).with_name(f'th{game}_signatures.json').read_text())
            for s in signatures:
                expected=bytes.fromhex(s['bytes']);rva=int(s['address'],16)-base
                actual=next((data[raw+rva-va:raw+rva-va+len(expected)] for _,va,n,raw in sections if va<=rva and rva+len(expected)<=va+n),None)
                if actual!=expected:break
            else:return game
    except struct.error as e:raise ValueError('Truncated PE') from e
    raise ValueError('Unsupported or modified executable')

if __name__=='__main__':
    print(identify(sys.argv[1]))
