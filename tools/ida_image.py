"""Save the initial bytes of IGN_WIN.EXE's data sections from IDA.

Writes .ida-mcp/image.bin covering 0x479000..0x4c6000 (.rdata and the
initialised .data), unreadable bytes as zero, and .ida-mcp/image.json
with the base address.

    python3 -I tools/ida_image.py
"""
import json
import os
import urllib.request

URL = "http://127.0.0.1:13337/mcp"
BASE, END, STEP = 0x479000, 0x4c6000, 0x1000


def call(name, args):
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                       "params": {"name": name, "arguments": args}}).encode()
    req = urllib.request.Request(URL, body, {"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=120) as r:
        res = json.loads(json.load(r)["result"]["content"][0]["text"])
    return res if isinstance(res, list) else res.get("result", res)


def main():
    out = bytearray()
    for a in range(BASE, END, STEP):
        r = call("get_bytes", {"regions": [{"addr": hex(a), "size": STEP}]})[0]
        data = r.get("data")
        out += bytes(int(x, 16) for x in data.split()) if data else bytes(STEP)
    with open(os.path.join(".ida-mcp", "image.bin"), "wb") as f:
        f.write(out)
    json.dump({"base": BASE, "end": END}, open(os.path.join(".ida-mcp", "image.json"), "w"))
    print(len(out))


if __name__ == "__main__":
    main()
