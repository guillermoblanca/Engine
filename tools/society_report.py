"""T204 (S1 of VISION_MUNDO_VIVO section 13): how each society of a run progressed, from its telemetry (GBSim --telemetry=run.db).

    python -I tools/society_report.py run.db [--run N] [--days 1,5,10,20,35]

Reads the table society_days (schema 5: one row per clan and day, written by ClanWorld::Day; GB/Game/SocietyMetrics.h) and prints, per clan,
the curve of its quality of life and its parts, technologies, population, generations, lineages, worn paths, barters with other clans and
trades, with the TASK ENTROPY of the day: for each person, the Shannon entropy (bits) of the kinds of work tasks it started that day (the
`tasks` table; the needs, talk, rites and the walking and hauling every job has are left out), averaged over the people with 3 or more of them.
Low = specialists; VISION section 13.3 measured 3.22 to 3.42 bits before T181 with the same idea.
Only reads the file. Without --run, the last run in it.
"""
import argparse
import math
import sqlite3
import sys
from collections import Counter, defaultdict

DAY = 1200.0
NOT_WORK = {"sleep", "eat", "drink", "talk", "rite", "approach", "deposit", "warm", "rest", "regroup", "socialize", "idle"}


def founded(db, run):
    """clan -> the world time it was founded at (a split: the clan world's "clan_found" event, its result the new clan's id)."""
    out = {}
    for t, result in db.execute("select t, result from events where run_id = ? and kind = 'clan_found'", (run,)):
        try:
            out[int(result)] = t
        except (TypeError, ValueError):
            pass
    return out


def task_entropy(db, run, end):
    """(clan, day) -> mean per-person entropy of its work tasks that day.
    The tasks of a clan founded later are stamped with its own clock (0 at its founding), so they are moved by its founding time; a row
    that would then fall past the end of the run was already on the world's clock (a member who brought its task along) and stays."""
    born = founded(db, run)
    per = defaultdict(Counter)
    for clan, member, kind, start in db.execute("select clan_id, member, kind, start from tasks where run_id = ?", (run,)):
        if kind in NOT_WORK or start is None:
            continue
        if clan in born and start + born[clan] <= end + 1.0:
            start += born[clan]
        per[(clan, int(start // DAY) + 1, member)][kind] += 1
    sums = defaultdict(lambda: [0.0, 0])
    for (clan, day, _member), counts in per.items():
        n = sum(counts.values())
        if n < 3:
            continue
        h = -sum((c / n) * math.log2(c / n) for c in counts.values())
        s = sums[(clan, day)]
        s[0] += h
        s[1] += 1
    return {k: v[0] / v[1] for k, v in sums.items() if v[1]}


def fmt(v, digits=2):
    if v is None or (isinstance(v, float) and v < 0):
        return "-"
    return f"{v:.{digits}f}" if isinstance(v, float) else str(v)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("db")
    ap.add_argument("--run", type=int, default=None)
    ap.add_argument("--days", default="", help="only these days (comma separated), e.g. 1,5,10,20,35")
    a = ap.parse_args()
    db = sqlite3.connect(f"file:{a.db}?mode=ro", uri=True)
    if not db.execute("select 1 from sqlite_master where name = 'society_days'").fetchone():
        sys.exit(f"{a.db}: no society_days table (a run of schema 4 or older, before T204)")
    run = a.run if a.run is not None else db.execute("select max(run_id) from runs").fetchone()[0]
    info = db.execute("select seed, schema_version, sim_seconds, note from runs where run_id = ?", (run,)).fetchone()
    days = {int(d) for d in a.days.split(",") if d.strip()} if a.days else None
    end = (info[2] if info and info[2] else None) or db.execute("select max(t) from society_days where run_id = ?", (run,)).fetchone()[0] or 0.0
    entropy = task_entropy(db, run, end)
    rows = db.execute("select clan_id, day, alive, qol, needs, shelter, food, tools, health, longevity, life_techs, techs, generations, lineages, "
                      "trail_cells, trades_out, trades_in, roles, skill_entropy, inventors, splits, births, deaths "
                      "from society_days where run_id = ? order by clan_id, day, rowid", (run,)).fetchall()
    if not rows:
        sys.exit(f"{a.db}: run {run} has no society rows (a run of one clan writes none)")
    seed, schema, seconds, note = info if info else (None, None, None, None)
    print(f"run {run}: seed {seed}, schema {schema}, {fmt((seconds or 0.0) / DAY, 1)} days{' - ' + note if note else ''}")
    head = (f"{'day':>4} {'alive':>5} {'qol':>5} | {'needs':>5} {'shelt':>5} {'food':>5} {'tools':>5} {'healt':>5} {'longv':>5} {'ltech':>5} | "
            f"{'techs':>5} {'gen':>3} {'lin':>3} {'trail':>5} {'t_out':>5} {'t_in':>4} {'roles':>5} {'skH':>4} {'taskH':>5} {'inv':>3} {'split':>5} {'born':>4} {'dead':>4}")
    by_clan = defaultdict(list)
    for r in rows:
        by_clan[r[0]].append(r)
    last = {}
    for clan, rs in by_clan.items():
        seen = {}
        for r in rs:
            seen[r[1]] = r   # (a day written twice keeps its last row)
        print(f"\nclan {clan}")
        print(head)
        for day in sorted(seen):
            if days and day not in days:
                continue
            (_c, d, alive, qol, needs, shelter, food, tools, health, longevity, ltech, techs, gen, lin, trail, tout, tin, roles, skh, inv, split,
             born, dead) = seen[day]
            print(f"{d:>4} {alive:>5} {fmt(qol, 3):>5} | {fmt(needs):>5} {fmt(shelter):>5} {fmt(food):>5} {fmt(tools):>5} {fmt(health):>5} {fmt(longevity):>5} "
                  f"{fmt(ltech):>5} | {techs:>5} {gen:>3} {lin:>3} {trail:>5} {tout:>5} {tin:>4} {roles:>5} {fmt(skh):>4} {fmt(entropy.get((clan, d))):>5} "
                  f"{inv:>3} {split:>5} {born:>4} {dead:>4}")
        last[clan] = seen[max(seen)]
    alive = sum(r[2] for r in last.values())
    qol = sum(r[2] * r[3] for r in last.values()) / alive if alive else 0.0
    techs = [r[11] for r in last.values()]
    print(f"\nworld, last day: {len(last)} clans, {alive} alive, qol {qol:.3f} (by population), technologies {min(techs)}..{max(techs)}, "
          f"generation {max(r[12] for r in last.values())}, trades with other clans {sum(r[15] for r in last.values()) // 2}")


if __name__ == "__main__":
    main()
