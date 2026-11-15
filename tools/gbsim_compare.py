"""Compare two GBSim runs (base and new) row by row, to prove that a cut of a refactor changed nothing (TRIBE_REFACTOR.md section 4).

Usage:
  python -I tools/gbsim_compare.py BASE.db NEW.db BASE.log NEW.log
  python -I tools/gbsim_compare.py BASE.db NEW.db                     (the .log next to each .db, same name, is used when present)
  python -I tools/gbsim_compare.py --pairs DIR_BASE DIR_NEW            (every run of one folder with its pair in the other)

A run is the telemetry database of `--telemetry=X.db` and the text of `--report` (`GBSim ... > X.log 2>&1`). In --pairs a run is paired
by its key: the trailing digits of its name (the seed: base_5.db goes with new_5.db and with s5.db), or the whole name when it has none.

Verdict (the last line) and exit code:
  SAME     0  every compared table and the report are identical
  DIFF     1  something differs; the first difference of each table and of the report is printed
  INVALID  2  nothing is proven: a file is missing, a run did not load its content ("did not load"), did not finish, holds no rows in a
              table every run fills, holds more than one run, or the two runs have different sets of tables. Also bad usage.

What is compared:
  - Tables the sink never drops (STRICT): row by row in rowid order (the order the simulation wrote them), every column but run_id,
    values exact (REAL included: the same build on the same input gives the same bits).
  - Tables the sink may drop rows from when its queue is full (LOSSY): the same, only when neither run dropped a sample
    (runs.samples_dropped = 0, the usual case); otherwise they are reported "skipped (lossy)" and do not decide the verdict.
  - The --report text, line by line, without blank lines.

What is ignored, because it is not deterministic or not the simulation's:
  - the table `runs` (start date, commit hash, run_id) and the column run_id of every table;
  - report lines with wall time ("... s of wall time"), the content check of the run's start ("content check: ..."), and in every line
    durations in milliseconds ("12.3 ms") and the seconds the terrain graph took ("world of the terrain graph ... (3.1 s)", --graph);
  - in the closing line "finished after D days: N samples, N dropped, ...": the sample counts (dropping depends on the writer thread);
  - the paths of the files (only their content is read).
"""
import os
import re
import sqlite3
import sys

STRICT = ["individuals", "events", "tasks", "clan_relations", "clan_days", "world_nodes", "society_days"]
LOSSY = ["samples", "clan_samples", "decisions", "relations"]
MUST_HAVE_ROWS = ["individuals", "events", "tasks", "clan_days"]   # every run that ran a day writes these
LOG_SKIP = ("wall time", "content check:")
LOG_SUBS = [(re.compile(r"after ([0-9.]+) days: \d+ samples, \d+ dropped"), r"after \1 days: N samples, N dropped"),
            (re.compile(r"\b\d+(\.\d+)? ?ms\b"), "N ms"),
            (re.compile(r"^(world of the terrain graph .*) \([0-9.]+ s\)$"), r"\1 (N s)")]
LOG_END = re.compile(r"^(finished|stopped) after [0-9.]+ days: ", re.M)


class Invalid(Exception):
    pass


def connect(path):
    try:
        db = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
        db.execute("select count(*) from sqlite_master").fetchone()
        return db
    except sqlite3.Error as e:
        raise Invalid(f"{path}: not a database ({e})")


def tables(db):
    return {r[0] for r in db.execute("select name from sqlite_master where type='table'")}


def rows(db, table):
    cols = [c[1] for c in db.execute(f"pragma table_info({table})") if c[1] != "run_id"]
    return [tuple(r) for r in db.execute(f"select {','.join(cols)} from {table} order by rowid")]


def count(db, table):
    return db.execute(f"select count(*) from {table}").fetchone()[0]


def dropped(db):
    try:
        return sum(r[0] or 0 for r in db.execute("select samples_dropped from runs"))
    except sqlite3.Error:
        return -1


def report_lines(path):
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if not line.strip() or any(s in line for s in LOG_SKIP):
                continue
            for rx, rep in LOG_SUBS:
                line = rx.sub(rep, line)
            out.append(line.rstrip())
    return out


def clip(row, width=400):
    """A row as printed: long texts (the options of a decision, the beliefs of a day) cut, the full row is in the database."""
    text = repr(row)
    return text if len(text) <= width else text[:width] + f"... ({len(text)} chars)"


def first_diff(a, b):
    return next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))


def check_run(path_db, path_log):
    """The reasons a run proves nothing (empty: it ran). A run that compares SAME must have loaded, run and finished."""
    why = []
    if not path_log or not os.path.isfile(path_log):
        why.append(f"{path_log or '(no log)'}: the log is missing")
    else:
        with open(path_log, encoding="utf-8", errors="replace") as f:
            text = f.read()
        if "did not load" in text:
            why.append(f"{path_log}: the content did not load")
        if not LOG_END.search(text):
            why.append(f"{path_log}: the run did not finish (no 'finished after' line)")
    if not os.path.isfile(path_db):
        why.append(f"{path_db}: the database is missing")
        return why
    if os.path.getsize(path_db) == 0:
        why.append(f"{path_db}: the database is empty (0 bytes)")
        return why
    try:
        db = connect(path_db)
    except Invalid as e:
        why.append(str(e))
        return why
    have = tables(db)
    if "runs" not in have:
        why.append(f"{path_db}: no table runs")
    else:
        n = count(db, "runs")
        if n != 1:
            why.append(f"{path_db}: {n} runs in one database (delete it before a run: the sink appends)")
        elif not db.execute("select closed from runs").fetchone()[0]:
            why.append(f"{path_db}: the run was not closed (it stopped before the end)")
    for t in MUST_HAVE_ROWS:
        if t not in have:
            why.append(f"{path_db}: table {t} is missing")
        elif count(db, t) == 0:
            why.append(f"{path_db}: table {t} is empty")
    db.close()
    return why


def compare_db(base, new):
    """(same, problems): problems make the comparison invalid (different sets of tables)."""
    same, problems = True, []
    a, b = connect(base), connect(new)
    ta, tb = tables(a), tables(b)
    if ta != tb:
        problems.append(f"the sets of tables differ: only base {sorted(ta - tb)}, only new {sorted(tb - ta)}")
    lossless = dropped(a) == 0 and dropped(b) == 0
    for t in STRICT + LOSSY:
        if t not in ta or t not in tb:
            continue
        if t in LOSSY and not lossless:
            print(f"  {t}: skipped (lossy: dropped {dropped(a)} / {dropped(b)})")
            continue
        ra, rb = rows(a, t), rows(b, t)
        if ra == rb:
            print(f"  {t}: identical ({len(ra)} rows)")
            continue
        same = False
        i = first_diff(ra, rb)
        print(f"  {t}: DIFF ({len(ra)} vs {len(rb)} rows); first difference at row {i}")
        print("    base:", clip(ra[i]) if i < len(ra) else "<end>")
        print("    new: ", clip(rb[i]) if i < len(rb) else "<end>")
    a.close()
    b.close()
    return same, problems


def compare_log(base, new):
    la, lb = report_lines(base), report_lines(new)
    if la == lb:
        print(f"  report: identical ({len(la)} lines)")
        return True
    i = first_diff(la, lb)
    print(f"  report: DIFF ({len(la)} vs {len(lb)} lines) at line {i}")
    print("    base:", la[i] if i < len(la) else "<end>")
    print("    new: ", lb[i] if i < len(lb) else "<end>")
    return False


def compare_run(db_a, db_b, log_a, log_b):
    """(same, valid) of one pair of runs, printing what it found."""
    why = check_run(db_a, log_a) + check_run(db_b, log_b)
    for w in why:
        print(f"  INVALID: {w}")
    if why:
        return False, False
    same, problems = compare_db(db_a, db_b)
    for p in problems:
        print(f"  INVALID: {p}")
    same = compare_log(log_a, log_b) and same
    return same, not problems


def key_of(name):
    m = re.search(r"(\d+)$", name)
    return m.group(1) if m else name


def runs_in(folder):
    """key -> stem of every .db or .log of a folder; raises Invalid when two runs share a key."""
    found = {}
    for f in sorted(os.listdir(folder)):
        stem, ext = os.path.splitext(f)
        if ext not in (".db", ".log"):
            continue
        k = key_of(stem)
        if found.get(k, stem) != stem:
            raise Invalid(f"{folder}: {found[k]} and {stem} have the same key {k}")
        found[k] = stem
    return found


def pairs(dir_a, dir_b):
    valid = same = True
    try:
        ra, rb = runs_in(dir_a), runs_in(dir_b)
    except (OSError, Invalid) as e:
        print(f"INVALID: {e}")
        return 2
    if not ra or not rb:
        print(f"INVALID: no runs in {dir_a if not ra else dir_b}")
        return 2
    for k in sorted(set(ra) | set(rb), key=lambda s: (len(s), s)):
        print(f"{ra.get(k, '-')} / {rb.get(k, '-')}")
        if k not in ra or k not in rb:
            print(f"  INVALID: run {k} is only in {dir_a if k in ra else dir_b}")
            valid = False
            continue
        pa, pb = os.path.join(dir_a, ra[k]), os.path.join(dir_b, rb[k])
        s, v = compare_run(pa + ".db", pb + ".db", pa + ".log", pb + ".log")
        same &= s
        valid &= v
    return verdict(same, valid)


def main(argv):
    if len(argv) == 3 and argv[0] == "--pairs":
        if not all(os.path.isdir(p) for p in argv[1:]):
            print(f"INVALID: not a folder: {[p for p in argv[1:] if not os.path.isdir(p)]}")
            return 2
        return pairs(argv[1], argv[2])
    if len(argv) not in (2, 4) or argv[0].startswith("-"):
        print(__doc__)
        return 2
    if len(argv) == 4:
        logs = argv[2], argv[3]
    else:
        logs = tuple(os.path.splitext(p)[0] + ".log" for p in argv[:2])
    try:
        same, valid = compare_run(argv[0], argv[1], *logs)
    except Invalid as e:
        print(f"  INVALID: {e}")
        same, valid = False, False
    return verdict(same, valid)


def verdict(same, valid):
    if not valid:
        print("INVALID (a run is missing or did not run: nothing proven)")
        return 2
    print("SAME" if same else "DIFF")
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
