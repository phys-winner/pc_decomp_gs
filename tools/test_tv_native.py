"""Differential TV kernel check against the unchanged original stage3C bytecode."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import re

from android_tools import ADB, CLANG
from build_widescreen import crypt_index
from decompile_cv4 import parse
from build_android import ARM32_FLAGS, GENERATED_FLAGS, HOST_FLAGS

ROOT=Path(__file__).resolve().parents[1]


def f32(x):
    return struct.unpack("<f",struct.pack("<f",x))[0]


def integer(x):
    return (int(x)+2**31)%2**32-2**31


def arithmetic(a,b,op):
    if op==37:
        return integer(a-int(a/b)*b)
    if isinstance(a,float) or isinstance(b,float):
        a,b=f32(a),f32(b)
        return f32({43:lambda:a+b,45:lambda:a-b,42:lambda:a*b,47:lambda:a/b}[op]())
    return integer({43:lambda:a+b,45:lambda:a-b,42:lambda:a*b,47:lambda:int(a/b)}[op]())


def member(obj,key):
    if key=="len":return lambda owner:len(owner)
    if key=="tointeger":return lambda owner:integer(owner)
    return obj[key]


def execute(function,env,root):
    r=[None]*64;r[0]=env;ip=steps=0
    code=function["instructions"];literals=function["literals"]
    while ip<len(code):
        a1,op,a0,a2,a3=code[ip];ip+=1;steps+=1
        assert steps<100000
        if op==1:r[a0]=literals[a1]
        elif op==2:r[a0]=a1
        elif op==3:r[a0]=struct.unpack("<f",struct.pack("<i",a1))[0]
        elif op==21:r[a0]=root
        elif op==9:r[a0]=member(r[a2],literals[a1])
        elif op==8:
            owner=r[a2];r[a0]=member(owner,literals[a1]);r[a3]=owner
        elif op==6:r[a0]=r[a1](*r[a2:a2+a3])
        elif op==10:r[a0]=r[a1]
        elif op==14:r[a0]=r[a1][r[a2]]
        elif op==17:r[a0]=arithmetic(r[a2],r[a1],a3)
        elif op==40:r[a0]=({0:lambda:r[a2]>r[a1],1:lambda:r[a2]>=r[a1],2:lambda:r[a2]<=r[a1],3:lambda:r[a2]<r[a1]}[a3]())
        elif op==15:r[a0]=r[a2]==r[a1]
        elif op==26:
            if not r[a0]:ip+=a1
        elif op==24:ip+=a1
        elif op==43:
            if not r[a2]:r[a0]=r[a2];ip+=a1
        elif op==46:r[a0]=not r[a1]
        elif op==38:
            obj,key=r[a1],r[a2];old=obj[key];obj[key]=arithmetic(old,a3,43);r[a0]=old
        elif op==39:old=r[a1];r[a1]=arithmetic(old,a3,43);r[a0]=old
        elif op==19:return
        else:raise AssertionError((ip-1,op,code[ip-1]))


def original():
    archive=ROOT/"original/japanese/gs00.dat"
    with archive.open("rb") as stream:
        assert hashlib.file_digest(stream,"sha256").hexdigest()=="60feca01c0466b6e3f65a6d100cd8b0911e72621738d33658a005187d6d63f65"
        stream.seek(0);count,length=struct.unpack("<HI",stream.read(6))
        index=crypt_index(stream.read(length));at=0;found=None
        for _ in range(count):
            offset,size,n=struct.unpack_from("<IIB",index,at);at+=9
            name=index[at:at+n].decode("cp932");at+=n
            if name=="data/stage/stage3C.global.cv4":found=(offset,size)
        assert at==length and found
        stream.seek(found[0]);encrypted=stream.read(found[1]);key=encrypted[0]^0xfa
    decoded=bytes(x^key for x in encrypted)
    path=ROOT/"build/tests/stage3C-original.cv4";path.parent.mkdir(exist_ok=True,parents=True);path.write_bytes(decoded)
    root=parse(path)
    update,=[f for f in root["children"] if f["name"]=="Update"]
    return update,hashlib.sha256(decoded).hexdigest()


def trace(function,groups,source,bars,frames,positions,shader,active,seed):
    commands=[];state=[seed,0]
    def emit(op,layer=0,resource=0,x=0,y=0,w=0,h=0,sx=0,sy=0,sw=0,sh=0,blend=0,alpha=0.0):
        commands.append(struct.pack("<I2I9if",op,layer,resource,x,y,w,h,sx,sy,sw,sh,blend,f32(alpha)))
    def rand(owner):
        state[0]=(state[0]*1664525+1013904223)%2**32
        return state[0]>>1
    def active_call(owner):state[1]+=1;return active
    def layer(index,dx,dy):
        return {"dst_x":dx,"dst_y":dy,
            "SetBlend":lambda owner,b,a:emit(1,index,blend=b,alpha=a),
            "StretchBlt":lambda owner,x,y,w,h,res,sx,sy,sw,sh:emit(2,index,res,integer(x),integer(y),integer(w),integer(h),integer(sx),integer(sy),integer(sw),integer(sh)),
            "BitBlt":lambda owner,x,y,w,h,res,sx,sy,b,a:emit(3,index,res,integer(x),integer(y),integer(w),integer(h),integer(sx),integer(sy),blend=b,alpha=a)}
    # Resources are IDs in the trace; SetCapture is the sole method of resource 3.
    class Capture(int):
        def __getitem__(self,key):
            assert key=="SetCapture";return lambda owner:emit(0,resource=3)
    root={"CaptureAct":{"bbSnap":Capture(3)},"UtilityAct":{"resource":{"TvLightMap":4,"tv3":5,"TvLightMap2":6}},
          "stage":{"renderState":{"pixelShader":shader}}}
    env={"layermain":layer(1,*positions[2:4]),"layer_1":layer(2,*positions[4:6]),
        "camera2d":{"x":positions[0],"y":positions[1]},"BLEND_ALPHA":1,"BLEND_SUB":3,
        "GameOverAct":{"pl":{"IsActive":active_call}},"rand":rand,"fabs":lambda owner,x:abs(x),
        "colorBarTVchip":[{"left":x,"top":y} for x,y in bars],"colorBarTVFrame":frames}
    env.update({"animChipList"+chr(65+i):[{"left":x,"top":y} for x,y in group] for i,group in enumerate(groups)})
    env.update(dict(zip(("tvSrcLeft","tvSrcTop","tvSrcWidth","tvSrcHeight"),source)))
    execute(function,env,root)
    return commands,state


def value(n):
    return struct.pack("<II",0x05000004,struct.unpack("<I",struct.pack("<f",n))[0]) if isinstance(n,float) else struct.pack("<II",0x05000002,n%2**32)


def corpus(function,path):
    rng=random.Random(81923);cases=2048
    with path.open("wb") as stream:
        stream.write(struct.pack("<II",0x314e5654,cases))
        for case in range(cases):
            positions=[f32(rng.choice((0.0,-0.0,0.5,-0.5,100.125,-200.375))) for _ in range(6)]
            groups=[[(rng.choice((-1000,-900,-899,0,899,900,901,1200)),rng.choice((-800,-700,-699,0,699,700,701))) for _ in range(rng.randrange(8))] for _ in range(4)]
            bars=[(rng.randrange(-1100,1101),rng.randrange(-850,851)) for _ in range(rng.randrange(10))]
            count=sum(map(len,groups));source=[[f32(rng.randrange(801)/3) if rng.randrange(2) else rng.randrange(801) for _ in range(count)] for _ in range(4)]
            frames=[rng.choice((0,1,2,10,11,12,13,254,255,2147483645)) for _ in bars]
            shader=case%3;active=bool(case%2);seed=rng.randrange(2**32)
            stream.write(struct.pack("<8I6f",*map(len,groups),len(bars),shader,active,seed,*positions))
            for group in groups:stream.write(b"".join(value(n) for pair in group for n in pair))
            for array in source:stream.write(b"".join(value(n) for n in array))
            stream.write(b"".join(value(n) for pair in bars for n in pair))
            stream.write(b"".join(value(n) for n in frames))
            commands,state=trace(function,groups,source,bars,frames,positions,shader,active,seed)
            stream.write(struct.pack("<I",len(commands)));stream.write(b"".join(commands))
            stream.write(struct.pack("<"+"I"*len(frames),*frames));stream.write(struct.pack("<II",*state))
    return cases


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("serial");args=parser.parse_args()
    function,digest=original();out=ROOT/"build/tests";data=out/"tv-native-corpus.bin"
    cases=corpus(function,data)
    print(json.dumps({"cases":cases,"original_bytecode_sha256":digest,"corpus_sha256":hashlib.sha256(data.read_bytes()).hexdigest()}),flush=True)
    subprocess.run([str(ADB),"-s",args.serial,"push",str(data),"/data/local/tmp/grief-tv-corpus.bin"],check=True)
    bodies=[]
    for address in ("004D7800","004EE0C0","004EE3D0"):
        marker="void sub_"+address+"(void) {"
        matches=[p.read_text().split(marker,1)[1].split("\n}\n",1)[0] for p in (ROOT/"generated/game-x86").glob("recomp_*.c") if marker in p.read_text()]
        assert len(matches)==1
        bodies.append(marker+matches[0]+"\n}\n")
    original_source=out/"tv-original.c"
    original_source.write_text('#include "../../generated/game-x86/recomp_funcs.h"\n'+"\n".join(bodies),encoding="utf-8")
    assert not re.search(r"RECOMP_CALL\(sub_004D7800\)","".join(p.read_text() for p in (ROOT/"generated/game-x86").glob("recomp_*.c")))
    for abi,target in (("arm64","aarch64-linux-android24"),("arm32","armv7a-linux-androideabi24")):
        executable=out/("tv-native-selftest-"+abi)
        flags=ARM32_FLAGS if abi=="arm32" else []
        subprocess.run([str(CLANG),"--target="+target,*flags,"-O2","-std=gnu11","-Wall","-Wextra","-Werror","-ffp-contract=off","-fno-strict-aliasing","-fwrapv","tools/tv_native_selftest.c","-lm","-o",str(executable)],cwd=ROOT,check=True)
        remote="/data/local/tmp/"+executable.name
        subprocess.run([str(ADB),"-s",args.serial,"push",str(executable),remote],check=True)
        subprocess.run([str(ADB),"-s",args.serial,"shell",f"chmod 755 {remote}; {remote} /data/local/tmp/grief-tv-corpus.bin"],check=True)
        original_object=out/("tv-original-"+abi+".o")
        generated_flags=["--target="+target,*GENERATED_FLAGS[1:],*flags]
        subprocess.run([str(CLANG),*generated_flags,"-c",str(original_source),"-o",str(original_object)],cwd=ROOT,check=True)
        bridge=out/("tv-bridge-selftest-"+abi)
        subprocess.run([str(CLANG),"--target="+target,*HOST_FLAGS[1:],*flags,"-Wno-unused-label",
            "tools/tv_bridge_selftest.c",str(original_object),"-Wl,--gc-sections","-lm","-o",str(bridge)],cwd=ROOT,check=True)
        remote="/data/local/tmp/"+bridge.name
        subprocess.run([str(ADB),"-s",args.serial,"push",str(bridge),remote],check=True)
        subprocess.run([str(ADB),"-s",args.serial,"shell",f"chmod 755 {remote}; {remote}"],check=True)


if __name__=="__main__":main()
