import sys,struct
def vlq(d,i):
    v=0
    while True:
        b=d[i]; i+=1; v=(v<<7)|(b&0x7f)
        if not b&0x80: break
    return v,i
def parse(path):
    d=open(path,'rb').read()
    assert d[:4]==b'MThd'
    fmt,ntrk,div=struct.unpack('>HHH',d[8:14])
    print(f"{path.split('/')[-1]}: format={fmt} tracks={ntrk} division={div}")
    i=14
    chans=set(); progs={}; notes=set(); running=0
    for t in range(ntrk):
        assert d[i:i+4]==b'MTrk', d[i:i+4]
        ln=struct.unpack('>I',d[i+4:i+8])[0]; i+=8; end=i+ln
        running=0
        while i<end:
            dt,i=vlq(d,i)
            b=d[i]
            if b&0x80: status=b; i+=1
            else: status=running
            running=status
            hi=status&0xf0; ch=status&0x0f
            if hi==0x90:
                n=d[i]; v=d[i+1]; i+=2
                if v>0: chans.add(ch); notes.add(n)
            elif hi==0x80:
                i+=2
            elif hi==0xC0:
                progs[ch]=d[i]; chans.add(ch); i+=1
            elif hi in (0xA0,0xB0,0xE0):
                i+=2
            elif hi==0xD0:
                i+=1
            elif status==0xFF:
                mt=d[i]; i+=1; l,i=vlq(d,i); i+=l
            elif status in (0xF0,0xF7):
                l,i=vlq(d,i); i+=l
            else:
                i+=1
    print(f"  channels used (0-based)={sorted(chans)}  -> 1-based={sorted(c+1 for c in chans)}")
    print(f"  program changes (ch->prog)={progs}")
    print(f"  note numbers={sorted(notes)}")
    print(f"  drum channel 10 (idx 9) used? {9 in chans}")
for p in sys.argv[1:]:
    parse(p); print()
