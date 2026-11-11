#!/usr/bin/env python3
import argparse
import json
import socket
import time


def exchange(path, operation, **values):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(10)
        client.connect(path)
        client.sendall(json.dumps({"op": operation, **values}).encode() + b"\n")
        with client.makefile("rb") as stream:
            response = json.loads(stream.readline())
    if not response["ok"]:
        raise RuntimeError(response["error"])
    return response["result"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("socket")
    parser.add_argument("--infer", action="store_true")
    args = parser.parse_args()
    ping = exchange(args.socket, "ping")
    status = exchange(args.socket, "status")
    assert ping["pong"] and status["ready"] and status["backend"] == ping["backend"]
    print(status)
    if args.infer:
        result = exchange(args.socket, "infer", question="这是通信验收。请只回答：巡检服务正常。")
        deadline = time.monotonic() + 180
        while result["state"] in ("queued", "running") and time.monotonic() < deadline:
            time.sleep(1)
            result = exchange(args.socket, "poll", job=result["id"])
        assert result["state"] == "done" and result["text"].strip(), result["state"]
        print({"state": result["state"], "model": result["model"], "characters": len(result["text"])})


if __name__ == "__main__":
    main()
