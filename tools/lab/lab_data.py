"""What the laboratory shows of one run: everything it reads from a telemetry file (.db), as plain JSON, plus an automatic reading of what happened.

Only the standard library. Used by server.py; analyze_run.py has the summaries that both share.
"""
import json
import os
import re
import sqlite3
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import analyze_run as ar  # noqa: E402

DAY = 1200.0
YEAR_DAYS = 4.0          # Life.DaysPerYear
NEEDS = ("hunger", "thirst", "rest", "warmth", "health")
LOW = 30.0               # a need is "covered" while its level is at least this (0..100)


def connect(path):
    db = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    db.row_factory = None
    return db


def q(db, sql, *a):
    try:
        return db.execute(sql, a).fetchall()
    except sqlite3.OperationalError:
        return []   # a file from an older schema: the table is simply not there


def run_info(db):
    rows = q(db, "select run_id, seed, profile, note, sim_seconds, samples_dropped, closed, started from runs order by run_id desc limit 1")
    if not rows:
        return None
    rid, seed, profile, note, sim, dropped, closed, started = rows[0]
    last = q(db, "select max(t) from clan_samples where run_id=?", rid)[0][0] or 0.0
    return {"run": rid, "seed": seed, "profile": profile, "note": note, "sim_seconds": sim or last, "dropped": dropped, "closed": bool(closed), "started": started}


def series(db, rid):
    out = defaultdict(list)
    for clan, t, tier, food, alive, dead, beds, bld, fire in q(db, "select clan_id,t,tier,food_days,alive,dead,beds,buildings,fire from clan_samples where run_id=? order by clan_id,t", rid):
        out[clan].append({"day": round(t / DAY, 4), "tier": tier, "food_days": round(food, 2), "alive": alive, "dead": dead, "beds": beds, "buildings": bld, "fire": fire})
    return {str(c): rows for c, rows in out.items()}


def needs(db, rid):
    """Per clan and per quarter of a day: the share of member samples whose need was covered, and the mean level of each need."""
    res = defaultdict(lambda: defaultdict(lambda: {"n": 0, **{k: 0.0 for k in NEEDS}, **{k + "_ok": 0 for k in NEEDS}}))
    for clan, day, h, t, r, w, hp in q(db, "select clan_id, cast(t/300 as int), hunger, thirst, rest, warmth, health from samples where run_id=? and alive=1", rid):
        b = res[clan][day]
        b["n"] += 1
        for name, v in zip(NEEDS, (h, t, r, w, hp)):
            if v is None or v < 0:
                continue
            b[name] += v
            b[name + "_ok"] += 1 if v >= (50.0 if name == "health" else LOW) else 0
    out = {}
    for clan, days in res.items():
        rows = []
        for day in sorted(days):
            b = days[day]
            n = max(1, b["n"])
            row = {"day": (day + 1) * 0.25}
            for name in NEEDS:
                row[name + "_mean"] = round(b[name] / n, 1)
                row[name + "_covered"] = round(100.0 * b[name + "_ok"] / n, 1)
            rows.append(row)
        out[str(clan)] = rows
    return out


def clan_days(db, rid):
    res = defaultdict(list)
    for clan, t, day, members, bld, techs, beliefs, rites, mood in q(db, "select clan_id,t,day,members,buildings,techs,beliefs,rites,mood from clan_days where run_id=? order by clan_id,t", rid):
        try:
            tl = json.loads(techs or "[]")
        except ValueError:
            tl = []
        res[str(clan)].append({"day": round(t / DAY, 3), "members": members, "buildings": bld, "techs": tl, "beliefs": beliefs, "rites": rites, "mood": round(mood or 0.0, 3)})
    return dict(res)


def tech_timeline(days):
    """For every clan, the day each technology was first seen known."""
    out = {}
    for clan, rows in days.items():
        seen = {}
        for r in rows:
            for t in r["techs"]:
                seen.setdefault(t, r["day"])
        out[clan] = sorted(({"tech": t, "day": d} for t, d in seen.items()), key=lambda x: (x["day"], x["tech"]))
    return out


def building_events(db, rid):
    rows = q(db, "select clan_id, t, who, text from events where run_id=? and kind='say' and (text like '%finishes%' or text like '%builds%' or text like '%built%') order by t limit 400", rid)
    return [{"clan": c, "day": round(t / DAY, 3), "who": w, "text": x[:100]} for c, t, w, x in rows]


def belief_events(db, rid):
    rows = q(db, "select clan_id, t, kind, who, text from events where run_id=? and kind in ('tradition','norm') order by t limit 300", rid)
    extra = q(db, "select clan_id, t, 'say', who, text from events where run_id=? and kind='say' and (text like '%omen%' or text like '%taboo%' or text like '%shaman%' or text like '%rite%' or text like '%funeral%' or text like '%sacred%') order by t limit 300", rid)
    return [{"clan": c, "day": round(t / DAY, 3), "kind": k, "who": w, "text": x[:110]} for c, t, k, w, x in rows + extra]


def relations(db, rid):
    res = defaultdict(list)
    for a, b, t, st, att, hos, tr, gab, gba, enc, trades, gifts, thefts, raids, mar, pacts in q(db, "select clan_a,clan_b,t,stance,attitude,hostility,trust,grievance_ab,grievance_ba,encounters,trades,gifts,thefts,raids,marriages,pacts from clan_relations where run_id=? order by clan_a,clan_b,t", rid):
        res[f"{a}-{b}"].append({"day": round(t / DAY, 3), "stance": st, "attitude": round(att, 3), "hostility": round(hos, 3), "trust": round(tr, 3), "grievance": round(max(gab, gba), 3),
                                "encounters": enc, "trades": trades, "gifts": gifts, "thefts": thefts, "raids": raids, "marriages": mar, "pacts": pacts})
    return dict(res)


def diplomacy_events(db, rid):
    rows = q(db, "select clan_id, t, kind, who, other, text from events where run_id=? and kind like 'clan_%' order by t limit 1500", rid)
    return [{"clan": c, "day": round(t / DAY, 3), "kind": k[5:], "text": x[:140]} for c, t, k, w, o, x in rows]


def member_bonds(db, rid):
    rows = q(db, "select clan_id, max(t), a, b, kin, bond, affinity from relations where run_id=? group by clan_id,a,b order by clan_id, abs(bond) desc limit 400", rid)
    return [{"clan": c, "a": a, "b": b, "kin": k, "bond": round(bo, 3), "affinity": round(af, 3)} for c, _, a, b, k, bo, af in rows]


def world(db, rid):
    rows = q(db, "select t, kind, count, empty, amount from world_nodes where run_id=? order by t, kind", rid)
    first = {}
    last = {}
    over = defaultdict(list)
    for t, kind, count, empty, amount in rows:
        first.setdefault(kind, {"count": count, "amount": round(amount, 1)})
        last[kind] = {"count": count, "empty": empty, "amount": round(amount, 1)}
        over[kind].append({"day": round(t / DAY, 3), "count": count, "empty": empty, "amount": round(amount, 1)})
    return {"initial": first, "final": last, "over_time": over,
            "fauna": "not simulated: GBSim has no ecology system yet (the editor does); flora and resources only"}


def stats_rows(db, rid):
    final = ar.last_rows(db, rid)
    tiers = ar.tiers(db, rid)
    return {"final": {str(k): v for k, v in final.items()}, "tiers": {str(k): v for k, v in tiers.items()}, "jobs": {str(k): v for k, v in ar.jobs(db, rid).items()},
            "tasks": {str(k): v for k, v in ar.task_results(db, rid).items()}}


def decisions_summary(db, rid):
    rows = q(db, "select clan_id, chosen, count(*) from decisions where run_id=? group by 1,2 order by 3 desc", rid)
    out = defaultdict(dict)
    for c, k, n in rows:
        out[str(c)][k] = n
    return dict(out)


# ---------------------------------------------------------------------------------------------------------------------------------------------- the reading
def narrative(info, data):
    """Sentences about what happened in the run, each with the numbers it rests on. Heuristics, not truth: they point at where to look."""
    out = []
    days = info["sim_seconds"] / DAY

    def add(level, title, text):
        out.append({"level": level, "title": title, "text": text})

    series_ = data["series"]
    n_clans = len(series_)
    add("info", "La partida", f"{days:.1f} días simulados ({days / YEAR_DAYS:.1f} años), {n_clans} clan(es), semilla {info['seed']}.")
    for clan, rows in sorted(series_.items()):
        if not rows:
            continue
        alive = [r["alive"] for r in rows]
        peak, end, first = max(alive), alive[-1], alive[0]
        peak_day = rows[alive.index(peak)]["day"]
        if end == 0:
            died = next((r["day"] for r in rows if r["alive"] == 0), rows[-1]["day"])
            add("bad", f"Clan {clan} se extinguió", f"Pasó de {first} a 0 miembros; murió del todo el día {died:.1f}. Pico de {peak} el día {peak_day:.1f}.")
        elif peak <= first:
            add("warn", f"Clan {clan} no crece", f"Empezó con {first} y acaba con {end} (pico {peak}): no ha superado su población inicial en {days:.1f} días.")
        else:
            add("ok" if end >= peak else "info", f"Clan {clan}: {first} → {end}", f"Pico de {peak} miembros el día {peak_day:.1f}; {rows[-1]['dead']} muertos acumulados.")

    # why does a clan not grow: the conditions of a birth (Society.cpp: food per member, a fire, a couple, the spacing of two years)
    for clan, rows in sorted(series_.items()):
        if not rows or rows[-1]["alive"] == 0:
            continue
        births = sum(1 for p in data["people"] if str(p["clan"]) == clan and p["generation"] > 0)
        pairs = [e for e in data["society"].get(clan, []) if e["kind"] == "pair"]
        fire = sum(1 for r in rows if r["fire"]) / len(rows)
        food = sum(r["food_days"] for r in rows) / len(rows)
        if days < 2 * YEAR_DAYS and births:
            add("info", f"Clan {clan}: pocos nacimientos por la duración",
                f"{births} nacimientos con {len(pairs)} parejas. Una madre espera 2 años entre hijos ({2 * YEAR_DAYS:.0f} días simulados): con solo {days:.1f} días cada pareja puede tener un hijo como mucho. No es un tope, es el reloj.")
        elif len(pairs) == 0:
            add("warn", f"Clan {clan}: sin parejas", "Nadie ha formado pareja: no hay nacimientos posibles (adultos de 16+, de sexo distinto, sin parentesco y con afinidad mínima).")
        if fire < 0.5:
            add("warn", f"Clan {clan}: sin hoguera casi todo el tiempo", f"Hoguera encendida el {fire * 100:.0f}% de las muestras; sin ella no hay nacimientos (NeedsFire).")
        if food < 1.5:
            add("warn", f"Clan {clan}: poca comida", f"Reserva media de {food:.1f} días de comida; los nacimientos piden 1,5 unidades por miembro en el almacén.")

    # deaths
    causes = Counter((d["kind"] or "?") for d in data["deaths"])
    if causes:
        add("info", "Muertes", ", ".join(f"{k}: {v}" for k, v in causes.most_common()) + f" ({len(data['deaths'])} en total).")
    killers = Counter(d["other"] for d in data["deaths"] if d["other"])
    if killers:
        add("bad", "Violencia entre clanes", f"{sum(killers.values())} muertes a manos de otros miembros; los que más: " + ", ".join(f"{k} ({v})" for k, v in killers.most_common(3)) + ".")

    # loops
    for l in data["loops"][:3]:
        add("bad", "Bucle de tareas", f"{l['member']} (clan {l['clan']}) falló {l['count']} veces «{l['kind']}» sobre «{l['target'] or '—'}» ({l['result']}): la IA se queda reintentando lo mismo.")

    # needs
    for clan, rows in sorted(data["needs"].items()):
        if not rows:
            continue
        worst = min(NEEDS, key=lambda n: sum(r[n + "_covered"] for r in rows) / len(rows))
        cover = sum(r[worst + "_covered"] for r in rows) / len(rows)
        if cover < 90:
            add("warn", f"Clan {clan}: necesidad peor cubierta", f"«{worst}» está cubierta el {cover:.0f}% del tiempo (nivel ≥ {LOW:.0f}).")

    # war and diplomacy
    ev = Counter(e["kind"] for e in data["diplomacy_events"])
    if ev:
        add("info", "Diplomacia", ", ".join(f"{k}: {v}" for k, v in ev.most_common(8)) + ".")
    if ev.get("raid", 0) + ev.get("theft", 0) > 0 and ev.get("trade", 0) + ev.get("gift", 0) == 0:
        add("warn", "Solo conflicto", "Hubo robos o incursiones y ningún comercio ni regalo entre clanes: la relación solo se deteriora.")

    # progression
    for clan, tl in sorted(data["tech_timeline"].items()):
        add("info", f"Clan {clan}: tecnologías", f"{len(tl)} conocidas" + (": " + ", ".join(t["tech"] for t in tl[:8]) if tl else "") + ("." if len(tl) <= 8 else "…"))
    for clan, rows in sorted(series_.items()):
        if rows and rows[-1]["buildings"] == 0 and days >= 3:
            add("warn", f"Clan {clan}: sin edificios", f"Ninguna construcción terminada en {days:.1f} días.")

    # resources
    w = data["world"]
    for kind, last in sorted(w["final"].items(), key=lambda kv: -kv[1].get("empty", 0))[:2]:
        if last.get("empty", 0) > 0 and w["initial"].get(kind):
            add("info", "Recursos agotados", f"«{kind}»: {last['empty']} de {last['count']} nodos vacíos al final (inicio: {w['initial'][kind]['count']}).")
    return out


def analyze(path):
    db = connect(path)
    try:
        info = run_info(db)
        if not info:
            return {"error": "the file has no run yet"}
        rid = info["run"]
        d = {"info": info}
        d["series"] = series(db, rid)
        d["needs"] = needs(db, rid)
        d["clan_days"] = clan_days(db, rid)
        d["tech_timeline"] = tech_timeline(d["clan_days"])
        d["buildings"] = building_events(db, rid)
        d["beliefs"] = belief_events(db, rid)
        d["relations"] = relations(db, rid)
        d["diplomacy_events"] = diplomacy_events(db, rid)
        d["bonds"] = member_bonds(db, rid)
        d["world"] = world(db, rid)
        d["stats"] = stats_rows(db, rid)
        d["decisions"] = decisions_summary(db, rid)
        d["people"] = ar.people(db, rid)
        d["society"] = {str(k): [dict(kind=kind, **r) for kind, rows in v.items() for r in rows] for k, v in ar.society(db, rid).items()}
        d["deaths"] = ar.deaths(db, rid)
        d["loops"] = ar.loops(db, rid)
        d["hunger"] = ar.hunger(db, rid)
        d["narrative"] = narrative(info, d)
        return d
    finally:
        db.close()


def summary(path):
    """One line for the list of runs."""
    db = connect(path)
    try:
        info = run_info(db)
        if not info:
            return None
        rid = info["run"]
        final = ar.last_rows(db, rid)
        return {"seed": info["seed"], "profile": info["profile"], "days": round(info["sim_seconds"] / DAY, 2), "closed": info["closed"], "clans": len(final),
                "alive": sum(f["alive"] for f in final.values()), "dead": sum(f["dead"] for f in final.values())}
    except Exception as e:  # a file being written may not be readable yet
        return {"error": str(e)}
    finally:
        db.close()


def compare(paths):
    """The series of several runs side by side: for each, the alive members of every clan by day."""
    out = {}
    for name, path in paths.items():
        db = connect(path)
        try:
            info = run_info(db)
            if info:
                out[name] = {"info": info, "series": series(db, info["run"]), "needs": needs(db, info["run"])}
        finally:
            db.close()
    return out
