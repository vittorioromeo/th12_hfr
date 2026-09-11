import sys,subprocess,struct
exe=sys.argv[1]; start=int(sys.argv[2],16); end=int(sys.argv[3],16)
d=open(exe,'rb').read(); pe=struct.unpack_from('<I',d,0x3c)[0]; opt=struct.unpack_from('<H',d,pe+20)[0]; o=pe+24+opt; vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8)
off=lambda a: ro+a-0x400000-va
open('/tmp/chunk.bin','wb').write(d[off(start):off(end)])
print(subprocess.run(['objdump','-D','-b','binary','-mi386','-M','intel','--adjust-vma=%d'%start,'/tmp/chunk.bin'],capture_output=True,text=True).stdout.split('<.data>:')[1])
