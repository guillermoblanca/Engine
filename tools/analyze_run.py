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
    rows = q(db, "select clan_id, t, who, result, text from events where run_id=? and kind='death' order by t", run)
    killers = {(c, w, round(t, 1)): text[len("is killed by "):] for c, t, w, text in q(db, "select clan_id, t, who, text from events where run_id=? and kind='say' and text like 'is killed by %'", run)}
    return [{"clan": r[0], "t": round(r[1]), "kind": r[3], "who": r[2], "other": killers.get((r[0], r[2], round(r[1], 1)), ""), "text": r[4]} for r in rows]


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


def people(db, run):
    """The sheet of everyone (the family tree is mother/father), when they died and what they did most."""
    died = {(c, w): t for c, w, t in q(db, "select clan_id, who, min(t) from events where run_id=? and kind='death' group by 1,2", run)}
    last = {(c, m): (t, age, alive) for c, m, t, age, alive in q(db, "select clan_id, member, max(t), age, alive from samples where run_id=? group by 1,2", run)}
    top = {}
    for c, m, job, n in q(db, "select clan_id, member, job, count(*) n from samples where run_id=? and alive=1 and job not in ('rest','sleep','') group by 1,2,3 order by n", run):
        top[(c, m)] = job
    out = []
    for c, m, role, joined, female, mother, father, gen, traits in q(db, "select clan_id, member, role, joined, female, mother, father, generation, traits from individuals where run_id=? order by clan_id, joined", run):
        t, age, alive = last.get((c, m), (None, None, 1))
        try:
            tr = {k: round(v, 2) for k, v in json.loads(traits or "{}").items()}
        except ValueError:
            tr = {}
        out.append({"clan": c, "name": m, "role": role, "joined": round(joined), "female": bool(female), "mother": mother or "", "father": father or "", "generation": gen,
                    "died": round(died[(c, m)]) if (c, m) in died else None, "alive": bool(alive) and (c, m) not in died, "age": None if age is None else round(age, 1), "main_job": top.get((c, m), ""), "traits": tr})
    return out


def society(db, run):
    """Births, pairs, traditions and norms by clan, with the time of each."""
    res = defaultdict(lambda: defaultdict(list))
    for c, t, k, who, other, text in q(db, "select clan_id, t, kind, who, other, text from events where run_id=? and kind in ('birth','pair','tradition','norm','adult','elder') order by t", run):
        res[c][k].append({"t": round(t), "who": who, "other": other, "text": text[:90]})
    return {c: dict(v) for c, v in res.items()}


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
            s["people"] = people(db, info[0])
            s["society"] = society(db, info[0])
            out.append((p, s))
        db.close()
    return out


def datasets(data, out):
    """Flat row datasets (ds_*.json, an array of objects each) for the dashboard. `game` names a run: the file without extension."""
    def put(name, rows):
        with open(os.path.join(out, f"ds_{name}.json"), "w", encoding="utf-8") as f:
            json.dump(rows, f, separators=(",", ":"))
    runs_, series, people_, rel, loops_, ev, deaths_, jobs_, soc = [], [], [], [], [], [], [], [], []
    for _, s in data:
        g = os.path.splitext(s["file"])[0]
        days = s["sim_seconds"] / 1200.0
        for c, f in s["final"].items():
            ser = s["series"].get(c, [])
            t = s["tiers"].get(c, {})
            tot = sum(t.values()) or 1
            task = s["tasks"].get(c, {})
            bad = sum(v for k, v in task.items() if not k.endswith(":ok"))
            allt = sum(task.values()) or 1
            ppl = [p for p in s["people"] if p["clan"] == c]
            runs_.append({"game": g, "seed": s["seed"], "clans": s["clans"], "clan": c, "days": round(days, 2), "alive": f["alive"], "dead": f["dead"], "food_days": f["food_days"],
                          "buildings": f["buildings"], "beds": f["beds"], "peak": max([r["alive"] for r in ser] or [0]), "born": sum(1 for p in ppl if p["generation"] > 0),
                          "survival_pct": round(100 * t.get("survival", 0) / tot), "growth_pct": round(100 * t.get("growth", 0) / tot),
                          "failed_pct": round(100 * bad / allt), "tasks": sum(task.values()), "kills": sum(1 for d in s["deaths"] if d["clan"] == c)})
            for r in ser:
                series.append({"game": g, "clan": c, "key": f"{g}/{c}", "day": round(r["t"] / 1200.0, 3), "alive": r["alive"], "dead": r["dead"], "food_days": r["food_days"], "tier": r["tier"],
                               "buildings": r["buildings"], "beds": r["beds"]})
            for k, v in s["jobs"].get(c, {}).items():
                jobs_.append({"game": g, "clan": c, "job": k, "samples": v})
        for p in s["people"]:
            people_.append({"game": g, "clan": p["clan"], "id": f"{g}/{p['clan']}/{p['name']}", "name": p["name"], "role": p["role"], "female": p["female"], "generation": p["generation"],
                            "mother": f"{g}/{p['clan']}/{p['mother']}" if p["mother"] else "", "father": f"{g}/{p['clan']}/{p['father']}" if p["father"] else "",
                            "born_day": round(p["joined"] / 1200.0, 2), "died_day": None if p["died"] is None else round(p["died"] / 1200.0, 2), "alive": p["alive"], "age": p["age"], "main_job": p["main_job"]})
        for key, rows in s["relations"].items():
            for r in rows:
                rel.append({"game": g, "pair": key, "day": round(r["t"] / 1200.0, 3), "stance": r["stance"], "attitude": r["attitude"], "hostility": r["hostility"], "grievance": r["grievance"],
                            "thefts": r["thefts"], "raids": r["raids"], "trades": r["trades"]})
        for l in s["loops"]:
            loops_.append({"game": g, **l})
        for c, evs in s["clan_events"].items():
            for k, v in evs.items():
                ev.append({"game": g, "clan": c, "kind": k, "count": v})
        for d in s["deaths"]:
            deaths_.append({"game": g, "clan": d["clan"], "day": round(d["t"] / 1200.0, 2), "kind": d["kind"], "who": d["who"], "other": d["other"], "text": d["text"][:120]})
        for c, kinds in s["society"].items():
            for k, rows in kinds.items():
                for r in rows:
                    soc.append({"game": g, "clan": c, "kind": k, "day": round(r["t"] / 1200.0, 2), "who": r["who"], "text": r["text"]})
    for name, rows in [("runs", runs_), ("series", series), ("people", people_), ("relations", rel), ("loops", loops_), ("clan_events", ev), ("deaths", deaths_), ("jobs", jobs_), ("society", soc)]:
        put(name, rows)


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
    datasets(data, a.out)
    print(f"wrote {a.out}/dashboard.json, series.csv and ds_*.json ({len(data)} runs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
