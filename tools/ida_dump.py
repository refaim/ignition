"""Dump the disassembly of every function in the open IDA database.

Talks to the IDA MCP plugin over HTTP and writes one file per function to
.ida-mcp/asm/<addr>_<name>.asm, plus .ida-mcp/funcs.tsv (addr, size, name).

    python3 -I tools/ida_dump.py [--named-only]
"""
import json
import os
import sys
import urllib.request

URL = "http://127.0.0.1:13337/mcp"
OUT = os.path.join(".ida-mcp", "asm")


def call(name, args):
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                       "params": {"name": name, "arguments": args}}).encode()
    req = urllib.request.Request(URL, body, {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        res = json.load(r)["result"]
    return json.loads(res["content"][0]["text"])


def main():
    funcs = call("list_funcs", {"queries": {"count": 0, "offset": 0}})
    if "_download_url" in funcs:
        with urllib.request.urlopen(funcs["_download_url"], timeout=120) as r:
            funcs = json.load(r)
    funcs = funcs["result"][0]["data"] if "result" in funcs else funcs[0]["data"]
    os.makedirs(OUT, exist_ok=True)
    for old in os.listdir(OUT):
        os.remove(os.path.join(OUT, old))
    with open(os.path.join(".ida-mcp", "funcs.tsv"), "w") as tsv:
        for f in funcs:
            tsv.write("%s\t%s\t%s\n" % (f["addr"], f["size"], f["name"]))
    for i, f in enumerate(funcs):
        d = call("disasm", {"addr": f["addr"], "max_instructions": 50000})
        lines = d.get("asm", {}).get("lines", "")
        safe = "".join(c if c.isalnum() or c in "_@" else "_" for c in f["name"])
        with open(os.path.join(OUT, "%s_%s.asm" % (f["addr"][2:], safe)), "w") as fp:
            fp.write(lines + "\n")
        sys.stderr.write("\r%d/%d" % (i + 1, len(funcs)))
    sys.stderr.write("\n")


if __name__ == "__main__":
    main()
