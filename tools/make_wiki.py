#!/usr/bin/env python3
"""Builds the project wiki: one self-contained HTML page (docs/wiki/index.html) from

  * docs/wiki/data/rules.json   what the rules say about themselves (editor command `docs export docs/wiki/data/rules.json`)
  * assets/data/*.json          items, recipes and resource nodes
  * docs/*.md                   the written documents

Usage:  python tools/make_wiki.py [--out docs/wiki/index.html]

The page needs no server and no network: open it in a browser. Regenerate it after changing the rules or the documents
(`tools/make_wiki.cmd` or the commands in docs/wiki/README.md do the whole sequence).
"""
import argparse
import html
import json
import re
import sys
from pathlib import Path

try:
    import markdown
except ImportError:  # pragma: no cover
    sys.exit('This script needs the "markdown" package: pip install markdown')

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / 'docs'

# documents shown in the wiki, in order: (file, group)
DOC_LIST = [
    ('PLAN_SOCIEDAD_VIVA.md', 'Plan'),
    ('PROPUESTA_HABILIDADES_Y_ECONOMIA.md', 'Plan'),
    ('INFORME_CALIDAD.md', 'Plan'),
    ('TRIBU.md', 'Sistemas'),
    ('IA.md', 'Sistemas'),
    ('NAVEGACION.md', 'Sistemas'),
    ('OBJETOS.md', 'Sistemas'),
    ('INTERACTIVOS.md', 'Sistemas'),
    ('ECOLOGIA.md', 'Sistemas'),
    ('GUARDADO.md', 'Sistemas'),
    ('PAQUETES.md', 'Sistemas'),
    ('CONOCIMIENTO.md', 'Sistemas'),
    ('EXPLORACION.md', 'Sistemas'),
    ('RECURSOS_MUNDO.md', 'Sistemas'),
    ('CAMARAS.md', 'Sistemas'),
    ('habilidades.md', 'Sistemas'),
    ('COSTE_ACTOR.md', 'Sistemas'),
    ('ANIMACIONES_PENDIENTES.md', 'Animación'),
    ('ANIMACION_LOCOMOCION.md', 'Animación'),
    ('ANIMACION_SISTEMA_COMPLETO.md', 'Animación'),
    ('AUDITORIA_ANIMACION.md', 'Animación'),
    ('modelos-y-animacion.md', 'Animación'),
    ('editor.md', 'Motor'),
    ('render.md', 'Motor'),
    ('mundo.md', 'Motor'),
    ('efectos.md', 'Motor'),
    ('fisica.md', 'Motor'),
    ('audio.md', 'Motor'),
    ('qa.md', 'Motor'),
]

e = html.escape

# Spanish text for the notes that live in the C++ ability definitions (in English there); unknown ones are shown as they are.
NOTES_ES = {
    'Chop': 'Talar un árbol (necesita un hacha).',
    'Mine': 'Picar una roca (necesita un pico).',
    'Gather': 'Recoger bayas a mano.',
    'PullFiber': 'Arrancar fibra de una mata de hierba.',
    'Dig': 'Cavar una parcela de hierba.',
    'Sow': 'Sembrar semillas en una parcela cavada.',
    'Water': 'Regar una parcela sembrada o un árbol joven.',
    'PlantTree': 'Plantar un árbol joven en el hueco de los árboles.',
    'PickFruit': 'Coger fruta de un árbol crecido (tres animaciones por turno).',
    'Pull': 'Arrancar una planta crecida de su parcela.',
    'Milk': 'Ordeñar la vaca.',
    'CarryBox': 'Coger una caja y llevarla en las manos.',
    'DropBox': 'Dejar la caja que lleva.',
    'PushWheelbarrow': 'Agarrar la carretilla y empujarla.',
    'ReleaseWheelbarrow': 'Soltar la carretilla.',
    'DumpWheelbarrow': 'Vaciar la carretilla en el compostador.',
    'Stoke': 'Echar combustible a una fogata (necesita madera o ramas en la mochila).',
    'DrawWater': 'Llenar la mochila con agua de un pozo.',
    'OpenDoor': 'Abrir una puerta.',
    'CloseDoor': 'Cerrar una puerta.',
    'Collect': 'Recoger arcilla o junco a mano.',
}



def load_json(path, default=None):
    try:
        return json.loads(Path(path).read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return default


def slug(text):
    return re.sub(r'[^a-z0-9]+', '-', text.lower()).strip('-')


def tags(items, cls='tag'):
    return ' '.join(f'<code class="{cls}">{e(t)}</code>' for t in items) if items else '<span class="none">—</span>'


def num(v):
    if v is None:
        return '∞'
    if isinstance(v, float) and v.is_integer():
        return str(int(v))
    return str(v)


# ---------------------------------------------------------------- documents
def render_doc(path, known):
    text = path.read_text(encoding='utf-8')
    text = re.sub(r'!\[([^\]]*)\]\([^)]*\)', r'*[imagen: \1]*', text)  # the wiki is text only
    body = markdown.markdown(text, extensions=['tables', 'fenced_code', 'sane_lists'])

    def link(m):
        target = m.group(1)
        name = Path(target.split('#')[0]).name
        return f'href="#doc-{slug(name)}"' if name in known else f'href="{target}"'
    body = re.sub(r'href="([^"]+\.md(?:#[^"]*)?)"', link, body)
    return body


# ---------------------------------------------------------------- sections
def section_home(rules, content, docs_status):
    nab = len(rules.get('abilities', []))
    nef = len(rules.get('effects', []))
    nit = len(content['items'])
    nre = len(content['recipes'])
    nno = len(content['nodes'])
    cards = [('Habilidades', nab), ('Efectos', nef), ('Objetos', nit), ('Recetas', nre), ('Tipos de nodo', nno), ('Tipos de entidad', len(rules.get('entityKinds', [])))]
    out = ['<h1>GB Engine — wiki del mundo vivo</h1>',
           '<p class="lead">Lo que el motor hace hoy, generado desde las reglas reales (habilidades, efectos, atributos, necesidades, roles, estaciones) y desde los datos de contenido, junto a los documentos del proyecto. '
           'Usa el buscador de arriba para encontrar cualquier habilidad, atributo, etiqueta u objeto.</p>',
           '<div class="cards">' + ''.join(f'<div class="card"><b>{n}</b><span>{e(t)}</span></div>' for t, n in cards) + '</div>',
           '<h2>Cómo se actualiza</h2>',
           '<ol><li>En el editor, con las demos construidas: <code>docs export docs/wiki/data/rules.json</code> (o <code>tools/make_wiki.cmd</code>, que lo hace sin ventana).</li>'
           '<li><code>python tools/make_wiki.py</code> reconstruye <code>docs/wiki/index.html</code>.</li></ol>',
           '<h2>Mapa</h2><ul>'
           '<li><a href="#habilidades">Habilidades</a>: el árbol de lo que un personaje puede hacer, con requisitos, costes, enfriamientos y animaciones.</li>'
           '<li><a href="#efectos">Efectos</a> y <a href="#etiquetas">etiquetas</a>: cómo cambian los atributos y el estado.</li>'
           '<li><a href="#atributos">Atributos</a> por tipo de entidad y <a href="#necesidades">necesidades</a> de los personajes.</li>'
           '<li><a href="#mente">Sentidos, memoria y decisión</a> y <a href="#roles">roles</a> de la tribu.</li>'
           '<li><a href="#mundo">Estaciones, ecología e interactivos</a>.</li>'
           '<li><a href="#contenido">Objetos, recetas y nodos de recurso</a> (datos).</li>'
           '<li>Los <a href="#docs">documentos</a> del proyecto (plan, propuesta de economía, informe de calidad, sistemas).</li></ul>']
    if docs_status:
        out.append('<h2>Estado de los hitos</h2>' + docs_status)
    return '\n'.join(out)


def ability_card(a):
    a = dict(a)
    a['note'] = NOTES_ES.get(a['name'], a['note'])
    rows = []

    def row(label, value):
        rows.append(f'<tr><th>{e(label)}</th><td>{value}</td></tr>')
    if a['note']:
        row('Qué hace', e(a['note']))
    row('Etiqueta', f'<code>{e(a["tag"])}</code>')
    if a['requiredTags'] or a['requiredProp']:
        req = tags(a['requiredTags'])
        if a['requiredProp']:
            req += f' · lleva <code>{e(a["requiredProp"])}</code>'
        row('Quien la usa necesita', req)
    if a['blockedTags']:
        row('No puede si tiene', tags(a['blockedTags'], 'tag bad'))
    if a['needsTarget']:
        tr = tags(a['targetRequiredTags'])
        if a['targetRequiredAnyTags']:
            tr += ' · alguna de ' + tags(a['targetRequiredAnyTags'])
        for m in a['targetMinAttributes']:
            tr += f' · <code>{e(m["attribute"])} ≥ {num(m["value"])}</code>'
        row('El objetivo debe tener', tr)
        if a['targetBlockedTags']:
            row('El objetivo no puede tener', tags(a['targetBlockedTags'], 'tag bad'))
        row('Se desplaza', 'sí, hasta el punto de interacción' + (f' <code>{e(a["point"])}</code>' if a['point'] else '') if a['moveToTarget'] else 'no')
    if a['cost']:
        row('Coste', f'<a href="#fx-{slug(a["cost"])}">{e(a["cost"])}</a>')
    if a['cooldownSeconds']:
        row('Enfriamiento', f'{num(a["cooldownSeconds"])} s')
    if a['montages']:
        row('Animación', ' → '.join(f'<code>{e(m)}</code>' for m in a['montages']) + ' <span class="none">(una por uso, en orden)</span>' if len(a['montages']) > 1 else f'<code>{e(a["montages"][0])}</code>')
    row('Duración sin animación', f'{num(a["durationSeconds"])} s')
    for ev in a['events']:
        effs = ''
        if ev['selfEffects']:
            effs += ' · a quien la usa: ' + ', '.join(f'<a href="#fx-{slug(x)}">{e(x)}</a>' for x in ev['selfEffects'])
        if ev['targetEffects']:
            effs += ' · al objetivo: ' + ', '.join(f'<a href="#fx-{slug(x)}">{e(x)}</a>' for x in ev['targetEffects'])
        row('Evento', f'<code>{e(ev["eventTag"])}</code> al {int(ev["time"] * 100)} % del montaje (aviso <code>{e(ev["notify"])}</code>){effs}')
    if a['activeTags']:
        row('Mientras dura da', tags(a['activeTags']))
    for k, label in (('activationEffects', 'Al empezar aplica'), ('removeEffects', 'Al empezar quita'), ('endEffects', 'Al acabar bien aplica')):
        if a[k]:
            row(label, ', '.join(f'<a href="#fx-{slug(x)}">{e(x)}</a>' for x in a[k]))
    if a['equipProp']:
        row('Objeto en la mano', f'<code>{e(a["equipProp"])}</code>' + (' (lo conserva)' if a['keepProp'] else ' (lo suelta al acabar)'))
    if a['waitEvent']:
        row('Espera el evento', f'<code>{e(a["waitEvent"])}</code>')
    return (f'<details class="ability" id="ab-{slug(a["name"])}" data-search="{e(a["name"].lower())} {e(a["note"].lower())} {e(a["tag"].lower())}">'
            f'<summary><b>{e(a["name"])}</b> <span class="muted">{e(a["note"])}</span></summary>'
            f'<table class="kv">{"".join(rows)}</table></details>')


def section_abilities(rules):
    domains = {}
    for a in rules['abilities']:
        domains.setdefault(a['domain'], []).append(a)
    out = ['<h1>Habilidades</h1>',
           '<p class="lead">Árbol de lo que un personaje puede hacer, agrupado por lo que toca. Cada habilidad es la misma para el jugador y para un NPC: comprueba etiquetas (quién la usa y su objetivo), '
           'paga un coste, camina hasta el punto de interacción, reproduce un montaje y, en su aviso, aplica efectos y lanza un evento al que reaccionan los objetos del mundo.</p>',
           '<p><b>Cómo leerla:</b> «necesita» son etiquetas que debe tener quien la usa (p. ej. <code>Item.Tool.Axe</code> sale de llevar un hacha en la mochila); '
           '«el objetivo debe tener» son etiquetas y atributos del nodo, parcela o interactivo sobre el que actúa.</p>']
    order = ['Recursos', 'Huerto', 'Interactivos', 'Transporte', 'Otras']
    for d in sorted(domains, key=lambda x: order.index(x) if x in order else 99):
        lst = sorted(domains[d], key=lambda a: a['name'])
        out.append(f'<h2>{e(d)} <span class="muted">({len(lst)})</span></h2>')
        out.extend(ability_card(a) for a in lst)
    return '\n'.join(out)


def section_effects(rules):
    out = ['<h1>Efectos</h1>',
           '<p class="lead">Un efecto cambia atributos y concede o quita etiquetas. <b>Instantáneo</b>: cambia el valor base para siempre. <b>Con duración</b> o <b>infinito</b>: sus modificadores cuentan mientras dura '
           '(y, con periodo, se aplican como cambios instantáneos cada cierto tiempo: fruta que rebrota, energía que se recupera).</p>',
           '<table class="grid"><tr><th>Efecto</th><th>Tipo</th><th>Modifica</th><th>Etiquetas</th></tr>']
    for fx in sorted(rules['effects'], key=lambda x: x['name']):
        mods = ', '.join(f'<code>{e(m["attribute"])} {"+" if m["op"] == "add" and m["magnitude"] >= 0 else ""}{"×" if m["op"] == "multiply" else ("=" if m["op"] == "override" else "")}{num(m["magnitude"])}</code>' for m in fx['modifiers']) or '<span class="none">—</span>'
        kind = {'instant': 'instantáneo', 'duration': f'dura {num(fx["durationSeconds"])} s', 'infinite': 'infinito'}[fx['policy']]
        if fx['periodSeconds']:
            kind += f', cada {num(fx["periodSeconds"])} s'
        tg = ''
        if fx['grantedTags']:
            tg += 'da ' + tags(fx['grantedTags'])
        if fx['removedTags']:
            tg += ' quita ' + tags(fx['removedTags'], 'tag bad')
        out.append(f'<tr id="fx-{slug(fx["name"])}" data-search="{e(fx["name"].lower())}"><td><b>{e(fx["name"])}</b></td><td>{kind}</td><td>{mods}</td><td>{tg or "<span class=none>—</span>"}</td></tr>')
    out.append('</table>')
    return '\n'.join(out)


def section_attributes(rules):
    out = ['<h1>Atributos y etiquetas por tipo de entidad</h1>',
           '<p class="lead">Todo lo que tiene estado en el mundo es una entidad con un sistema de habilidades: sus <b>atributos</b> son números con mínimo y máximo, y sus <b>etiquetas</b> son su estado en texto. '
           'Se guardan y se cargan con la partida. Leído de una escena real con todas las demos construidas.</p>']
    for k in rules['entityKinds']:
        rows = ''.join(f'<tr><td><code>{e(a["name"])}</code></td><td>{num(a["base"])}</td><td>{num(a["min"])}</td><td>{num(a["max"])}</td></tr>' for a in k['attributes'])
        out.append(f'<details class="kind" open data-search="{e(k["kind"].lower())} {" ".join(a["name"].lower() for a in k["attributes"])}">'
                   f'<summary><b>{e(k["kind"])}</b> <span class="muted">{k["count"]} en la escena de ejemplo (p. ej. «{e(k["example"])}»)</span></summary>'
                   + (f'<table class="grid"><tr><th>Atributo</th><th>Valor inicial</th><th>Mínimo</th><th>Máximo</th></tr>{rows}</table>' if rows else '<p class="none">Sin atributos.</p>')
                   + f'<p><b>Etiquetas:</b> {tags(k["tags"])}</p><p><b>Habilidades concedidas:</b> '
                   + (' '.join(f'<a href="#ab-{slug(x)}">{e(x)}</a>' for x in k['abilities']) or '<span class="none">—</span>') + '</p></details>')
    return '\n'.join(out)


NEED_TEXT = {
    'Hunger': ('Hambre', 'Baja con el tiempo (más rápido en invierno); sube al comer. Con 0 de hambre se pierde salud.'),
    'Thirst': ('Sed', 'Baja con el tiempo; sube al beber en el pozo. Con 0 se pierde salud.'),
    'Rest': ('Descanso', 'Baja despierto; sube durmiendo (de noche, en el campamento).'),
    'Warmth': ('Abrigo', 'Baja con el frío del invierno y la noche; sube junto a una hoguera encendida.'),
    'Safety': ('Seguridad', 'Baja con los gritos y el peligro; se recupera estando a salvo y acompañado.'),
    'Company': ('Compañía', 'Baja en soledad; sube junto a otros miembros del clan.'),
}


def section_needs(rules):
    n = rules['needs']
    c = n['config']
    out = ['<h1>Necesidades</h1>',
           '<p class="lead">Cada personaje tiene seis necesidades de 0 a 100 (100 = satisfecha) como atributos de su sistema de habilidades. Con el tiempo bajan, al actuar suben, y cuando una llega a 0 se pierde <b>Salud</b>; '
           'con 0 de salud muere. Las necesidades bajas también le quitan <b>Vigor</b> (velocidad y rendimiento) mediante los efectos <code>Need.*</code>.</p>',
           '<table class="grid"><tr><th>Necesidad</th><th>Atributo</th><th>Cómo se mueve</th><th>Ritmo de caída</th></tr>']
    rate = {
        'Hunger': f'de 100 a 0 en {num(c["hungerSeconds"])} s despierto en primavera',
        'Thirst': f'de 100 a 0 en {num(c["thirstSeconds"])} s',
        'Rest': f'de 100 a 0 en {num(c["restSeconds"])} s despierto; dormir da +{num(c["sleepGain"])}/s',
        'Warmth': f'junto a un fuego encendido +{num(c["warmGain"])}/s; el frío la baja',
        'Safety': 'según gritos y peligros cercanos',
        'Company': 'según la gente cerca',
    }
    for key in n['list']:
        es, text = NEED_TEXT.get(key, (key, ''))
        out.append(f'<tr data-search="{e(es.lower())} {e(key.lower())}"><td><b>{e(es)}</b></td><td><code>{e(key)}</code></td><td>{e(text)}</td><td>{e(rate.get(key, ""))}</td></tr>')
    out.append('</table>')
    out.append('<h2>Salud, vigor y muerte</h2><ul>'
               f'<li><b>Salud</b> (<code>Health</code>): baja {num(c["starveDamage"])}/s con hambre a 0, {num(c["thirstDamage"])}/s con sed a 0 y {num(c["coldDamage"])}/s con frío; se recupera {num(c["healPerSecond"])}/s con las necesidades cubiertas. A 0 muere (<code>State.Dead</code>, <code>Death.Hunger</code>…).</li>'
               '<li><b>Vigor</b> (<code>Vigor</code>, 1 = pleno): lo escalan los efectos <code>Need.Hungry</code>, <code>Need.Thirsty</code>, <code>Need.Tired</code>, <code>Need.Cold</code>, <code>Need.Afraid</code>, <code>Need.Lonely</code> y <code>Need.Starving</code>.</li>'
               f'<li><b>Comer y beber:</b> una baya devuelve {c["foodHunger"]} de hambre y {c["foodThirst"]} de sed; una unidad de agua, {c["waterThirst"]} de sed.</li></ul>')
    out.append('<h2>Día y noche</h2>'
               f'<p>El día dura {num(c["daySeconds"])} s de simulación y empieza a las {num(c["startHour"])} h. Es de noche de las {num(c["nightFrom"])} h a las {num(c["nightTo"])} h: se duerme.</p>')
    fx = [f for f in rules['effects'] if f['name'].startswith('Need.')]
    if fx:
        out.append('<h2>Efectos de las necesidades</h2><ul>' + ''.join(f'<li><a href="#fx-{slug(f["name"])}">{e(f["name"])}</a></li>' for f in fx) + '</ul>')
    return '\n'.join(out)


def section_mind(rules):
    s = rules['senses']
    d = rules['decision']
    v = s['vision']
    m = s['memory']
    return '\n'.join(['<h1>Sentidos, memoria y decisión</h1>',
        '<p class="lead">Cada trabajador es un agente: <b>ve</b>, <b>oye</b>, <b>recuerda</b> lo que aprendió y cada pocos segundos <b>puntúa</b> lo que podría hacer según sus necesidades, su rol y lo que recuerda (IA de utilidad).</p>',
        '<h2>Vista y oído</h2>',
        f'<ul><li>Cono de visión: hasta <b>{num(v["rangeMeters"])} m</b>, {num(v["halfFovDegrees"] * 2)}° de campo total; lo que esté a menos de <b>{num(v["nearRangeMeters"])} m</b> se nota siempre. Los troncos y rocas tapan la vista.</li>'
        '<li>Oído: eventos con radio (golpes de hacha, pasos, gritos); el que los oye sabe de dónde vienen y, si es un grito, se siente menos seguro.</li></ul>',
        '<h2>Memoria espacial</h2>',
        f'<ul><li>Cada agente recuerda qué recursos conoce, dónde están y cuándo los vio; olvida a los <b>{num(m["lifetimeSeconds"])} s</b>.</li>'
        f'<li>Un nodo visto vacío se vuelve a mirar pasados <b>{num(m["recheckSeconds"])} s</b>. En el campamento los miembros comparten lo que saben con la memoria del clan.</li></ul>',
        '<h2>Decisión</h2>',
        f'<ul><li>Una decisión se mantiene al menos <b>{num(d["minCommitSeconds"])} s</b> salvo que otra supere a la actual en <b>{num(d["margin"])}</b> puntos (más un bonus de <b>{num(d["stickBonus"])}</b> para lo que ya hace).</li>'
        f'<li>Si una opción llega a <b>{num(d["criticalScore"])}</b> o más (urgencia: hambre, sed, frío) pasa por delante de inmediato.</li>'
        '<li>El inspector del editor (panel Tribe → miembro seleccionado) muestra cada opción con su puntuación y sus factores: por qué hace lo que hace.</li></ul>'])


def section_roles(rules):
    roles = rules['roles']
    kinds = sorted({k for w in roles.values() for k in w})
    out = ['<h1>Roles de la tribu</h1>',
           '<p class="lead">Un rol son <b>pesos</b> sobre las opciones de trabajo (1 = le encanta, 0 = no lo hace por gusto), por id de nodo o por tipo (<code>Resource.Stone</code> vale para el sílex y el cobre); '
           '<code>default</code> es lo que da a lo que no nombra. Están en la sección <code>roles</code> de <code>era1.json</code>. Sobre ellos se suman las aptitudes por práctica de cada miembro, '
           'y el clan reparte los oficios según lo que necesita (ver TRIBU.md). Cualquiera ayuda con lo que falta y come, bebe y duerme cuando debe.</p>',
           '<table class="grid heat"><tr><th>Rol</th>' + ''.join(f'<th>{e(k)}</th>' for k in kinds) + '</tr>']
    for role, w in roles.items():
        cells = ''.join(f'<td style="--w:{w.get(k, 0)}">{num(w.get(k, 0))}</td>' for k in kinds)
        out.append(f'<tr><td><b>{e(role)}</b></td>{cells}</tr>')
    out.append('</table>')
    return '\n'.join(out)


def section_world(rules):
    s = rules['seasons']
    out = ['<h1>Estaciones, ecología e interactivos</h1>',
           f'<h2>Estaciones</h2><p>Cada estación dura {num(s["seasonSeconds"])} s; salen del reloj de simulación, así que no se guardan aparte.</p>',
           '<table class="grid"><tr><th>Estación</th><th>Crecimiento y cría</th><th>Hambre</th><th>Nieve (inicio → fin)</th><th>Clima</th><th>Color del follaje</th></tr>']
    for se in s['list']:
        r, g, b = [int(max(0, min(1, x)) * 255) for x in se['foliage']]
        out.append(f'<tr><td><b>{e(se["name"])}</b></td><td>×{num(se["growthRate"])}</td><td>×{num(se["hungerRate"])}</td>'
                   f'<td>{num(se["snowAtStart"])} → {num(se["snowAtEnd"])}</td><td>{e(se["weatherAtStart"])} / {e(se["weatherAtMiddle"])} / {e(se["weatherAtEnd"])}</td>'
                   f'<td><span class="swatch" style="background:rgb({r},{g},{b})"></span></td></tr>')
    out.append('</table>')
    eco = rules['ecology']
    out.append('<h2>Ecología: conejos</h2><p>Las demás especies (el zorro, que caza conejos) están en <a href="#datos">Más datos del juego</a> → especies.</p><table class="grid"><tr><th>Ajuste</th><th>Valor</th></tr>'
               + ''.join(f'<tr><td><code>{e(k)}</code></td><td>{num(v)}</td></tr>' for k, v in eco.items()) + '</table>')
    it = rules['interactives']
    out.append('<h2>Interactivos</h2><ul>'
               f'<li><b>Fogata</b>: 1 de combustible dura {num(it["burnSeconds"])} s; 1 de madera da {it["fuelPerWood"]} (la rama, menos).</li>'
               f'<li><b>Pozo</b>: cada uso da {it["waterPerDraw"]} de agua y recupera 1 cada 20 s.</li><li><b>Puerta</b>: abierta o cerrada; la hoja gira sobre su bisagra.</li></ul>')
    g = rules['garden']
    out.append(f'<h2>Huerto</h2><p>Una planta regada pasa de semilla a brote y de brote a crecida en {num(g["stageSeconds"])} s; un riego mantiene la tierra húmeda {num(g["moistureSeconds"])} s; un árbol joven regado tarda {num(g["treeSeconds"])} s.</p>')
    return '\n'.join(out)


def section_content(content, extra):
    out = ['<h1>Objetos, recetas y nodos de recurso</h1>',
           '<p class="lead">Datos de <code>assets/data/era1.json</code> (la base) y de <code>era1_ampliado.json</code> (propuesta de economía; se carga con <code>content load</code>). Cambiarlos no requiere compilar.</p>']

    def items_table(items, title):
        rows = ''
        for it in items:
            extras = []
            for k, label in (('power', 'potencia'), ('speed', 'velocidad'), ('fuel', 'combustible'), ('bagSlots', 'casillas'), ('bagWeight', 'kg')):
                if k in it:
                    extras.append(f'{label} {num(it[k])}')
            rows += (f'<tr data-search="{e(it["id"].lower())} {e(it.get("name", "").lower())}"><td><code>{e(it["id"])}</code></td><td>{e(it.get("name", ""))}</td><td>{e(it.get("category", ""))}</td>'
                     f'<td>{num(it.get("weight", 1))}</td><td>{it.get("stack", 1)}</td><td>{tags(it.get("tags", []))}</td><td>{e(", ".join(extras))}</td></tr>')
        return f'<h2>{e(title)} <span class="muted">({len(items)})</span></h2><table class="grid"><tr><th>Id</th><th>Nombre</th><th>Categoría</th><th>Peso</th><th>Apilado</th><th>Etiquetas</th><th>Extra</th></tr>{rows}</table>'

    out.append(items_table(content['items'], 'Objetos (base)'))
    if extra['items']:
        out.append(items_table(extra['items'], 'Objetos (ampliado)'))

    def recipes_table(recipes, title):
        rows = ''
        for r in recipes:
            ins = ', '.join(f'{v}× <code>{e(k)}</code>' for k, v in r.get('inputs', {}).items())
            rows += (f'<tr data-search="{e(r["id"].lower())}"><td><code>{e(r["id"])}</code></td><td>{ins}</td><td>{r.get("count", 1)}× <code>{e(r["output"])}</code></td>'
                     f'<td>{e(r.get("station", "") or "en cualquier sitio")}</td><td>{e(r.get("tool", "") or "—")}</td></tr>')
        return f'<h2>{e(title)} <span class="muted">({len(recipes)})</span></h2><table class="grid"><tr><th>Receta</th><th>Entradas</th><th>Resultado</th><th>Estación</th><th>Herramienta (no se gasta)</th></tr>{rows}</table>'

    out.append(recipes_table(content['recipes'], 'Recetas (base)'))
    if extra['recipes']:
        out.append(recipes_table(extra['recipes'], 'Recetas (ampliado)'))

    def nodes_table(nodes, title):
        rows = ''
        for n in nodes:
            patch = n.get('patch', {})
            ptxt = f'{patch.get("count", 0)} + {patch.get("perWorker", 0)}/trabajador, {patch.get("shape", "")}' if patch else '—'
            rows += (f'<tr data-search="{e(n["id"].lower())}"><td><code>{e(n["id"])}</code></td><td><code>{e(n["item"])}</code></td><td>{n.get("capacity", 5)}</td><td>{n.get("perGather", 1)}</td>'
                     f'<td>{num(n.get("regenSeconds", 0)) + " s (+" + str(n.get("regen", 1)) + ")" if n.get("regenSeconds", 0) else "no regenera"}</td>'
                     f'<td>{e(n.get("requires", "") or "manos")}</td><td>{e(n.get("ability", ""))}</td><td>{e(n.get("kind", "") or "—")}</td><td>{e(ptxt)}</td></tr>')
        return (f'<h2>{e(title)} <span class="muted">({len(nodes)})</span></h2><table class="grid"><tr><th>Nodo</th><th>Da</th><th>Capacidad</th><th>Por golpe</th><th>Regeneración</th>'
                f'<th>Herramienta</th><th>Habilidad</th><th>Tipo</th><th>Colocación</th></tr>{rows}</table>')

    out.append(nodes_table(content['nodes'], 'Nodos de recurso (base)'))
    if extra['nodes']:
        out.append(nodes_table(extra['nodes'], 'Nodos de recurso (ampliado)'))
    return '\n'.join(out)


def section_techs(content, extra_techs):
    out = ['<h1>Tecnologías</h1>',
           '<p class="lead">Datos de la sección <code>techs</code> de <code>assets/data/era1.json</code> (y del paquete ampliado). Una receta o un nodo que una tecnología desbloquea '
           'solo se puede usar cuando el agente o el clan la conoce; se descubre experimentando, observando o en las lecciones del campamento (ver CONOCIMIENTO.md).</p>']

    def table(techs, title):
        rows = ''
        for t in techs:
            unlocks = [f'receta <code>{e(r)}</code>' for r in t.get('recipes', [])] + [f'nodo <code>{e(n)}</code>' for n in t.get('nodes', [])] \
                + [f'edificio <code>{e(b)}</code>' for b in t.get('buildings', [])]
            d = t.get('discovery', {})
            how = 'se sabe desde el principio' if t.get('known') else ', '.join(f'{k} {num(v)}' for k, v in d.items()) or '—'
            rows += (f'<tr data-search="{e(t["id"].lower())} {e(t.get("name", "").lower())}"><td><code>{e(t["id"])}</code></td><td>{e(t.get("name", ""))}</td><td>{t.get("era", 1)}</td>'
                     f'<td>{tags(t.get("requires", []))}</td><td>{", ".join(unlocks) or "—"}</td><td>{e(how)}</td></tr>')
        return (f'<h2>{e(title)} <span class="muted">({len(techs)})</span></h2><table class="grid"><tr><th>Id</th><th>Nombre</th><th>Era</th><th>Requiere</th>'
                f'<th>Desbloquea</th><th>Descubrimiento</th></tr>{rows}</table>')
    out.append(table(content.get('techs', []), 'Tecnologías (base)'))
    if extra_techs:
        out.append(table(extra_techs, 'Tecnologías (ampliado)'))
    return '\n'.join(out)


def section_packs(packs):
    out = ['<h1>Paquetes de contenido</h1>',
           '<p class="lead">Cada era es un paquete (<code>assets/data/&lt;id&gt;.pack.json</code>): sus ficheros, de qué paquetes depende, qué objetos vienen de fuera y los huecos conocidos. '
           'El editor carga <code>era1</code> al arrancar; <code>pack load &lt;id&gt; [strict]</code> carga otro y <code>pack check</code> comprueba que toda receta es alcanzable (ver PAQUETES.md).</p>']
    for p in packs:
        given = ''.join(f'<li><code>{e(k)}</code>: {e(v)}</li>' for k, v in p.get('given', {}).items())
        deferred = ''.join(f'<li><code>{e(k)}</code>: {e(v)}</li>' for k, v in p.get('deferred', {}).items())
        out.append(f'<h2 data-search="{e(p.get("id", "").lower())}">{e(p.get("name", p.get("id", "")))} <span class="muted">({e(p.get("id", ""))}, era {p.get("era", "?")}, v{e(str(p.get("version", "")))})</span></h2>'
                   f'<table class="kv"><tr><th>Depende de</th><td>{tags(p.get("depends", [])) or "—"}</td></tr>'
                   f'<tr><th>Ficheros</th><td>{tags(p.get("files", []))}</td></tr>'
                   f'<tr><th>Estaciones</th><td>{tags(p.get("stations", [])) or "—"}</td></tr></table>'
                   + (f'<h3>Vienen de fuera</h3><ul>{given}</ul>' if given else '') + (f'<h3>Huecos conocidos</h3><ul>{deferred}</ul>' if deferred else ''))
    return '\n'.join(out)


# data sections shown as they are written (the systems read them through the content section registry)
DATA_SECTIONS = [
    ('era1.json', 'tribe', 'Tribu: agenda del día y economía (comida, existencias objetivo)'),
    ('era1.json', 'work', 'Trabajo: aptitudes por práctica y tablón de trabajos'),
    ('era1.json', 'knowledge', 'Conocimiento: reglas del clan (lecciones, observación)'),
    ('era1.json', 'exploration', 'Exploración: regiones, expediciones y migración'),
    ('world.json', 'interactives', 'Interactivos: hoguera, pozo, puerta'),
    ('world.json', 'species', 'Especies de la ecología'),
    ('cameras.json', 'cameras', 'Cámaras de juego'),
]


def section_data():
    out = ['<h1>Más datos del juego</h1>',
           '<p class="lead">El resto de secciones de <code>assets/data</code>, tal como están escritas. Cambiarlas no requiere compilar: se recargan con <code>pack load era1</code>.</p>']
    for file, key, title in DATA_SECTIONS:
        doc = load_json(ROOT / 'assets' / 'data' / file, {})
        if key not in doc:
            continue
        text = json.dumps(doc[key], ensure_ascii=False, indent=2)
        out.append(f'<details data-search="{e(key)} {e(title.lower())}"><summary><b>{e(title)}</b> <span class="muted">{e(file)} → <code>{e(key)}</code></span></summary>'
                   f'<pre><code>{e(text)}</code></pre></details>')
    return '\n'.join(out)


def section_tags(rules, content):
    users = {}

    def use(tag, who):
        users.setdefault(tag, set()).add(who)
    for a in rules['abilities']:
        for t in a['requiredTags'] + a['targetRequiredTags'] + a['targetRequiredAnyTags']:
            use(t, f'requiere: {a["name"]}')
        for t in a['blockedTags'] + a['targetBlockedTags']:
            use(t, f'bloquea: {a["name"]}')
        for t in a['activeTags']:
            use(t, f'da: {a["name"]}')
    for fx in rules['effects']:
        for t in fx['grantedTags']:
            use(t, f'efecto: {fx["name"]}')
    for k in rules['entityKinds']:
        for t in k['tags']:
            use(t, f'entidad: {k["kind"]}')
    for it in content['items']:
        for t in it.get('tags', []):
            use(t, f'objeto: {it["id"]}')
    # a nested tree of dotted tags
    tree = {}
    for t in sorted(users):
        node = tree
        for part in t.split('.'):
            node = node.setdefault(part, {})

    def walk(node, prefix):
        out = '<ul>'
        for k in sorted(node):
            full = f'{prefix}.{k}' if prefix else k
            who = sorted(users.get(full, []))
            info = f' <span class="muted">{e("; ".join(who[:6]))}{"…" if len(who) > 6 else ""}</span>' if who else ''
            out += f'<li data-search="{e(full.lower())}"><code>{e(k)}</code>{info}{walk(node[k], full) if node[k] else ""}</li>'
        return out + '</ul>'
    return ('<h1>Árbol de etiquetas</h1><p class="lead">Las etiquetas son rutas con puntos: preguntar por <code>State.Carrying</code> acierta si alguien tiene <code>State.Carrying.Box</code>. '
            'Aquí están todas las que usan hoy las habilidades, los efectos, las entidades y los objetos, y quién las usa.</p>' + walk(tree, ''))


def section_docs(rendered):
    groups = {}
    for name, group, body in rendered:
        groups.setdefault(group, []).append((name, body))
    out = ['<h1>Documentos</h1><p class="lead">Los documentos del repositorio (<code>docs/</code>), tal cual.</p>']
    for g, lst in groups.items():
        out.append(f'<h2>{e(g)}</h2><ul>' + ''.join(f'<li><a href="#doc-{slug(n)}">{e(n)}</a></li>' for n, _ in lst) + '</ul>')
    return '\n'.join(out)


CSS = '''
:root{--bg:#fbfaf7;--fg:#1d2433;--muted:#6b7385;--line:#e3e0d8;--card:#fff;--accent:#2f6f4f;--accent2:#8a5a1f;--code:#f1efe8;--bad:#a23b3b}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#14171d;--fg:#e6e8ee;--muted:#9aa3b5;--line:#2a2f3a;--card:#1b1f27;--accent:#7bc99b;--accent2:#e0b070;--code:#242936;--bad:#e08a8a}}
:root[data-theme=dark]{--bg:#14171d;--fg:#e6e8ee;--muted:#9aa3b5;--line:#2a2f3a;--card:#1b1f27;--accent:#7bc99b;--accent2:#e0b070;--code:#242936;--bad:#e08a8a}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.55 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{position:sticky;top:0;z-index:5;background:var(--bg);border-bottom:1px solid var(--line);padding:10px 16px;display:flex;gap:12px;align-items:center}
header b{white-space:nowrap}header input{flex:1;min-width:0;padding:8px 12px;border:1px solid var(--line);border-radius:8px;background:var(--card);color:var(--fg);font:inherit}
header button{border:1px solid var(--line);background:var(--card);color:var(--fg);border-radius:8px;padding:7px 10px;cursor:pointer}
.layout{display:flex;align-items:flex-start}
nav{width:240px;flex:none;position:sticky;top:57px;max-height:calc(100vh - 57px);overflow:auto;padding:14px 8px 40px 16px;border-right:1px solid var(--line)}
nav a{display:block;padding:4px 8px;border-radius:6px;color:var(--fg);text-decoration:none}nav a:hover,nav a.on{background:var(--code)}
nav h4{margin:14px 8px 4px;font-size:11px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}
main{flex:1;min-width:0;max-width:1100px;padding:20px 28px 80px}
h1{font-size:28px;margin:.2em 0 .4em}h2{font-size:20px;margin:1.6em 0 .5em;border-bottom:1px solid var(--line);padding-bottom:4px}h3{font-size:16px}
.lead{font-size:16px;color:var(--muted)}.muted{color:var(--muted);font-weight:400}.none{color:var(--muted)}
a{color:var(--accent)}code{background:var(--code);padding:1px 5px;border-radius:4px;font:12.5px ui-monospace,Consolas,monospace}pre{background:var(--code);padding:12px;border-radius:8px;overflow:auto}pre code{padding:0}
code.tag{color:var(--accent2)}code.bad{color:var(--bad)}
table{border-collapse:collapse;max-width:100%}.grid{width:100%;display:block;overflow-x:auto}.grid th,.grid td,main table th,main table td{border:1px solid var(--line);padding:5px 9px;text-align:left;vertical-align:top}
.grid th,main table th{background:var(--code)}table.kv{width:100%;margin:6px 0 12px}table.kv th{width:200px;background:transparent;font-weight:500;color:var(--muted)}
details{background:var(--card);border:1px solid var(--line);border-radius:10px;margin:8px 0;padding:6px 12px}details>summary{cursor:pointer;padding:4px 0}details[open]>summary{margin-bottom:6px}
.cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:10px}.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px}.card b{display:block;font-size:26px;color:var(--accent)}.card span{color:var(--muted)}
.heat td[style]{background:color-mix(in srgb,var(--accent) calc(var(--w)*70%),transparent);text-align:center}.swatch{display:inline-block;width:46px;height:18px;border-radius:4px;border:1px solid var(--line)}
section{display:none}section.on{display:block}.hidden{display:none!important}
@media (max-width:820px){nav{display:none}nav.open{display:block;position:fixed;inset:57px 0 0 0;width:auto;background:var(--bg);z-index:4}main{padding:14px 14px 80px}}
'''

JS = '''
const secs=[...document.querySelectorAll('section')],links=[...document.querySelectorAll('nav a')];
function go(id){let sec=document.getElementById(id);if(!sec)return;let host=sec;
 while(host&&host.tagName!=='SECTION')host=host.parentElement;
 if(!host)host=document.getElementById('home');
 secs.forEach(s=>s.classList.toggle('on',s===host));
 links.forEach(a=>a.classList.toggle('on',a.getAttribute('href')==='#'+host.id));
 document.getElementById('nav').classList.remove('open');
 if(host!==sec){sec.scrollIntoView();if(sec.tagName==='DETAILS')sec.open=true}else window.scrollTo(0,0);
 try{history.replaceState(null,'','#'+id)}catch(e){}}
document.addEventListener('click',ev=>{const a=ev.target.closest('a[href^="#"]');if(!a)return;ev.preventDefault();go(a.getAttribute('href').slice(1))});
addEventListener('hashchange',()=>go(location.hash.slice(1)||'home'));
go((location.hash||'#home').slice(1));
document.getElementById('menu').onclick=()=>document.getElementById('nav').classList.toggle('open');
const q=document.getElementById('q');
q.addEventListener('input',()=>{const t=q.value.trim().toLowerCase();
 if(!t){document.querySelectorAll('[data-search]').forEach(x=>x.classList.remove('hidden'));go('home');return}
 secs.forEach(s=>s.classList.add('on'));
 document.querySelectorAll('[data-search]').forEach(x=>{x.classList.toggle('hidden',!x.dataset.search.includes(t))});
 secs.forEach(s=>{s.classList.toggle('on',!!s.querySelector('[data-search]:not(.hidden)'))})});
const th=document.getElementById('theme');th.onclick=()=>{const r=document.documentElement;const dark=getComputedStyle(r).getPropertyValue('--bg').trim()==='#14171d';r.dataset.theme=dark?'light':'dark'};
'''


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=str(DOCS / 'wiki' / 'index.html'))
    ap.add_argument('--rules', default=str(DOCS / 'wiki' / 'data' / 'rules.json'))
    args = ap.parse_args()

    rules = load_json(args.rules)
    if rules is None:
        sys.exit(f'No rules file at {args.rules}. In the editor (with the demos built): docs export {args.rules}')
    content = load_json(ROOT / 'assets' / 'data' / 'era1.json', {'items': [], 'recipes': [], 'nodes': []})
    extra = load_json(ROOT / 'assets' / 'data' / 'era1_ampliado.json', {})
    base_techs = {t['id'] for t in content.get('techs', [])}
    extra_techs = [t for t in extra.get('techs', []) if t['id'] not in base_techs]
    packs = [load_json(f, {}) for f in sorted((ROOT / 'assets' / 'data').glob('*.pack.json'))]
    extra = {k: extra.get(k, []) for k in ('items', 'recipes', 'nodes')}
    # what the extended file repeats from the base is not shown twice
    for k in ('items', 'recipes', 'nodes'):
        base_ids = {x['id'] for x in content.get(k, [])}
        extra[k] = [x for x in extra[k] if x['id'] not in base_ids]
    for k in ('items', 'recipes', 'nodes'):
        content.setdefault(k, [])

    known = {name for name, _ in DOC_LIST}
    rendered = []
    for name, group in DOC_LIST:
        p = DOCS / name
        if p.exists():
            rendered.append((name, group, render_doc(p, known)))

    # the status table of the plan is shown on the home page
    status = ''
    plan = DOCS / 'PLAN_SOCIEDAD_VIVA.md'
    if plan.exists():
        text = plan.read_text(encoding='utf-8')
        m = re.search(r'(\*\*Estado de los hitos\*\*.*?)(?=\n### |\Z)', text, re.S)   # the one status table, at the top of section 4
        if m:
            status = render_doc_text(m.group(1), known)

    sections = [
        ('home', 'Inicio', None, section_home(rules, content, status)),
        ('habilidades', 'Habilidades', 'Reglas', section_abilities(rules)),
        ('efectos', 'Efectos', 'Reglas', section_effects(rules)),
        ('etiquetas', 'Árbol de etiquetas', 'Reglas', section_tags(rules, content)),
        ('atributos', 'Atributos por entidad', 'Reglas', section_attributes(rules)),
        ('necesidades', 'Necesidades', 'Personajes', section_needs(rules)),
        ('mente', 'Sentidos y decisión', 'Personajes', section_mind(rules)),
        ('roles', 'Roles', 'Personajes', section_roles(rules)),
        ('mundo', 'Estaciones y ecología', 'Mundo', section_world(rules)),
        ('contenido', 'Objetos, recetas y nodos', 'Mundo', section_content(content, extra)),
        ('tecnologias', 'Tecnologías', 'Mundo', section_techs(content, extra_techs)),
        ('paquetes', 'Paquetes', 'Datos', section_packs(packs)),
        ('datos', 'Más datos del juego', 'Datos', section_data()),
        ('docs', 'Documentos', 'Documentos', section_docs(rendered)),
    ]
    nav, body = [], []
    last = None
    for sid, title, group, html_ in sections:
        if group != last and group:
            nav.append(f'<h4>{e(group)}</h4>')
            last = group
        nav.append(f'<a href="#{sid}">{e(title)}</a>')
        body.append(f'<section id="{sid}">{html_}</section>')
    g_last = None
    for name, group, doc in rendered:
        if group != g_last:
            nav.append(f'<h4>Doc · {e(group)}</h4>')
            g_last = group
        sid = f'doc-{slug(name)}'
        nav.append(f'<a href="#{sid}">{e(name)}</a>')
        body.append(f'<section id="{sid}" data-search="{e(name.lower())}"><p class="muted">docs/{e(name)}</p>{doc}</section>')

    page = (f'<!doctype html><html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
            f'<title>Wiki GB Engine</title><style>{CSS}</style></head><body>'
            f'<header><button id="menu" title="Menú">☰</button><b>GB Engine · wiki</b><input id="q" type="search" placeholder="Buscar habilidad, atributo, etiqueta, objeto…"><button id="theme" title="Tema">◐</button></header>'
            f'<div class="layout"><nav id="nav">{"".join(nav)}</nav><main>{"".join(body)}</main></div><script>{JS}</script></body></html>')
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page, encoding='utf-8')
    print(f'{out}: {len(page) // 1024} KB, {len(rules["abilities"])} abilities, {len(rules["effects"])} effects, {len(rendered)} documents')


def render_doc_text(text, known):
    body = markdown.markdown(text, extensions=['tables', 'sane_lists'])
    return re.sub(r'href="([^"]+\.md(?:#[^"]*)?)"', lambda m: f'href="#doc-{slug(Path(m.group(1).split("#")[0]).name)}"' if Path(m.group(1).split('#')[0]).name in known else f'href="{m.group(1)}"', body)


if __name__ == '__main__':
    main()
