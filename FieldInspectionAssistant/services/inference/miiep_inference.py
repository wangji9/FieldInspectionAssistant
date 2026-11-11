#!/usr/bin/env python3
"""Local JSON endpoint for project analysis, backed by the board's RKNN3 worker."""
import argparse
import json
import os
from pathlib import Path
import re
import socketserver
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

MAX_LINE = 65536
MODEL = "qwen35"
BASE = "http://127.0.0.1:1828"


def upstream(path, payload=None):
    request = Request(BASE + path, data=None if payload is None else json.dumps(payload).encode(),
                      headers={"Content-Type": "application/json"})
    try:
        with urlopen(request, timeout=5) as response:
            return json.load(response)
    except HTTPError as error:
        raise ValueError(json.load(error).get("error", "推理服务拒绝请求")) from error
    except (URLError, TimeoutError) as error:
        raise ValueError("板端推理服务不可用") from error


def dispatch(request, mock):
    if not isinstance(request, dict):
        raise ValueError("需要 JSON 对象")
    operation = request.get("op")
    backend = "mock" if mock else "RKNN3/RK1828"
    if operation == "ping":
        return {"pong": True, "backend": backend}
    if operation == "status":
        if mock:
            return {"backend": backend, "ready": True, "model": "mock"}
        models = upstream("/api/status").get("models", [])
        ready = any(m.get("model") == MODEL and m.get("hardware") == "RK1828" and m.get("ready") for m in models)
        return {"backend": backend, "model": MODEL, "ready": ready}
    if operation == "infer":
        question = request.get("question")
        if not isinstance(question, str) or not question.strip() or len(question) > 12000:
            raise ValueError("分析内容为空或过长")
        if mock:
            return {"state": "done", "text": "Mock 通信测试结果，无真实模型分析。", "model": "mock"}
        return upstream("/api/jobs", {"mode": "text", "model": MODEL, "max_tokens": 256, "question": question})
    if operation in ("poll", "cancel"):
        job = request.get("job")
        if mock or not isinstance(job, str) or not re.fullmatch(r"[0-9a-f]{32}", job):
            raise ValueError("任务编号无效")
        return upstream("/api/jobs/" + job + ("/cancel" if operation == "cancel" else ""),
                        {} if operation == "cancel" else None)
    raise ValueError("未知操作")


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        self.connection.settimeout(8)
        try:
            line = self.rfile.readline(MAX_LINE + 1)
            if not line.endswith(b"\n") or len(line) > MAX_LINE:
                raise ValueError("请求不完整或超过大小限制")
            result = {"ok": True, "result": dispatch(json.loads(line), self.server.mock)}
        except (OSError, ValueError) as error:
            result = {"ok": False, "error": str(error)}
        try:
            self.wfile.write(json.dumps(result, ensure_ascii=False).encode() + b"\n")
        except OSError:
            pass


class Server(socketserver.ThreadingUnixStreamServer):
    daemon_threads = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socket", type=Path, required=True)
    parser.add_argument("--backend", choices=("rk1828", "mock"), default="rk1828")
    args = parser.parse_args()
    os.umask(0o077)
    args.socket.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    # systemd creates a fresh private RuntimeDirectory for each service lifetime.
    with Server(str(args.socket), Handler) as server:
        server.mock = args.backend == "mock"
        try:
            server.serve_forever()
        finally:
            args.socket.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
