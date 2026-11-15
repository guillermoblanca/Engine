"""Launch a batch of GBSim runs in parallel with one exe (it builds nothing) and, optionally, compare two batches (TRIBE_REFACTOR.md section 4).

Usage:
  python -I tools/gbsim_ab.py run --exe EXE --seeds 5,7,77 --days 6 --out DIR [--tag TAG] [--jobs N] [--extra "--pack=x --know=y"]
  python -I tools/gbsim_ab.py compare DIR_BASE DIR_NEW                  (tools/gbsim_compare.py --pairs)
  python -I tools/gbsim_ab.py run ... --compare DIR_BASE                (run, then compare DIR_BASE with --out)

`run` writes DIR/<tag>_<seed>.db (the telemetry) and DIR/<tag>_<seed>.log (stdout and stderr: the --report) for every seed, with the
standard flags of the A/B of a cut:
  --seed=S --days=D --clans=3 --members=6 --biome=temperate --combat --report --telemetry=DIR/<tag>_<seed>.db
plus --extra. A .db (and its -wal/-shm) left by an earlier run is deleted first: the sink appends to an existing database, and two runs in
one file would compare INVALID. The runs go in their own processes (default: all seeds at once; --jobs caps it), with DIR as the working
directory. The exe reads the assets of the tree it was built from (GB_ASSETS_DIR is compiled in): use, for base and new, exes whose trees
exist and hold the same assets/, or the runs will not load ("did not load", INVALID).

Exit code: run: 0 when every run exited 0, 1 otherwise (then the compare); compare: that of gbsim_compare (0 SAME, 1 DIFF, 2 INVALID).
"""
import argparse
import os
import shlex
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True   # no tools/__pycache__ left in the tree
import gbsim_compare  # noqa: E402  (the sibling tool; -I leaves the script's folder off sys.path)

STANDARD = ["--clans=3", "--members=6", "--biome=temperate", "--combat", "--report"]


def launch(exe, seed, days, out, tag, extra):
    db = os.path.join(out, f"{tag}_{seed}.db")
    log = os.path.join(out, f"{tag}_{seed}.log")
    for p in (db, db + "-wal", db + "-shm", db + "-journal"):
        if os.path.exists(p):
            os.remove(p)
    args = [exe, f"--seed={seed}", f"--days={days}", *STANDARD, f"--telemetry={db}", *extra]
    f = open(log, "w", encoding="utf-8")
    return subprocess.Popen(args, cwd=out, stdout=f, stderr=subprocess.STDOUT), f, args


def run(a):
    exe = os.path.abspath(a.exe)
    if not os.path.isfile(exe):
        print(f"no exe {exe}")
        return 2
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    seeds = [s.strip() for s in a.seeds.split(",") if s.strip()]
    extra = shlex.split(a.extra) if a.extra else []
    jobs = a.jobs or len(seeds)
    pending, live, failed = list(seeds), [], 0
    t0 = time.time()
    while pending or live:
        while pending and len(live) < jobs:
            seed = pending.pop(0)
            p, f, args = launch(exe, seed, a.days, out, a.tag, extra)
            print(f"start seed {seed}: {' '.join(args)}", flush=True)
            live.append((seed, p, f))
        for item in list(live):
            seed, p, f = item
            if p.poll() is None:
                continue
            f.close()
            live.remove(item)
            failed += p.returncode != 0
            print(f"seed {seed}: exit {p.returncode} ({time.time() - t0:.0f} s)", flush=True)
        time.sleep(0.5)
    print(f"{len(seeds)} runs in {out} ({time.time() - t0:.0f} s), {failed} failed")
    if a.compare:
        return gbsim_compare.main(["--pairs", os.path.abspath(a.compare), out])
    return 1 if failed else 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="launch the runs of one exe")
    r.add_argument("--exe", required=True, help="GBSim.exe (built elsewhere: this tool builds nothing)")
    r.add_argument("--seeds", default="5,7,77", help="comma-separated seeds (default 5,7,77)")
    r.add_argument("--days", required=True, help="days of every run (6: the quick level; 35: the full one)")
    r.add_argument("--out", required=True, help="folder of the runs (created; on D:, not C:)")
    r.add_argument("--tag", default="run", help="prefix of the files: <tag>_<seed>.db/.log (default run)")
    r.add_argument("--jobs", type=int, default=0, help="runs at once (default: every seed)")
    r.add_argument("--extra", default="", help="more GBSim flags, quoted as one argument")
    r.add_argument("--compare", metavar="DIR_BASE", help="then compare DIR_BASE (the base runs) with --out")
    c = sub.add_parser("compare", help="compare two folders of runs (gbsim_compare --pairs)")
    c.add_argument("base")
    c.add_argument("new")
    a = ap.parse_args(argv)
    if a.cmd == "compare":
        return gbsim_compare.main(["--pairs", a.base, a.new])
    return run(a)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
