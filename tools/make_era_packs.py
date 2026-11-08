"""V8: one data pack per era from the Iron Age to the arrival of AI (docs/VISION_MUNDO_VIVO.md §4). Each depends on the one before and adds only
what its age brings: the key technology, its materials and recipes, a building and, for some, a need with its verb. Written as UTF-8."""
import json

import os
D = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'assets', 'data') + os.sep

def item(i, name, cat='resource', tags=None, weight=1.0, stack=10, **kw):
    o = {'category': cat, 'id': i, 'name': name, 'stack': stack, 'tags': tags or ['Item.Resource.' + i.title().replace('_', '')], 'weight': weight}
    o.update(kw)
    return o

def tool(i, name, tag, ability, power, speed):
    return {'id': i, 'name': name, 'category': 'tool', 'weight': 2.0, 'stack': 1, 'tags': [tag], 'ability': ability, 'socket': 'RightHand', 'power': power, 'speed': speed}

def recipe(i, inputs, station, out=None, count=1):
    return {'ability': 'Craft', 'count': count, 'id': i, 'inputs': inputs, 'output': out or i, 'station': station}

def node(i, it, kind, biomes, per_km2, requires='Item.Tool.Pickaxe', model='copper_vein', ability='Mine'):
    return {'ability': ability, 'capacity': 6, 'id': i, 'item': it, 'kind': kind, 'model': model, 'perGather': 1, 'regen': 1, 'regenSeconds': 900.0, 'requires': requires,
            'spawn': {'biomes': biomes, 'max': 10, 'maxSlope': 50.0, 'perHectare': 8.0, 'spacing': 8.0,
                      'zones': {'perKm2': per_km2, 'min': 1, 'max': 2, 'radiusMin': 15.0, 'radiusMax': 35.0, 'fallback': ['Meadow', 'Forest']}}}

def tech(i, name, era, requires, recipes=(), buildings=(), experiment=0.04):
    return {'id': i, 'name': name, 'era': era, 'requires': list(requires), 'recipes': list(recipes), 'buildings': list(buildings),
            'discovery': {'experiment': experiment, 'observe': 0.15, 'teach': 0.1, 'seconds': 90.0, 'waste': 0.4}}

def building(name, needs1, needs2, station=None, provides=None, color=(0.5, 0.45, 0.4), footprint=(3.0, 3.0)):
    b = {'name': name, 'zone': 'craft', 'footprint': list(footprint),
         'phases': [{'name': 'base', 'work': 10.0, 'needs': needs1, 'part': 'slab'}, {'name': 'walls', 'work': 12.0, 'needs': needs2, 'part': 'walls'},
                    {'name': 'roof', 'work': 8.0, 'needs': {'wood': 4}, 'substitutes': {'wood': ['log', 'plank']}, 'part': 'roof'}],
         'color': list(color)}
    if station: b['station'] = station
    if provides: b['provides'] = provides; b['aura'] = 3.0
    return b

def era(i, name, key, frm, interest, progression, leap, pull=0.4):
    return {'id': i, 'name': name, 'key': [key], 'from': [frm], 'pull': pull, 'interest': interest, 'progression': progression, 'leap': leap}

packs = [
    ('era3', 'Era 3 · Hierro', 3, 'era2', {
        'note': 'Era 3, the Iron Age (V8): iron ore, which most lands have but deep, forged into tools for everyone.',
        'items': [item('iron_ore', 'Iron ore', tags=['Item.Resource.Ore.Iron'], weight=2.5), item('iron_ingot', 'Iron ingot', tags=['Item.Resource.Metal.Iron'], weight=2.0),
                  tool('iron_axe', 'Iron axe', 'Item.Tool.Axe.Iron', 'Chop', 4, 1.6), tool('iron_pickaxe', 'Iron pickaxe', 'Item.Tool.Pickaxe.Iron', 'Mine', 4, 1.6)],
        'nodes': [node('iron_vein', 'iron_ore', 'Stone.Iron', ['Rock', 'Snow', 'Forest'], 0.3)],
        'recipes': [recipe('iron_ingot', {'iron_ore': 2, 'wood': 2}, 'forge'), recipe('iron_axe', {'iron_ingot': 2, 'branch': 2}, 'forge'), recipe('iron_pickaxe', {'iron_ingot': 2, 'branch': 2}, 'forge')],
        'techs': [tech('forging', 'Forging', 6, ['alloying'], ['iron_ingot', 'iron_axe', 'iron_pickaxe'], ['smithy'])],
        'buildings': {'smithy': building('Smithy', {'stone': 8}, {'iron_ingot': 2, 'wood': 4}, station='forge', color=(0.35, 0.3, 0.28))},
        'eras': [era('iron', 'Iron Age', 'forging', 'bronze', ['iron_ore'], ['alloying', 'forging'], 300)],
        'diplomacy': {'trade': {'values': {'iron_ore': 3.0, 'iron_ingot': 6.0, 'iron_axe': 10.0, 'iron_pickaxe': 10.0}}}}),
    ('era4', 'Era 4 · Clásica', 4, 'era3', {
        'note': 'Era 4, the classical age (V8): writing on clay tablets, a school where people study, and the need to learn.',
        'items': [item('tablet', 'Clay tablet', tags=['Item.Resource.Writing.Tablet'], weight=0.6), item('scroll', 'Scroll', tags=['Item.Resource.Writing.Scroll'], weight=0.2)],
        'recipes': [recipe('tablet', {'clay': 2}, 'kiln'), recipe('scroll', {'fiber': 3, 'resin': 1}, 'workbench')],
        'techs': [tech('writing', 'Writing', 7, ['forging', 'pottery'], ['tablet', 'scroll'], ['school']),
                  # with the classical age in the game, stone building comes after writing (era1's masonry, from medieval.json, asks only carpentry and pottery)
                  tech('masonry', 'Masonry', 8, ['carpentry', 'pottery', 'writing'], [], ['stone_house', 'townhouse'], 0.03)],
        'buildings': {'school': building('School', {'stone': 8}, {'fired_brick': 6, 'tablet': 2}, provides={'learning': 1.5}, color=(0.7, 0.66, 0.55))},
        'eras': [era('classical', 'Classical Age', 'writing', 'iron', ['clay'], ['forging', 'writing'], 200),
                 era('medieval', 'Middle Ages', 'masonry', 'classical', ['stone', 'fired_brick'], ['timber_framing', 'pottery', 'smelting', 'carpentry', 'masonry'], 100, 0.3)],
        'needs': [{'id': 'learning', 'name': 'Learning', 'era': 'classical', 'verb': 'study', 'provision': 'learning', 'decayPerDay': 30.0, 'threshold': 35.0, 'weight': 0.8, 'seconds': 10.0, 'amount': 50.0}],
        'diplomacy': {'trade': {'values': {'tablet': 3.0, 'scroll': 4.0}}}}),
    ('era5', 'Era 5 · Renacimiento', 5, 'era4', {
        'note': 'Era 5, the Renaissance (V8): paper and the press; knowledge is copied and travels. Its era follows the Middle Ages (masonry, in era1).',
        'items': [item('paper', 'Paper', tags=['Item.Resource.Writing.Paper'], weight=0.1), item('book', 'Book', tags=['Item.Resource.Writing.Book'], weight=0.5)],
        'recipes': [recipe('paper', {'plank': 1, 'water': 1}, 'workbench', count=2), recipe('book', {'paper': 4, 'leather': 1}, 'press')],
        'techs': [tech('printing', 'Printing', 9, ['writing', 'masonry', 'carpentry'], ['paper', 'book'], ['print_shop', 'library'])],
        'buildings': {'print_shop': building('Print shop', {'stone': 8}, {'plank': 6, 'iron_ingot': 2}, station='press', color=(0.55, 0.42, 0.3)),
                      'library': building('Library', {'stone': 10}, {'plank': 6, 'book': 2}, provides={'learning': 3.0}, color=(0.6, 0.5, 0.38))},
        'eras': [era('renaissance', 'Renaissance', 'printing', 'medieval', ['paper'], ['masonry', 'printing'], 60)],
        'diplomacy': {'trade': {'values': {'paper': 1.0, 'book': 8.0}}}}),
    ('era6', 'Era 6 · Industrial', 6, 'era5', {
        'note': 'Era 6, the industrial age (V8): coal, steel and steam; the factory makes what hands made.',
        'items': [item('coal', 'Coal', tags=['Item.Resource.Fuel.Coal'], weight=1.5), item('steel_ingot', 'Steel ingot', tags=['Item.Resource.Metal.Steel'], weight=2.0),
                  item('engine', 'Steam engine', tags=['Item.Resource.Machine.Engine'], weight=8.0, stack=2)],
        'nodes': [node('coal_seam', 'coal', 'Stone.Coal', ['Rock', 'Forest', 'Meadow'], 0.25)],
        'recipes': [recipe('steel_ingot', {'iron_ingot': 2, 'coal': 1}, 'forge'), recipe('engine', {'steel_ingot': 4, 'coal': 2}, 'factory')],
        'techs': [tech('steam_power', 'Steam power', 11, ['printing', 'forging'], ['steel_ingot', 'engine'], ['factory'])],
        'buildings': {'factory': building('Factory', {'stone': 12, 'fired_brick': 8}, {'steel_ingot': 4, 'plank': 6}, station='factory', color=(0.4, 0.32, 0.3), footprint=(4.0, 3.5))},
        'eras': [era('industrial', 'Industrial Age', 'steam_power', 'renaissance', ['coal', 'iron_ore'], ['printing', 'steam_power'], 25)],
        'diplomacy': {'trade': {'values': {'coal': 2.0, 'steel_ingot': 9.0, 'engine': 30.0}}}}),
    ('era7', 'Era 7 · Eléctrica', 7, 'era6', {
        'note': 'Era 7, the electric age (V8): copper wire and dynamos; a power station lights the town.',
        'items': [item('copper_wire', 'Copper wire', tags=['Item.Resource.Electric.Wire'], weight=0.5), item('dynamo', 'Dynamo', tags=['Item.Resource.Machine.Dynamo'], weight=6.0, stack=2)],
        'recipes': [recipe('copper_wire', {'copper_ingot': 1}, 'factory', count=2), recipe('dynamo', {'copper_wire': 4, 'steel_ingot': 2, 'engine': 1}, 'factory')],
        'techs': [tech('electricity', 'Electricity', 13, ['steam_power'], ['copper_wire', 'dynamo'], ['power_station'])],
        'buildings': {'power_station': building('Power station', {'stone': 12, 'fired_brick': 8}, {'dynamo': 1, 'copper_wire': 6}, color=(0.45, 0.45, 0.5), footprint=(4.0, 3.5))},
        'eras': [era('electric', 'Electric Age', 'electricity', 'industrial', ['copper_ore'], ['steam_power', 'electricity'], 10)],
        'diplomacy': {'trade': {'values': {'copper_wire': 3.0, 'dynamo': 40.0}}}}),
    ('era8', 'Era 8 · Información', 8, 'era7', {
        'note': 'Era 8, the information age (V8): sand made silicon, chips and computers; an office where people work with knowledge.',
        'items': [item('sand', 'Sand', tags=['Item.Resource.Sand'], weight=1.0), item('silicon', 'Silicon', tags=['Item.Resource.Silicon'], weight=0.5),
                  item('chip', 'Chip', tags=['Item.Resource.Electric.Chip'], weight=0.05), item('computer', 'Computer', tags=['Item.Resource.Machine.Computer'], weight=5.0, stack=2)],
        'nodes': [node('sand_bank', 'sand', 'Loose.Sand', ['Shore', 'Dunes', 'Meadow'], 0.5, requires='', model='', ability='Collect')],
        'recipes': [recipe('silicon', {'sand': 3, 'coal': 1}, 'factory'), recipe('chip', {'silicon': 1, 'copper_wire': 1}, 'factory', count=2),
                    recipe('computer', {'chip': 4, 'steel_ingot': 1, 'copper_wire': 2}, 'factory')],
        'techs': [tech('computing', 'Computing', 15, ['electricity', 'writing'], ['silicon', 'chip', 'computer'], ['office'])],
        'buildings': {'office': building('Office', {'stone': 10, 'fired_brick': 10}, {'computer': 2, 'plank': 6}, provides={'learning': 4.0}, color=(0.62, 0.64, 0.68), footprint=(4.0, 3.5))},
        'eras': [era('information', 'Information Age', 'computing', 'electric', ['sand'], ['electricity', 'computing'], 5)],
        'diplomacy': {'trade': {'values': {'sand': 0.5, 'silicon': 3.0, 'chip': 6.0, 'computer': 40.0}}}}),
    ('era9', 'Era 9 · Inteligencia artificial', 9, 'era8', {
        'note': 'Era 9, the arrival of AI (V8): a data centre; machines do work and decide, and people need a purpose (what is left for them is a question each culture answers).',
        'items': [item('model', 'Trained model', tags=['Item.Resource.Machine.Model'], weight=0.01, stack=5)],
        'recipes': [recipe('model', {'computer': 2, 'book': 2}, 'datacenter')],
        'techs': [tech('artificial_intelligence', 'Artificial intelligence', 17, ['computing'], ['model'], ['datacenter', 'commons'])],
        'buildings': {'datacenter': building('Data centre', {'stone': 12, 'fired_brick': 10}, {'computer': 4, 'copper_wire': 8}, station='datacenter', color=(0.5, 0.55, 0.62), footprint=(4.0, 4.0)),
                      'commons': building('Commons', {'stone': 8}, {'plank': 6}, provides={'purpose': 2.0}, color=(0.55, 0.62, 0.45))},
        'eras': [era('ai', 'Age of AI', 'artificial_intelligence', 'information', ['computer'], ['computing', 'artificial_intelligence'], 2)],
        'needs': [{'id': 'purpose', 'name': 'Purpose', 'era': 'ai', 'verb': 'reflect', 'provision': 'purpose', 'decayPerDay': 40.0, 'threshold': 40.0, 'weight': 1.0, 'seconds': 10.0, 'amount': 50.0}],
        'diplomacy': {'trade': {'values': {'model': 60.0}}}}),
]

for pid, name, n, dep, content in packs:
    for nd in content.get('nodes', []):
        if not nd['requires']: del nd['requires']
        if not nd['model']: del nd['model']
    fname = pid + '.json'
    with open(D + fname, 'w', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps(content, ensure_ascii=False, indent=2) + '\n')
    with open(D + pid + '.pack.json', 'w', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps({'id': pid, 'name': name, 'era': n, 'version': '1.0', 'depends': [dep], 'files': [fname]}, ensure_ascii=False, indent=2) + '\n')
    print('wrote', pid)
