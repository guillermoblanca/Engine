#!/usr/bin/env python3
"""Reads the telemetry files (.db) that GBSim or the editor write and reports what happened (docs/DATOS_INDIVIDUO.md, section 5).

  analyze_run.py report run.db [more.db ...]              a text report per run, then a table that compares them
  analyze_run.py export run.db [more.db ...] --out dir    the summaries as JSON (dashboard.json) and CSV (series.csv), for the dashboard

Only the standard library (sqlite3). A file may hold several runs; each is reported on its own.
"""
import argparse, csv, json, os, sqlite3, sys
from collections import Counter, defaultdict

LOOP_MIN = 20   # the same member failing the same target this many times is a loop


def q(db, sql, *a):
    return db.execute(sql, a).fetchall()


def runs(db):
    return q(db, "select run_id, seed, profile, note, sim_seconds, samples_dropped, closed from runs order by run_id")


def clan_series(db, run):
    """The rows of every clan (about 200 points a clan are enough for a chart)."""
    out = defaultdict(list)
    for clan, t, d, tier, food, alive, dead, beds, bld in q(db, "select clan_id,t,day,tier,food_days,alive,dead,beds,buildings from clan_samples where run_id=? order by clan_id,t", run):
        out[clan].append({"t": t, "day": d, "tier": tier, "food_days": round(food, 2), "alive": alive, "dead": dead, "beds": beds, "buildings": bld})
    return {c: rows[::max(1, len(rows) // 200)] for c, rows in out.items()}


def last_rows(db, run):
    res = {}
    for clan, t, tier, food, alive, dead, beds, bld in q(db, "select clan_id,max(t),tier,food_days,alive,dead,beds,buildings from clan_samples where run_id=? group by clan_id", run):
        res[clan] = {"t": t, "tier": tier, "food_days": round(food, 2), "alive": alive, "dead": dead, "beds": beds, "buildings": bld}
    return res


def tiers(db, run):
    res = defaultdict(Counter)
    for clan, tier, n in q(db, "select clan_id, tier, count(*) from clan_samples where run_id=? group by 1,2", run):
        res[clan][tier] = n
    return {c: dict(v) for c, v in res.items()}


def jobs(db, run):
    res = defaultdict(Counter)
    for clan, job, n in q(db, "select clan_id, job, count(*) from samples where run_id=? and alive=1 group by 1,2", run):
        res[clan][job or "-"] = n
    return {c: dict(v) for c, v in res.items()}


def task_results(db, run):
    res = defaultdict(Counter)
    for clan, kind, result, n in q(db, "select clan_id, kind, result, count(*) from tasks where run_id=? group by 1,2,3", run):
        res[clan][f"{kind}:{result}"] = n
    return {c: dict(v) for c, v in res.items()}


def loops(db, run):
    rows = q(db, "select clan_id, member, kind, target, result, count(*) n from tasks where run_id=? and result!='ok' group by 1,2,3,4,5 having n>=? order by n desc limit 15", run, LOOP_MIN)
    return [{"clan": r[0], "member": r[1], "kind": r[2], "target": r[3], "result": r[4], "count": r[5]} for r in rows]


def deaths(db, run):
    rows = q(db, "select clan_id, t, kind, who, other, text from events where run_id=? and kind in ('death','killed','kill') order by t", run)
    return [{"clan": r[0], "t": round(r[1]), "kind": r[2], "who": r[3], "other": r[4], "text": r[5]} for r in rows]


def clan_events(db, run):
    res = defaultdict(dict)
    for c, k, n in q(db, "select clan_id, kind, count(*) from events where run_id=? and kind like 'clan_%' group by 1,2", run):
        res[c][k[5:]] = n
    return dict(res)


def relations(db, run):
    rows = q(db, "select clan_a, clan_b, t, stance, attitude, hostility, grievance_ab, grievance_ba, thefts, raids, trades from clan_relations where run_id=? order by clan_a, clan_b, t", run)
    res = defaultdict(list)
    for a, b, t, st, att, hos, gab, gba, th, ra, tr in rows:
        res[f"{a}-{b}"].append({"t": round(t), "stance": st, "attitude": round(att, 3), "hostility": round(hos, 3), "grievance": round(max(gab, gba), 3), "thefts": th, "raids": ra, "trades": tr})
    return dict(res)


def hunger(db, run):
    """The members that came closest to starving."""
    return [{"clan": c, "member": m, "lowest": round(low, 1), "samples_under_20": n or 0}
            for c, m, low, n in q(db, "select clan_id, member, min(hunger), sum(hunger<20) from samples where run_id=? and hunger>=0 group by 1,2 order by 3 limit 10", run)]


def summarize(db, info):
    rid, seed, profile, note, sim, dropped, closed = info
    final = last_rows(db, rid)
    return {
        "run": rid, "seed": seed, "profile": profile, "note": note, "sim_seconds": sim or max([f["t"] for f in final.values()] or [0]), "samples_dropped": dropped, "closed": bool(closed),
        "clans": len(final), "final": final, "tiers": tiers(db, rid), "jobs": jobs(db, rid), "tasks": task_results(db, rid), "loops": loops(db, rid),
        "deaths": deaths(db, rid), "clan_events": clan_events(db, rid), "hunger": hunger(db, rid),
        "events_total": q(db, "select count(*) from events where run_id=?", rid)[0][0],
    }


def failed(s):
    return sum(v for t in s["tasks"].values() for k, v in t.items() if not k.endswith(":ok"))


def text_report(path, s):
    days = s["sim_seconds"] / 1200.0
    L = [f"== {os.path.basename(path)}  run {s['run']}  seed {s['seed']}  {s['profile']}  {days:.1f} days  {s['clans']} clans" + ("" if s["closed"] else "  (NOT closed)")]
    for c, f in sorted(s["final"].items()):
        t = s["tiers"].get(c, {})
        total = sum(t.values()) or 1
        mix = ", ".join(f"{k} {100 * v // total}%" for k, v in sorted(t.items(), key=lambda kv: -kv[1]))
        L.append(f"  clan {c}: {f['alive']} alive, {f['dead']} dead, food {f['food_days']} d, {f['buildings']} buildings, {f['beds']} beds | tier: {mix}")
    for c, j in sorted(s["jobs"].items()):
        total = sum(j.values()) or 1
        L.append(f"  clan {c} jobs: " + ", ".join(f"{k} {100 * v // total}%" for k, v in sorted(j.items(), key=lambda kv: -kv[1])[:6]))
    bad = Counter()
    for t in s["tasks"].values():
        for k, v in t.items():
            if not k.endswith(":ok"):
                bad[k] += v
    if bad:
        L.append("  failed tasks: " + ", ".join(f"{k} {v}" for k, v in bad.most_common(6)))
    for l in s["loops"][:5]:
        L.append(f"  LOOP clan {l['clan']} {l['member']} {l['kind']} {l['target']} {l['result']} x{l['count']}")
    for c, ev in sorted(s["clan_events"].items()):
        L.append(f"  clan {c} diplomacy: " + ", ".join(f"{k} {v}" for k, v in sorted(ev.items(), key=lambda kv: -kv[1])[:8]))
    for d in s["deaths"][:10]:
        L.append(f"  death t={d['t']} clan {d['clan']}: {d['who']} {d['other']} {d['text']}"[:160])
    if len(s["deaths"]) > 10:
        L.append(f"  ... {len(s['deaths'])} deaths in all")
    return "\n".join(L)


def load(paths):
    out = []
    for p in paths:
        db = sqlite3.connect(f"file:{p}?mode=ro", uri=True)
        for info in runs(db):
            s = summarize(db, info)
            s["file"] = os.path.basename(p)
            s["series"] = clan_series(db, info[0])
            s["relations"] = relations(db, info[0])
            out.append((p, s))
        db.close()
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["report", "export"])
    ap.add_argument("files", nargs="+")
    ap.add_argument("--out", default="analysis")
    a = ap.parse_args()
    data = load(a.files)
    if a.cmd == "report":
        for p, s in data:
            print(text_report(p, s) + "\n")
        if len(data) > 1:
            print("seed  profile      days clans alive dead  food  loops failed")
            for p, s in data:
                alive = sum(f["alive"] for f in s["final"].values())
                dead = sum(f["dead"] for f in s["final"].values())
                food = sum(f["food_days"] for f in s["final"].values()) / max(1, len(s["final"]))
                print(f"{s['seed']:<5} {str(s['profile']):<12} {s['sim_seconds'] / 1200:4.1f} {s['clans']:5} {alive:5} {dead:4} {food:5.1f} {len(s['loops']):5} {failed(s)}")
        return 0
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "dashboard.json"), "w", encoding="utf-8") as f:
        json.dump([s for _, s in data], f)
    with open(os.path.join(a.out, "series.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["file", "run", "seed", "clan", "t", "day", "tier", "food_days", "alive", "dead", "beds", "buildings"])
        for _, s in data:
            for c, rows in s["series"].items():
                for r in rows:
                    w.writerow([s["file"], s["run"], s["seed"], c, round(r["t"]), r["day"], r["tier"], r["food_days"], r["alive"], r["dead"], r["beds"], r["buildings"]])
    print(f"wrote {a.out}/dashboard.json and series.csv ({len(data)} runs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
