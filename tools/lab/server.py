#!/usr/bin/env python3
"""The laboratory: a small web server to launch simulations of the engine and read what happened in them (docs/LABORATORIO.md).

  python tools/lab/server.py [--sim path/to/GBSim.exe] [--runs lab_runs] [--port 8765]

Then open http://127.0.0.1:8765. Only the standard library; it listens on this machine only.
Each run is a GBSim process that writes one telemetry file (.db) in the runs folder; the page reads those files through /api.
"""
import argparse
import json
import os
import re
import signal
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lab_data  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
EXE = "GBSim.exe" if os.name == "nt" else "GBSim"
NAME_OK = re.compile(r"^[A-Za-z0-9_.-]{1,80}$")


def find_sim(explicit):
    if explicit:
        return explicit
    candidates = []
    for root, dirs, files in os.walk(os.path.dirname(REPO)):
        depth = root[len(os.path.dirname(REPO)):].count(os.sep)
        if depth > 4:
            dirs[:] = []
            continue
        dirs[:] = [d for d in dirs if not d.startswith(".") and d not in ("node_modules", "_deps", "CMakeFiles")]
        if EXE in files:
            p = os.path.join(root, EXE)
            candidates.append((os.path.getmtime(p), p))
    return max(candidates)[1] if candidates else None


class Jobs:
    """The GBSim processes started from the page: their progress is read from what they print."""

    def __init__(self):
        self.lock = threading.Lock()
        self.jobs = {}

    def start(self, name, cmd):
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1,
                                creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0))
        job = {"name": name, "cmd": cmd, "proc": proc, "started": time.time(), "progress": "", "day": 0.0, "of": 0.0, "stopped": "", "lines": [], "done": False, "code": None}
        with self.lock:
            self.jobs[name] = job
        threading.Thread(target=self._read, args=(job,), daemon=True).start()

    def _read(self, job):
        for line in job["proc"].stdout:
            line = line.rstrip()
            job["lines"] = (job["lines"] + [line])[-30:]
            m = re.match(r"day (\d+) / ([\d.]+)", line)
            if m:
                job["day"], job["of"] = float(m.group(1)), float(m.group(2))
            if line.startswith("stop condition:"):
                job["stopped"] = line[len("stop condition:"):].strip()
        job["code"] = job["proc"].wait()
        job["done"] = True

    def stop(self, name):
        job = self.jobs.get(name)
        if not job or job["done"]:
            return False
        try:   # a clean stop (Ctrl+C / SIGINT) so the file is closed properly; CTRL_BREAK on Windows
            job["proc"].send_signal(signal.CTRL_BREAK_EVENT if os.name == "nt" else signal.SIGINT)
        except Exception:
            job["proc"].terminate()
        return True

    def view(self):
        out = []
        for j in self.jobs.values():
            out.append({"name": j["name"], "done": j["done"], "code": j["code"], "day": j["day"], "of": j["of"], "seconds": round(time.time() - j["started"]), "stopped": j["stopped"],
                        "last": j["lines"][-3:], "cmd": " ".join(j["cmd"][1:])})
        return sorted(out, key=lambda x: x["name"], reverse=True)


class Lab:
    def __init__(self, sim, runs):
        self.sim, self.runs = sim, runs
        self.jobs = Jobs()
        os.makedirs(runs, exist_ok=True)

    def path(self, name):
        if not NAME_OK.match(name or ""):
            raise ValueError("bad run name")
        return os.path.join(self.runs, name + ".db")

    def launch(self, p):
        if not self.sim or not os.path.exists(self.sim):
            raise ValueError("GBSim was not found: build it (cmake --build <build> --target GBSim) or start the server with --sim")

        def num(key, default, lo, hi):
            v = float(p.get(key, default))
            return max(lo, min(hi, v))
        seed = int(num("seed", 1, 0, 2 ** 31))
        clans = int(num("clans", 3, 1, 12))
        members = int(num("members", 4, 1, 40))
        years = num("years", 2, 0.05, 200)
        biome = p.get("biome", "temperate")
        if biome not in ("temperate", "alpine", "desert"):
            raise ValueError("bad biome")
        name = f"{biome}_c{clans}_s{seed}_{time.strftime('%H%M%S')}"
        cmd = [self.sim, f"--seed={seed}", f"--years={years}", f"--clans={clans}", f"--members={members}", f"--biome={biome}", f"--every={int(num('every', 60, 10, 3600))}",
               f"--telemetry={self.path(name)}", f"--profile={name}"]
        if p.get("combat", True):
            cmd.append("--combat")
        if p.get("hostile"):
            cmd.append("--hostile")
        stops = p.get("stop", [])
        if "clan_dead" in stops:
            cmd.append("--stop=clan_dead")
        if "extinct" in stops:
            cmd.append("--stop=extinct")
        if p.get("stagnant_days"):
            cmd.append(f"--stop=stagnant:{num('stagnant_days', 0, 0.5, 400)}")
        self.jobs.start(name, cmd)
        return name

    def runs_list(self):
        running = {j["name"] for j in self.jobs.view() if not j["done"]}
        out = []
        for f in sorted(os.listdir(self.runs)):
            if not f.endswith(".db"):
                continue
            name = f[:-3]
            full = os.path.join(self.runs, f)
            s = lab_data.summary(full) or {}
            out.append({"name": name, "size": os.path.getsize(full), "mtime": os.path.getmtime(full), "running": name in running, **s})
        return sorted(out, key=lambda r: -r["mtime"])


def make_handler(lab):
    page = os.path.join(HERE, "index.html")

    class H(BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def send(self, code, body, ctype="application/json"):
            data = body if isinstance(body, bytes) else json.dumps(body).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", ctype + "; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            u = urlparse(self.path)
            q = parse_qs(u.query)
            try:
                if u.path == "/":
                    with open(page, "rb") as f:
                        return self.send(200, f.read(), "text/html")
                if u.path == "/api/config":
                    return self.send(200, {"sim": lab.sim, "runs": os.path.abspath(lab.runs), "found": bool(lab.sim and os.path.exists(lab.sim))})
                if u.path == "/api/runs":
                    return self.send(200, lab.runs_list())
                if u.path == "/api/jobs":
                    return self.send(200, lab.jobs.view())
                if u.path == "/api/run":
                    return self.send(200, lab_data.analyze(lab.path(q["name"][0])))
                if u.path == "/api/compare":
                    return self.send(200, lab_data.compare({n: lab.path(n) for n in q["names"][0].split(",") if n}))
                self.send(404, {"error": "not found"})
            except Exception as e:
                self.send(400, {"error": str(e)})

        def do_POST(self):
            n = int(self.headers.get("Content-Length") or 0)
            try:
                body = json.loads(self.rfile.read(n) or b"{}")
                if self.path == "/api/launch":
                    return self.send(200, {"name": lab.launch(body)})
                if self.path == "/api/stop":
                    return self.send(200, {"stopped": lab.jobs.stop(body.get("name"))})
                if self.path == "/api/delete":
                    p = lab.path(body.get("name"))
                    for ext in ("", "-wal", "-shm"):
                        if os.path.exists(p + ext):
                            os.remove(p + ext)
                    return self.send(200, {"deleted": True})
                self.send(404, {"error": "not found"})
            except Exception as e:
                self.send(400, {"error": str(e)})

    return H


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sim")
    ap.add_argument("--runs", default=os.path.join(REPO, "lab_runs"))
    ap.add_argument("--port", type=int, default=8765)
    a = ap.parse_args()
    lab = Lab(find_sim(a.sim), a.runs)
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), make_handler(lab))
    print(f"Laboratory at http://127.0.0.1:{a.port}   GBSim: {lab.sim or 'NOT FOUND'}   runs: {os.path.abspath(a.runs)}", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
