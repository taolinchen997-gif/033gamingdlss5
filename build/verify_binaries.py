"""把自己编出来的程序和安装包里的那份逐字节比。

编译器会把两样东西写进程序：编译时间（PE 头、导出表、调试目录里的时间戳，调试信息的 GUID，核心自己用
__DATE__/__TIME__ 记下的编译时间字符串），以及源码和输出所在的路径（__FILE__、.pdb 路径）。比较前把时间
戳、GUID 和编译时间字符串清零，并把 --map 给的路径换成对方的路径（两边长度必须一样，所以要在同样长度的
目录里编）；其余每个字节都必须相同。核心 033-engine.dll 还有一个特殊情况：MSVC 给匿名命名空间起内部名字
时用到源码的完整路径，换了目录，一部分数据的排列会变，所以核心只能在同一个目录里比（见 BUILD.md）。

用法：python verify_binaries.py 安装包里的文件 自己编的文件 [--map 原路径=自己的路径 ...]
退出码 0 = 一致。
"""
import argparse, re, struct, sys

def pe_fields(data):
    """需要清零的 (偏移, 长度)：时间戳、校验和、调试 GUID/Age。"""
    out = []
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe + 4] != b'PE\0\0': raise SystemExit('不是 PE 文件')
    coff = pe + 4
    sections_count = struct.unpack_from('<H', data, coff + 2)[0]
    opt_size = struct.unpack_from('<H', data, coff + 16)[0]
    out.append((coff + 4, 4))  # TimeDateStamp
    opt = coff + 20
    magic = struct.unpack_from('<H', data, opt)[0]
    out.append((opt + 64, 4))  # CheckSum
    dirs = opt + (112 if magic == 0x20B else 96)
    sections = []
    for i in range(sections_count):
        s = opt + opt_size + i * 40
        vsize, va, rsize, raw = struct.unpack_from('<IIII', data, s + 8)
        sections.append((va, max(vsize, rsize), raw))
    def offset(rva):
        for va, size, raw in sections:
            if va <= rva < va + size: return raw + (rva - va)
        return None
    exp_rva, exp_size = struct.unpack_from('<II', data, dirs)
    if exp_rva and offset(exp_rva) is not None: out.append((offset(exp_rva) + 4, 4))
    dbg_rva, dbg_size = struct.unpack_from('<II', data, dirs + 6 * 8)
    base = offset(dbg_rva) if dbg_rva else None
    if base is not None:
        for i in range(dbg_size // 28):
            e = base + i * 28
            out.append((e + 4, 4))
            kind, size, _, ptr = struct.unpack_from('<IIII', data, e + 12)
            if kind == 2 and data[ptr:ptr + 4] == b'RSDS': out.append((ptr + 4, 20))  # GUID + Age
            elif kind in (13, 16) and size: out.append((ptr, size))  # POGO / REPRO 记录里带编译产生的散列
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('shipped'); ap.add_argument('rebuilt')
    ap.add_argument('--map', action='append', default=[], help='原路径=自己的路径（等长）')
    a = ap.parse_args()
    shipped = bytearray(open(a.shipped, 'rb').read()); rebuilt = bytearray(open(a.rebuilt, 'rb').read())
    if len(shipped) != len(rebuilt):
        print(f'大小不同：{len(shipped)} / {len(rebuilt)}'); return 1
    replaced = 0
    for pair in a.map:
        old, new = pair.split('=', 1)
        if len(old) != len(new): raise SystemExit(f'路径长度不同：{old} / {new}')
        for form in {old, old.replace('\\', '/'), old.lower(), old.upper(), old.lower().replace('\\', '/')}:
            twin = {old: new, old.replace('\\', '/'): new.replace('\\', '/'), old.lower(): new.lower(), old.upper(): new.upper(),
                    old.lower().replace('\\', '/'): new.lower().replace('\\', '/')}[form]
            for enc in ('ascii', 'utf-16-le'):
                try: o, n = form.encode(enc), twin.encode(enc)
                except UnicodeEncodeError: continue
                replaced += rebuilt.count(n)
                rebuilt[:] = rebuilt.replace(n, o)
    fields = pe_fields(bytes(shipped))
    for off, size in fields + pe_fields(bytes(rebuilt)):
        shipped[off:off + size] = b'\0' * size; rebuilt[off:off + size] = b'\0' * size
    # 核心用 __DATE__ " " __TIME__ 记下编译时间（例如 "Sep 22 2026 11:30:17"），两边同一位置都是这种字符串就清零。
    stamp = re.compile(rb'[A-Z][a-z]{2} [ 0-3][0-9] [0-9]{4} [0-9]{2}:[0-9]{2}:[0-9]{2}')
    stamps = 0
    for m in stamp.finditer(bytes(shipped)):
        o, e = m.start(), m.end()
        if stamp.fullmatch(bytes(rebuilt[o:e])): shipped[o:e] = b'\0' * (e - o); rebuilt[o:e] = b'\0' * (e - o); stamps += 1
    diffs = [i for i in range(len(shipped)) if shipped[i] != rebuilt[i]]
    runs = []
    for i in diffs:
        if runs and i == runs[-1][1] + 1: runs[-1][1] = i
        else: runs.append([i, i])
    print(f'{len(shipped)} 字节；换回路径 {replaced} 处；清零时间戳/GUID {len(fields)} 处、编译时间字符串 {stamps} 处；剩下不同的字节 {len(diffs)}（{len(runs)} 段）')
    for s, e in runs[:20]:
        print(f'  0x{s:08x}-0x{e:08x}  安装包 {bytes(shipped[s:e + 1])[:32].hex()}  自编 {bytes(rebuilt[s:e + 1])[:32].hex()}')
    return 0 if not diffs else 1

if __name__ == '__main__':
    sys.exit(main())
