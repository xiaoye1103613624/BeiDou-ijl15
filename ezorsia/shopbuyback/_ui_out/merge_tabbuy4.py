# -*- coding: utf-8 -*-
import json
import pathlib
import urllib.request

URL = "http://127.0.0.1:10002/mcp"
KEY = {
    "name": "083-GMS",
    "ivBase64": "TSPHKw==",
    "userKeyBase64": "EwAAAFIAAAAqAAAAWwAAAAgAAAACAAAAEAAAAGAAAAAGAAAAAgAAAEMAAAAPAAAAtAAAAEsAAAA1AAAABQAAABsAAAAKAAAAXwAAAAkAAAAPAAAAUAAAAAwAAAAbAAAAMwAAAFUAAAABAAAACQAAAFIAAADeAAAAxwAAAB4AAAA=",
}
ROOT = r"F:\MXD_dev\BeiDou-Client\Data\UI\UIWindow.img"
OUT = pathlib.Path(__file__).resolve().parent


def http(payload, session=None):
    data = json.dumps(payload).encode("utf-8")
    headers = {
        "Content-Type": "application/json",
        "Accept": "application/json, text/event-stream",
    }
    if session:
        headers["Mcp-Session-Id"] = session
    req = urllib.request.Request(URL, data=data, headers=headers)
    with urllib.request.urlopen(req, timeout=120) as resp:
        sid = resp.headers.get("Mcp-Session-Id") or session
        raw = resp.read().decode("utf-8")
        for line in raw.splitlines():
            if line.startswith("data:"):
                return sid, json.loads(line[5:].strip())
        return sid, json.loads(raw) if raw.strip() else None


def tool(sid, name, args, tid=1):
    _, res = http(
        {
            "jsonrpc": "2.0",
            "id": tid,
            "method": "tools/call",
            "params": {"name": name, "arguments": args},
        },
        session=sid,
    )
    return res


def main():
    sid, _ = http(
        {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": "2025-03-26",
                "capabilities": {},
                "clientInfo": {"name": "buyback-ui", "version": "1"},
            },
        }
    )
    http({"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}}, session=sid)
    print("load", tool(sid, "load_files", {"paths": [ROOT], "key": KEY}, tid=2))

    ops = []
    for state in ("enabled", "disabled"):
        b64 = (OUT / f"tabbuy4_{state}.b64.txt").read_text(encoding="utf-8").strip()
        ops.append(
            {
                "op": "create_child",
                "rootPath": ROOT,
                "nodePath": f"Shop/TabBuy/{state}",
                "type": "CANVAS",
                "name": "4",
                "x": 0,
                "y": 0,
                "pngFormat": "ARGB8888",
                "base64Png": b64,
            }
        )
    res = tool(sid, "mutate_nodes", {"operations": ops}, tid=3)
    print("mutate", json.dumps(res, ensure_ascii=False)[:4000])
    save = tool(
        sid,
        "save_node",
        {"rootPath": ROOT, "unloadAfterSave": True},
        tid=4,
    )
    print("save", json.dumps(save, ensure_ascii=False)[:2000])


if __name__ == "__main__":
    main()
