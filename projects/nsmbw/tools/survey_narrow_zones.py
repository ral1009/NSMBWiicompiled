import struct,glob,os,collections
def u8arc(d):
    root=struct.unpack('>I',d[4:8])[0]; n=struct.unpack('>I',d[root+8:root+12])[0]; strt=root+n*12
    files={}; path=[]; ends=[]
    def name(o): e=d.index(b'\0',strt+o); return d[strt+o:e].decode()
    for i in range(n):
        t=d[root+i*12]; no=struct.unpack('>I',b'\0'+d[root+i*12+1:root+i*12+4])[0]
        a,b=struct.unpack('>II',d[root+i*12+4:root+i*12+12])
        while ends and i>=ends[-1]: ends.pop(); path.pop()
        if t==1:
            if i: path.append(name(no)); ends.append(b)
        else: files['/'.join(path+[name(no)])]=d[a:a+b]
    return files
V16=408.70; V21=V16*(21/9)/(16/9)  # 1-player base view width (zoom 1)
rows=[]
for f in sorted(glob.glob('NSMBW-Files/files/Stage/*.arc')):
    st=os.path.basename(f)[:-4]
    if not st[:2].isdigit(): continue
    fl=u8arc(open(f,'rb').read())
    for area in range(1,5):
        c=fl.get(f'course/course{area}.bin')
        if not c: continue
        off,sz=struct.unpack('>II',c[9*8:9*8+8])
        for j in range(sz//24):
            e=c[off+j*24:off+j*24+24]
            x,y,w,h=struct.unpack('>HHHH',e[:8])
            rows.append((st,area,e[12],x,w,h,e[14],e[15]))
print('zones total',len(rows))
n16=[r for r in rows if r[4]<V16]; n21=[r for r in rows if r[4]<V21]
n21mp=[r for r in rows if r[4]<760]
print(f'narrower than 16:9 view ({V16:.0f}):',len(n16))
print(f'narrower than 21:9 1-player view ({V21:.0f}):',len(n21))
print('narrower than ~760 (21:9 with multiplayer zoom-out, est.):',len(n21mp))
lv=collections.Counter(r[0] for r in n21)
print('levels with a sub-21:9 zone:',len(lv))
for r in n21: print('  %s area %d zone %d x=%d w=%d h=%d cam %d zoom %d'%r)
print('---')
fixed=[r for r in rows if r[6] in (3,4)]
print('fixed-camera zones (cam 3/4):',len(fixed),'in',len(set(r[0] for r in fixed)),'stages')
kinds=collections.Counter()
for r in fixed:
    lvl=int(r[0][3:5]); kinds['toad house/minigame/coin battle (20+)' if lvl>=20 else 'main course']+=1
print(kinds)
# main-course fixed zones list grouped
mc=[r for r in fixed if int(r[0][3:5])<20]
print('main-course fixed zones:',len(mc), 'stages:',sorted(set(r[0] for r in mc)))
print('=== by zoom (cam 4) ===')
byz=collections.defaultdict(list)
for r in rows:
    if r[6]==4: byz[r[7]].append(r[4])
v16={}
for z in sorted(byz):
    ws=sorted(byz[z]); v16[z]=ws[0]; print('zoom',z,'n',len(ws),'min w',ws[0],'median',ws[len(ws)//2])
# estimate: 16:9 view at a zoom ~= smallest cam-4 zone width at that zoom (rooms are built to fit it)
narrow=[r for r in rows if r[7] in v16 and r[4] < v16[r[7]]*21/16 - 1]
print('zones narrower than the 21:9 view (est.):',len(narrow),'in',len(set(r[0] for r in narrow)),'stages')
mc=[r for r in narrow if int(r[0][3:5])<20]
print('  of which in main courses (not toad houses / minigames / coin battle):',len(mc),'in',len(set(r[0] for r in mc)),'stages')
for r in mc: print('   %s area %d zone %d w=%d h=%d cam %d zoom %d'%(r[0],r[1],r[2],r[4],r[5],r[6],r[7]))
