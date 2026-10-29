#!/usr/bin/env python3
"""A sweep of GBSim runs: seeds x clans x biomes, several processes at a time, one telemetry file per run (docs/DATOS_INDIVIDUO.md, section 5).

  sweep.py --sim build/Sim/Release/GBSim.exe --out runs --seeds 1-8 --clans 1,3 --days 5 [--biomes temperate,alpine] [--jobs 4] [--combat] [--profile name]

Then:  analyze_run.py report runs/*.db   or   analyze_run.py export runs/*.db --out analysis
A run whose file already exists is skipped, so a sweep that was stopped can be started again.
"""
import argparse, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor


def seeds(text):
    out = []
    for part in text.split(","):
        a, _, b = part.partition("-")
        out += list(range(int(a), int(b or a) + 1))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sim", required=True)
    ap.add_argument("--out", default="runs")
    ap.add_argument("--seeds", default="1-4")
    ap.add_argument("--clans", default="1")
    ap.add_argument("--biomes", default="temperate")
    ap.add_argument("--days", type=float, default=3.0)
    ap.add_argument("--members", type=int, default=4)
    ap.add_argument("--every", type=float, default=60.0)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 2))
    ap.add_argument("--combat", action="store_true")
    ap.add_argument("--profile", default="")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    todo = []
    for biome in a.biomes.split(","):
        for clans in [int(c) for c in a.clans.split(",")]:
            for seed in seeds(a.seeds):
                name = f"{biome}_c{clans}_s{seed}"
                path = os.path.join(a.out, name + ".db")
                if os.path.exists(path):
                    continue
                cmd = [a.sim, f"--seed={seed}", f"--days={a.days}", f"--clans={clans}", f"--members={a.members}", f"--biome={biome}", f"--every={a.every}",
                       f"--telemetry={path}", f"--profile={a.profile or name}"]
                if a.combat:
                    cmd.append("--combat")
                todo.append((name, cmd))

    def run(item):
        name, cmd = item
        r = subprocess.run(cmd, capture_output=True, text=True)
        last = (r.stderr.strip().splitlines() or [""])[-1]
        print(f"{name}: {'ok' if r.returncode == 0 else 'EXIT ' + str(r.returncode)}  {last}", flush=True)
        return r.returncode

    print(f"{len(todo)} runs, {a.jobs} at a time", flush=True)
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        codes = list(pool.map(run, todo))
    return 1 if any(codes) else 0


if __name__ == "__main__":
    sys.exit(main())
