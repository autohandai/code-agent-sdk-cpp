"""Run the C++ consumer against the real CLI with local HTTP mocks."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer


def main():
    cli = os.environ.get("AUTOHAND_TEST_CLI_PATH")
    if not cli:
        print("Set AUTOHAND_TEST_CLI_PATH to enable the compiled CLI integration.")
        return
    requests = []
    failures = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            self.respond()

        def do_POST(self):
            self.respond()

        def respond(self):
            try:
                if self.path == "/auth/me":
                    assert self.headers["Authorization"] == "Bearer session-token"
                    response = {"authenticated": True, "user": {"id": "cpp-sdk", "email": "sdk@example.test"}}
                else:
                    assert self.path == "/v1/chat/completions", self.path
                    assert self.headers["Authorization"] == "Bearer inference-key"
                    request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                    assert request["model"] == "fantail"
                    requests.append(request)
                    first = len(requests) == 1
                    message = {"role": "assistant", "content": "continued from persisted evidence"}
                    if first:
                        message = {"role": "assistant", "content": "Read evidence", "tool_calls": [{
                            "id": "read-evidence", "type": "function", "function": {
                                "name": "read_file", "arguments": json.dumps({"path": "evidence.txt"})}}]}
                    response = {"id": "cpp-completion", "choices": [{"message": message,
                        "finish_reason": "tool_calls" if first else "stop"}],
                        "usage": {"prompt_tokens": 10, "completion_tokens": 5, "total_tokens": 15}}
                body = json.dumps(response).encode()
                self.send_response(200)
            except Exception as error:
                failures.append(error)
                body = json.dumps({"error": str(error)}).encode()
                self.send_response(500)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    with tempfile.TemporaryDirectory(prefix="autohand-cpp-harness-") as directory:
        root = Path(directory)
        workspace, home = root / "workspace", root / "home"
        workspace.mkdir()
        home.mkdir()
        (workspace / "evidence.txt").write_text("cpp-persisted-marker")
        config = home / "config.json"
        saved = {"provider": "openai", "auth": {"token": "session-token"},
                 "openai": {"apiKey": "saved-key", "model": "saved-model", "baseUrl": "http://127.0.0.1:1/unused"},
                 "features": {"automaticSpecialists": False}, "agent": {"autoMemory": False},
                 "telemetry": {"enabled": False}}
        config.write_text(json.dumps(saved))
        extension = workspace / ".autohand" / "extensions" / "sdk.cpp"
        (extension / "agents").mkdir(parents=True)
        (extension / "autohand.extension.json").write_text(json.dumps({
            "schemaVersion": 1, "extensionApi": 1, "id": "sdk.cpp", "name": "C++ helper",
            "version": "1.0.0", "description": "Local SDK integration",
            "contributes": {"agents": ["agents/helper.md"]}}))
        (extension / "agents" / "helper.md").write_text(
            "---\ndescription: C++ helper\ntools: read_file\n---\nPrivate helper instructions.\n")
        with HTTPServer(("127.0.0.1", 0), Handler) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            try:
                environment = {**os.environ, "AUTOHAND_TEST_CLI_PATH": cli,
                    "AUTOHAND_CPP_HARNESS_BASE_URL": f"http://127.0.0.1:{server.server_port}",
                    "AUTOHAND_CPP_HARNESS_WORKSPACE": str(workspace),
                    "AUTOHAND_CPP_HARNESS_HOME": str(home), "AUTOHAND_CPP_HARNESS_CONFIG": str(config)}
                subprocess.run([sys.argv[1]], env=environment, check=True, timeout=60)
                assert not failures, failures
                assert len(requests) == 2, len(requests)
                assert "cpp-persisted-marker" in json.dumps(requests[1]["messages"])
                persisted = json.loads(config.read_text())
                assert persisted["provider"] == "openai"
                assert persisted["openai"] == saved["openai"]
                assert "autohandai" not in persisted
                print("Exactly two local inference calls; saved provider credentials preserved.")
            finally:
                server.shutdown()
                thread.join()


if __name__ == "__main__":
    main()
