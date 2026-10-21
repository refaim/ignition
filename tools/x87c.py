#!/usr/bin/env python3
"""Translates a function's x87 code into C-like statements, a basic block
at a time, as a guide for porting it by hand.

    python3 tools/x87c.py .ida-mcp/asm/40e6b0_sub_40E6B0.asm

The FPU stack is tracked symbolically: loads push expressions, arithmetic
combines them, stores emit assignments. A value used twice (fld st(i),
fst st(i)) is put in a temporary first. At a jump the values still on
the stack are spilled to s0..sN, which the block jumped to takes over.
fcom/fnstsw/test ah/jcc become C comparisons. Integer instructions are
shown as comments, except simple register moves and adds of memory
addresses, which name the memory the FPU reads.

Memory operands print as M(base, off): a double for qword, a float for a
dword load, an int for fild; [esp+N+var_X] prints as var_X. Constants
from the image (ds:dbl_X, flt_X) print as their values.
"""

import re
import struct
import sys
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
IMAGE = (ROOT / ".ida-mcp" / "image.bin").read_bytes()
BASE = 0x479000

LINE = re.compile(r"^([0-9a-f]+)\s+(\S+)\s*(.*)$")


def const(name, size):
    m = re.search(r"(dbl|flt|qword|dword)_([0-9A-Fa-f]+)", name)
    if not m:
        return None
    addr = int(m.group(2), 16)
    off = addr - BASE
    if not 0 <= off < len(IMAGE):
        return None
    if m.group(1) in ("dbl", "qword") or size == 8:
        return repr(struct.unpack_from("<d", IMAGE, off)[0])
    return repr(struct.unpack_from("<f", IMAGE, off)[0])


def mem(op):
    """An operand's C name: a memory reference, a register, a number."""
    op = op.split(";")[0].strip()
    size = 8 if op.startswith("qword") else 4 if op.startswith("dword") else 10 if op.startswith("tbyte") else 0
    op = re.sub(r"^(qword|dword|word|byte|tbyte) ptr ", "", op)
    op = op.replace("ds:", "").replace("ss:", "")
    c = const(op, size)
    if c is not None:
        return c, size
    m = re.match(r"\[esp\+[0-9A-Fa-f]+h\+(\w+)(\+[0-9A-Fa-f]+h?)?\]", op)
    if m:
        return m.group(1) + (f"_{m.group(2)[1:]}" if m.group(2) else ""), size
    m = re.match(r"\[(\w+)(?:\+(\w+))?(?:\+([0-9A-Fa-f]+)h?)?\]$", op)
    if m:
        base, idx, off = m.group(1), m.group(2), m.group(3)
        if idx and re.fullmatch(r"[0-9A-Fa-f]+h?", idx):
            off, idx = idx, None
        parts = base + (f"+{idx}" if idx else "")
        offv = int(off.rstrip("h"), 16) if off else 0
        return f"M({parts}, {offv:#x})", size
    m = re.match(r"\[(\w+)\-([0-9A-Fa-f]+)h?\]$", op)
    if m:
        return f"M({m.group(1)}, -{int(m.group(2), 16):#x})", size
    return op, size


class Block:
    def __init__(self, start):
        self.start = start
        self.lines = []


def parse(path):
    ins = []
    for line in pathlib.Path(path).read_text().splitlines():
        m = LINE.match(line.strip())
        if not m:
            continue
        addr, mn, ops = int(m.group(1), 16), m.group(2), m.group(3)
        ins.append((addr, mn, ops))
    return ins


def target(ops):
    m = re.search(r"(?:loc|sub|locret)_([0-9A-Fa-f]+)", ops)
    return int(m.group(1), 16) if m else None


def blocks(ins):
    starts = {ins[0][0]}
    for i, (a, mn, ops) in enumerate(ins):
        if mn.startswith("j") or mn in ("retn", "ret"):
            t = target(ops)
            if t is not None:
                starts.add(t)
            if i + 1 < len(ins):
                starts.add(ins[i + 1][0])
    out, cur = [], None
    for a, mn, ops in ins:
        if a in starts:
            cur = Block(a)
            out.append(cur)
        cur.lines.append((a, mn, ops))
    return out


class Tr:
    def __init__(self):
        self.st = []
        self.out = []
        self.tmp = 0
        self.cmp = None
        self.flags = None
        self.entry = {}
        self.regs = {}

    def emit(self, s):
        self.out.append("    " + s)

    def protect(self, name):
        """Before `name` is written, values on the stack that read it are
        kept in temporaries."""
        pat = re.compile(r"(?<![\w])" + re.escape(name) + r"(?![\w])")
        for k in range(len(self.st)):
            v = self.st[k]
            if v != name and pat.search(v):
                self.tmp += 1
                t = f"t{self.tmp}"
                self.out.append(f"    {t} = {v};")
                self.st[k] = t
        if self.cmp:
            x, y = self.cmp
            if pat.search(y) and y != name:
                self.tmp += 1
                t = f"t{self.tmp}"
                self.out.append(f"    {t} = {y};")
                self.cmp = (x, t)

    def temp(self, e):
        if re.fullmatch(r"[\w.]+", e):
            return e
        self.tmp += 1
        name = f"t{self.tmp}"
        self.emit(f"{name} = {e};")
        return name

    def fill(self, i):
        # A block entered with values the translator did not see pushed
        # (a path it took a different depth on): name them as unknown.
        while len(self.st) <= i:
            self.st.insert(0, f"st_unknown{len(self.st)}")

    def get(self, i):
        self.fill(i)
        return self.st[-1 - i]

    def set(self, i, e):
        self.fill(i)
        self.st[-1 - i] = e

    def push(self, e):
        self.st.append(e)

    def pop(self):
        self.fill(0)
        return self.st.pop()

    def stidx(self, op):
        op = op.strip()
        if op == "st":
            return 0
        m = re.fullmatch(r"st\((\d)\)", op)
        return int(m.group(1)) if m else None

    def binop(self, mn, ops, sym, rev):
        a = [x.strip() for x in ops.split(",")] if ops else []
        popit = mn.endswith("p") and mn not in ("fcomp",)
        if not a:
            # faddp with no operands: st(1) = st(1) op st0, pop
            x, y = self.get(0), self.get(1)
            self.set(1, f"({x} {sym} {y})" if rev else f"({y} {sym} {x})")
            self.pop()
            return
        if len(a) == 1 and self.stidx(a[0]) is None:
            m, _ = mem(a[0])
            if mn.startswith("fi"):
                m = f"(double){m}"
            x = self.get(0)
            self.set(0, f"({m} {sym} {x})" if rev else f"({x} {sym} {m})")
            return
        i, j = self.stidx(a[0]), self.stidx(a[1]) if len(a) > 1 else 0
        if len(a) == 1:
            i, j = 0, i
        # dest = st(i), src = st(j)
        d, s = self.get(i), self.get(j)
        self.set(i, f"({s} {sym} {d})" if rev else f"({d} {sym} {s})")
        if popit:
            self.pop()

    def spill(self):
        names = []
        for k in range(len(self.st)):
            v = self.st[k]
            n = f"s{len(self.st) - 1 - k}"
            if v != n:
                self.emit(f"{n} = {v};")
            names.append(n)
        self.st = names

    def run(self, blk, entry_depth):
        self.st = [f"s{entry_depth - 1 - k}" for k in range(entry_depth)]
        self.out = [f"L_{blk.start:x}:  /* fpu depth {entry_depth} */"]
        exits = []
        for a, mn, ops in blk.lines:
            ok = self.step(a, mn, ops, exits)
            if not ok:
                self.emit(f"/* {a:x} {mn} {ops} */")
        return self.out, exits

    def cond(self, mn):
        if self.flags is None or self.cmp is None:
            return None
        x, y = self.cmp
        mask = self.flags
        if mask == 0x01:
            c = f"{x} < {y}"
        elif mask == 0x41:
            c = f"{x} <= {y}"
        elif mask == 0x40:
            c = f"{x} == {y}"
        elif mask == 0x05:
            c = f"isnan({x} - {y}) || {x} < {y}"
        elif mask == 0x44:
            c = f"isnan({x} - {y}) || {x} == {y}"
        else:
            c = f"/* ah & {mask:#x} of {x} ? {y} */ 0"
        if mn == "jz":
            return f"!({c})"
        if mn == "jnz":
            return c
        return None

    def step(self, a, mn, ops, exits):
        st = self
        if mn in ("fld", "fild"):
            i = self.stidx(ops)
            if i is not None:
                v = self.temp(self.get(i))
                self.set(i, v)
                self.push(v)
            else:
                m, size = mem(ops)
                if mn == "fild":
                    m = f"(double){m}" if size != 8 else f"(double)(s64){m}"
                elif size == 4 and m.startswith("M("):
                    m = f"(double)F{m[1:]}"
                self.push(m)
            return True
        if mn == "fldz":
            self.push("0.0")
            return True
        if mn == "fld1":
            self.push("1.0")
            return True
        if mn in ("fst", "fstp", "fistp", "fist"):
            i = self.stidx(ops)
            v = self.get(0)
            if i is not None:
                if i != 0:
                    v = self.temp(v)
                    self.set(0, v)
                    self.set(i, v)
            else:
                m, size = mem(ops)
                self.protect(m if not (size == 4 and m.startswith("M(")) else m)
                v = self.get(0)
                if mn.startswith("fi"):
                    self.emit(f"{m} = (s32){v};")
                elif size == 4 and m.startswith("M("):
                    self.emit(f"F{m[1:]} = (float){v};")
                else:
                    self.emit(f"{m} = {v};")
                if not mn.endswith("p"):
                    t = self.temp(m) if not re.fullmatch(r"[\w.]+", v) else v
                    self.set(0, m if not mn.startswith("fi") else v)
            if mn.endswith("p"):
                self.pop()
            return True
        if mn == "fxch":
            i = self.stidx(ops) if ops else 1
            x, y = self.get(0), self.get(i)
            self.set(0, y)
            self.set(i, x)
            return True
        ops_map = {"fadd": ("+", False), "faddp": ("+", False), "fiadd": ("+", False),
                   "fsub": ("-", False), "fsubp": ("-", False), "fisub": ("-", False),
                   "fsubr": ("-", True), "fsubrp": ("-", True), "fisubr": ("-", True),
                   "fmul": ("*", False), "fmulp": ("*", False), "fimul": ("*", False),
                   "fdiv": ("/", False), "fdivp": ("/", False), "fidiv": ("/", False),
                   "fdivr": ("/", True), "fdivrp": ("/", True), "fidivr": ("/", True)}
        if mn in ops_map:
            sym, rev = ops_map[mn]
            # Intel semantics for the reg-reg forms with p: fsubp st(i), st:
            # st(i) = st(i) - st(0); fsubrp: st(i) = st(0) - st(i).
            self.binop(mn, ops, sym, rev)
            return True
        un = {"fchs": "-({})", "fabs": "fabs({})", "fsin": "sin({})", "fcos": "cos({})",
              "fsqrt": "sqrt({})", "frndint": "rint({})"}
        if mn in un:
            self.set(0, un[mn].format(self.get(0)))
            return True
        if mn == "fptan":
            self.set(0, f"tan({self.get(0)})")
            self.push("1.0")
            return True
        if mn == "fpatan":
            x, y = self.get(0), self.get(1)
            self.pop()
            self.set(0, f"atan2({y}, {x})")
            return True
        if mn in ("fcom", "fcomp", "fcompp", "fucom", "fucomp", "fucompp"):
            if not ops:
                self.cmp = (self.get(0), self.get(1))
            elif self.stidx(ops) is not None:
                self.cmp = (self.get(0), self.get(self.stidx(ops)))
            else:
                m, size = mem(ops)
                if size == 4 and m.startswith("M("):
                    m = f"(double)F{m[1:]}"
                self.cmp = (self.get(0), m)
            if self.cmp and not re.fullmatch(r"[\w.\-]+", self.cmp[0]):
                t = self.temp(self.cmp[0])
                self.set(0, t)
                self.cmp = (t, self.cmp[1])
            if mn.endswith("pp"):
                self.pop()
                self.pop()
            elif mn.endswith("p"):
                self.pop()
            return True
        if mn == "fnstsw":
            return True
        # Doubles copied through integer registers: mov r, [m]; mov [v], r.
        m = re.match(r"(e[a-ds][xip]|e[sd]i|ebp)\s*,\s*(.+)$", ops) if mn == "mov" else None
        if m and "[" in m.group(2):
            self.regs[m.group(1)] = mem(m.group(2))[0]
            return False
        m = re.match(r"(e[a-ds][xip]|e[sd]i|ebp)\s*,", ops)
        if m and mn in ("mov", "xor", "lea", "add", "sub", "and", "or", "inc", "dec", "imul", "shl", "sar", "shr"):
            if mn == "xor" and re.fullmatch(r"(\w+),\s*\1", ops.strip()):
                self.regs[m.group(1)] = "0"
            else:
                self.regs.pop(m.group(1), None)
        m = re.match(r"(.+?),\s*(e[a-ds][xip]|e[sd]i|ebp)$", ops) if mn == "mov" else None
        if m and "[" in m.group(1) and m.group(2) in self.regs:
            dst, src = mem(m.group(1))[0], self.regs[m.group(2)]
            self.protect(dst)
            if src == "0" and (dst.startswith("var") or dst.startswith("M(")):
                self.emit(f"{dst} = 0;  /* a dword */")
                return True
            if (src.startswith("M(") or src.startswith("var")) and (dst.startswith("var") or dst.startswith("M(")):
                if not dst.endswith("_4") and not re.search(r", 0x[0-9a-f]*[4c]\)$", src):
                    self.emit(f"{dst} = {src};  /* copied as a double */")
                    return True
            return False
        if mn == "test" and ops.startswith("ah,"):
            v = ops.split(",")[1].strip()
            self.flags = int(v.rstrip("h"), 16)
            return True
        if mn.startswith("j"):
            t = target(ops)
            if mn == "jmp":
                self.spill()
                exits.append((t, len(self.st)))
                self.emit(f"goto L_{t:x};" if t else f"/* jmp {ops} */")
                return True
            c = self.cond(mn)
            self.spill()
            exits.append((t, len(self.st)))
            if c is not None:
                self.emit(f"if ({c}) goto L_{t:x};")
            else:
                self.emit(f"if (/* {mn} */) goto L_{t:x};")
            self.flags = None
            return True
        if mn in ("retn", "ret"):
            if self.st:
                self.emit(f"return {self.get(0)};  /* st0 */")
            else:
                self.emit("return;")
            return True
        if mn == "call":
            m = re.search(r"(\w+)$", ops)
            name = m.group(1) if m else ops
            if name == "__ftol":
                v = self.pop()
                self.emit(f"eax = (s32)({v});")
            else:
                self.spill()
                self.emit(f"{name}(...);  /* fpu depth {len(self.st)} */")
            return True
        return False


def main():
    ins = parse(sys.argv[1])
    bl = blocks(ins)
    depth = {bl[0].start: 0}
    order = []
    for b in bl:
        order.append(b)
    tr = Tr()
    done = {}
    work = [bl[0].start]
    bymap = {b.start: b for b in bl}
    # Follow fall-throughs and jumps to learn each block's entry depth.
    i = 0
    printed = []
    for b in bl:
        d = depth.get(b.start, 0)
        out, exits = tr.run(b, d)
        last = b.lines[-1][1]
        for t, dd in exits:
            if t is not None:
                depth.setdefault(t, dd)
        if not last.startswith("jmp") and not last.startswith("ret"):
            idx = bl.index(b)
            if idx + 1 < len(bl):
                tr.spill()
                depth.setdefault(bl[idx + 1].start, len(tr.st))
        printed.extend(out)
    print("\n".join(printed))


if __name__ == "__main__":
    main()
