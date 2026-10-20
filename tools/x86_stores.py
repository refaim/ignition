"""Run a straight run of x86 data stores from the IDA dump and print the
bytes it writes.

Static initialisers in IGN_WIN.EXE (the menu tables at 0x404a60) are long
runs of mov, xor and or on registers and absolute addresses, guarded by
test and jnz on bits of a guard byte. This interprets that subset, with
every guard bit clear (the first run), reading the initial values of
memory from the IDA database, and stops at the first instruction it does
not know.

    python3 -I tools/x86_stores.py <asm file> [start] [stop] > writes.json

Output: {"writes": {addr: byte}, "stopped_at": addr, "reason": text}.
"""
import json
import re
import sys
import urllib.request

URL = "http://127.0.0.1:13337/mcp"

REG32 = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]
REG16 = {"ax": "eax", "cx": "ecx", "dx": "edx", "bx": "ebx",
         "sp": "esp", "bp": "ebp", "si": "esi", "di": "edi"}
REG8 = {"al": ("eax", 0), "cl": ("ecx", 0), "dl": ("edx", 0), "bl": ("ebx", 0),
        "ah": ("eax", 8), "ch": ("ecx", 8), "dh": ("edx", 8), "bh": ("ebx", 8)}
SIZE_PREFIX = {"byte": 1, "word": 2, "dword": 4, "unk": 1, "off": 4, "stru": 1,
               "dbl": 8, "flt": 4, "qword": 8}


def call(name, args):
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                       "params": {"name": name, "arguments": args}}).encode()
    req = urllib.request.Request(URL, body, {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        return json.loads(json.load(r)["result"]["content"][0]["text"])


class Machine:
    def __init__(self):
        self.reg = {r: 0 for r in REG32}
        self.mem = {}           # bytes written here
        self.cache = {}         # bytes read from IDA
        self.names = {}
        self.zf = False

    def resolve(self, name):
        """Address of a symbol: from its IDA auto name, or by lookup."""
        m = re.match(r"(?:byte|word|dword|unk|off|stru|dbl|flt|qword|a\w*?)_([0-9A-F]+)$", name)
        if m and not name.startswith("a"):
            return int(m.group(1), 16)
        if name not in self.names:
            r = call("lookup_funcs", {"queries": [name]})["result"][0]
            if r.get("fn"):
                self.names[name] = int(r["fn"]["addr"], 16)
            else:
                g = call("list_globals", {"queries": {"filter": name, "count": 50, "offset": 0}})
                hits = [x for x in g["result"][0]["data"] if x.get("name") == name]
                if not hits:
                    raise KeyError(name)
                self.names[name] = int(hits[0]["addr"], 16)
        return self.names[name]

    def read_byte(self, addr):
        if addr in self.mem:
            return self.mem[addr]
        if addr not in self.cache:
            base = addr & ~0xff
            r = call("get_bytes", {"regions": [{"addr": hex(base), "size": 256}]})
            r = r if isinstance(r, list) else r.get("result", r)
            data = r[0].get("data")
            vals = [int(x, 16) for x in data.split()] if data else [0] * 256
            for i, v in enumerate(vals):
                self.cache[base + i] = v
        return self.cache[addr]

    def read(self, addr, size):
        return sum(self.read_byte(addr + i) << (8 * i) for i in range(size))

    def write(self, addr, size, value):
        for i in range(size):
            self.mem[addr + i] = (value >> (8 * i)) & 0xff

    def get_reg(self, r):
        if r in REG32:
            return self.reg[r]
        if r in REG16:
            return self.reg[REG16[r]] & 0xffff
        full, shift = REG8[r]
        return (self.reg[full] >> shift) & 0xff

    def set_reg(self, r, v):
        if r in REG32:
            self.reg[r] = v & 0xffffffff
        elif r in REG16:
            f = REG16[r]
            self.reg[f] = (self.reg[f] & 0xffff0000) | (v & 0xffff)
        else:
            f, shift = REG8[r]
            self.reg[f] = (self.reg[f] & ~(0xff << shift)) | ((v & 0xff) << shift)

    def reg_size(self, r):
        return 4 if r in REG32 else 2 if r in REG16 else 1

    def operand(self, text):
        """(kind, value, size): kind is reg, imm or mem."""
        t = text.strip()
        t = re.sub(r";.*$", "", t).strip()
        size = None
        m = re.match(r"(byte|word|dword) ptr (.*)$", t)
        if m:
            size = {"byte": 1, "word": 2, "dword": 4}[m.group(1)]
            t = m.group(2).strip()
        if t.startswith("[esp"):
            return ("stack", 0, size)
        if t in REG32 or t in REG16 or t in REG8:
            return ("reg", t, self.reg_size(t))
        if t.startswith("offset "):
            return ("imm", self.resolve(t[7:].strip()), 4)
        m = re.match(r"^-?[0-9A-F]+h?$", t)
        if m:
            v = int(t[:-1], 16) if t.endswith("h") else int(t)
            return ("imm", v & 0xffffffff, None)
        m = re.match(r"^'(.)'$", t)
        if m:
            return ("imm", ord(m.group(1)), None)
        m = re.match(r"^([A-Za-z_][\w@?$]*)(?:\+([0-9A-F]+)h?)?$", t)
        if m:
            addr = self.resolve(m.group(1))
            if m.group(2):
                addr += int(m.group(2), 16)
            if size is None:
                prefix = m.group(1).split("_")[0]
                size = SIZE_PREFIX.get(prefix, 4)
            return ("mem", addr, size)
        raise ValueError("operand " + t)

    def value(self, op, size):
        kind, v, _ = op
        if kind == "reg":
            return self.get_reg(v)
        if kind == "imm":
            return v & ((1 << (8 * size)) - 1)
        if kind == "stack":
            return 0
        return self.read(v, size)

    def store(self, op, size, value):
        kind, v, _ = op
        if kind == "reg":
            self.set_reg(v, value)
        elif kind == "mem":
            self.write(v, size, value)
        elif kind == "stack":
            pass        # locals are not part of the result
        else:
            raise ValueError("store to immediate")


def main():
    path = sys.argv[1]
    start = int(sys.argv[2], 16) if len(sys.argv) > 2 else None
    stop = int(sys.argv[3], 16) if len(sys.argv) > 3 else None
    lines = []
    for line in open(path):
        m = re.match(r"^([0-9a-f]+)\s+(\w+)\s*(.*)$", line.strip())
        if m:
            lines.append((int(m.group(1), 16), m.group(2), m.group(3)))
    index = {a: i for i, (a, _, _) in enumerate(lines)}
    labels = {}
    for i, (a, _, _) in enumerate(lines):
        labels["loc_%X" % a] = i
    vm = Machine()
    i = index[start] if start is not None else 0
    reason = "end"
    while i < len(lines):
        addr, op, args = lines[i]
        if stop is not None and addr >= stop:
            reason = "stop"
            break
        parts = [p for p in re.split(r",(?![^\[]*\])", re.sub(r";.*$", "", args)) if p.strip()]
        try:
            if op in ("nop", "push", "pop") or (op in ("sub", "add") and parts[0].strip() == "esp"):
                pass
            elif op == "mov":
                dst = vm.operand(parts[0])
                src = vm.operand(parts[1])
                size = dst[2] or src[2] or 4
                vm.store(dst, size, vm.value(src, size))
            elif op in ("xor", "or", "and"):
                dst = vm.operand(parts[0])
                src = vm.operand(parts[1])
                size = dst[2] or src[2] or 4
                a, b = vm.value(dst, size), vm.value(src, size)
                r = a ^ b if op == "xor" else a | b if op == "or" else a & b
                vm.store(dst, size, r)
                vm.zf = r == 0
            elif op == "test":
                a = vm.operand(parts[0])
                b = vm.operand(parts[1])
                size = a[2] or b[2] or 4
                vm.zf = (vm.value(a, size) & vm.value(b, size)) == 0
            elif op in ("jnz", "jz", "jmp"):
                target = parts[0].strip().replace("short ", "")
                taken = op == "jmp" or (op == "jnz") != vm.zf
                if taken:
                    i = labels[target]
                    continue
            else:
                reason = "unknown instruction %s %s" % (op, args)
                break
        except (ValueError, KeyError) as e:
            reason = "at %x: %s" % (addr, e)
            break
        i += 1
    json.dump({"writes": {hex(a): v for a, v in sorted(vm.mem.items())},
               "stopped_at": hex(lines[i][0]) if i < len(lines) else None,
               "reason": reason}, sys.stdout)


if __name__ == "__main__":
    main()
