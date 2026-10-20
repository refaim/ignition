"""Index the disassembly dumped by ida_dump.py.

For each function: its callees, its callers, the imports it calls and the
strings it references. Writes .ida-mcp/index.json and prints a summary of
the game functions (those IDA has not named from its signatures).

    python3 -I tools/ida_index.py [addr...]
"""
import json
import os
import re
import sys

ASM = os.path.join(".ida-mcp", "asm")

call_re = re.compile(r"\bcall\s+(?:near ptr\s+)?(sub_[0-9A-F]+|[?_A-Za-z][\w@?$]*)")
imp_re = re.compile(r"__imp_(\w+)")
str_re = re.compile(r';\s*"(.*?)"')
off_re = re.compile(r"\boffset\s+(sub_[0-9A-F]+)")


def main():
    funcs = {}
    names = {}
    for line in open(os.path.join(".ida-mcp", "funcs.tsv")):
        addr, size, name = line.rstrip("\n").split("\t")
        funcs[addr] = {"name": name, "size": int(size, 16), "calls": set(),
                       "imports": set(), "strings": [], "callers": set(),
                       "refs": set()}
        names[name] = addr
    for fn in os.listdir(ASM):
        addr = "0x" + fn.split("_", 1)[0]
        if addr not in funcs:
            continue
        f = funcs[addr]
        for line in open(os.path.join(ASM, fn)):
            for m in call_re.finditer(line):
                t = m.group(1)
                if t in names:
                    f["calls"].add(names[t])
            for m in off_re.finditer(line):
                if m.group(1) in names:
                    f["refs"].add(names[m.group(1)])
            for m in imp_re.finditer(line):
                f["imports"].add(m.group(1))
            for m in str_re.finditer(line):
                if m.group(1) not in f["strings"]:
                    f["strings"].append(m.group(1))
    for a, f in funcs.items():
        for c in f["calls"] | f["refs"]:
            funcs[c]["callers"].add(a)
    out = {a: {k: sorted(v) if isinstance(v, set) else v for k, v in f.items()}
           for a, f in funcs.items()}
    json.dump(out, open(os.path.join(".ida-mcp", "index.json"), "w"), indent=1)
    want = sys.argv[1:]
    for a in sorted(out):
        f = out[a]
        if want and a not in want and f["name"] not in want:
            continue
        if not want and not f["name"].startswith("sub_"):
            continue
        print("%s %-12s %5d in=%-3d calls=%s imp=%s str=%s" % (
            a, f["name"], f["size"], len(f["callers"]),
            ",".join(out[c]["name"] for c in f["calls"])[:80],
            ",".join(f["imports"])[:60], " | ".join(f["strings"])[:120]))


if __name__ == "__main__":
    main()
