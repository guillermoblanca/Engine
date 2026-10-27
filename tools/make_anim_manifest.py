#!/usr/bin/env python3
"""Adds the sets, montages and pack listings of the Mixamo packs to an animation manifest (see Engine/src/GB/Animation/AnimManifest.h).

    python tools/make_anim_manifest.py --base assets/animation/mixamo.manifest.json --packs D:/GameEngine/packs --out assets/animation/mixamo.manifest.json

`--packs` is a folder with one subfolder per pack (locomotion, farming, injured, magic, longbow, swordshield, gestures), each with the .fbx clips
of the pack (the packs themselves are not in the repository). `--base` is a manifest to extend: export the built-in one with the Editor command
`manifest_save <file>`. Running it again replaces the entries it made, so it can be repeated when a pack changes.
Clips named in a rule but missing from the pack are reported and left out.
"""
import argparse
import json
import os
import sys

PACKS = {
    'locomotion': 'Mixamo "Male Locomotion": idle, walk, run, strafes, turns and a jump',
    'farming': 'Mixamo farming: carrying a box, holding, wheelbarrow, planting, watering, picking, milking',
    'injured': 'Mixamo "Male Injured": injured walk and run in every direction, idles, turns and jumps',
    'magic': 'Mixamo "Pro Magic": standing and crouched locomotion, magic attacks and casts, blocking, hit reactions, deaths, jumps',
    'longbow': 'Mixamo "Longbow Aiming": aim idle, aim walk in four directions, draw, overdraw and recoil',
    'swordshield': 'Mixamo "Sword and Shield": locomotion, slashes, attacks, blocks, impacts, crouch, casting, deaths, sheathing',
    'gestures': 'Mixamo "Gestures Basic": nods, head shakes, dismissing, angry, happy, acknowledging, sighing',
}

# locomotion sets of the new packs: the gaits (placed by their measured velocity) and the turns on the spot
SETS = [
    ('standing', 'magic', 'Unarmed standing locomotion with the four-direction walk and run and a sprint', [
        'standing idle', 'Standing Walk Forward', 'Standing Walk Back', 'Standing Walk Left', 'Standing Walk Right',
        'Standing Run Forward', 'Standing Run Back', 'Standing Run Left', 'Standing Run Right', 'Standing Sprint Forward'],
        ['Standing Turn Left 90', 'Standing Turn Right 90']),
    ('crouch', 'magic', 'Crouched: idle and walk in four directions', [
        'Crouch Idle', 'Crouch Walk Forward', 'Crouch Walk Back', 'Crouch Walk Left', 'Crouch Walk Right'],
        ['Crouch Turn Left 90', 'Crouch Turn Right 90']),
    ('sword_shield', 'swordshield', 'Sword and shield stance: walk, run and strafes', [
        'sword and shield idle', 'sword and shield walk', 'sword and shield run',
        'sword and shield strafe', 'sword and shield strafe (2)', 'sword and shield strafe (3)', 'sword and shield strafe (4)'],
        ['sword and shield turn', 'sword and shield turn (2)']),
    ('sword_crouch', 'swordshield', 'Sword and shield crouched', [
        'sword and shield crouch idle', 'sword and shield crouching', 'sword and shield crouching (2)', 'sword and shield crouching (3)'], []),
    ('bow_aim', 'longbow', 'Aiming a longbow: idle and walk in four directions', [
        'standing idle 01', 'standing aim walk forward', 'standing aim walk back', 'standing aim walk left', 'standing aim walk right'], []),
]

# montages: (name, clip, slot, loop, note)
MONTAGES = [
    # magic
    ('magic_cast_1h', 'standing 1H cast spell 01'), ('magic_attack_1h_1', 'Standing 1H Magic Attack 01'),
    ('magic_attack_1h_2', 'Standing 1H Magic Attack 02'), ('magic_attack_1h_3', 'Standing 1H Magic Attack 03'),
    ('magic_cast_2h', 'Standing 2H Cast Spell 01'), ('magic_area_1', 'Standing 2H Magic Area Attack 01'),
    ('magic_area_2', 'Standing 2H Magic Area Attack 02'),
    ('magic_attack_2h_1', 'Standing 2H Magic Attack 01'), ('magic_attack_2h_2', 'Standing 2H Magic Attack 02'),
    ('magic_attack_2h_3', 'Standing 2H Magic Attack 03'), ('magic_attack_2h_4', 'Standing 2H Magic Attack 04'),
    ('magic_attack_2h_5', 'Standing 2H Magic Attack 05'),
    # blocking
    ('block_start', 'Standing Block Start'), ('block_idle', 'Standing Block Idle', 'FullBody', True),
    ('block_end', 'Standing Block End'), ('block_react', 'Standing Block React Large'),
    # hits and deaths
    ('react_small_back', 'Standing React Small From Back'), ('react_small_front', 'Standing React Small From Front'),
    ('react_small_left', 'Standing React Small From Left'), ('react_small_right', 'Standing React Small From Right'),
    ('react_large_back', 'Standing React Large From Back'), ('react_large_front', 'Standing React Large From Front'),
    ('react_large_left', 'Standing React Large From Left'), ('react_large_right', 'Standing React Large From Right'),
    ('death_backward', 'Standing React Death Backward'), ('death_forward', 'Standing React Death Forward'),
    ('death_left', 'Standing React Death Left'), ('death_right', 'Standing React Death Right'),
    # posture changes and jumps
    ('crouch_to_stand', 'Crouch To Standing Idle'), ('stand_to_crouch', 'Standing Idle To Crouch'),
    ('jump_standing', 'Standing Jump'), ('jump_running', 'Standing Jump Running'),
    ('landing_running', 'Standing Jump Running Landing'), ('landing_standing', 'Standing Land To Standing Idle'),
    ('idle_variant_2', 'standing idle 02'), ('idle_variant_3', 'Standing Idle 03'), ('idle_variant_4', 'Standing Idle 04'),
    # sword and shield
    ('sword_slash_1', 'sword and shield slash'), ('sword_slash_2', 'sword and shield slash (2)'),
    ('sword_slash_3', 'sword and shield slash (3)'), ('sword_slash_4', 'sword and shield slash (4)'),
    ('sword_slash_5', 'sword and shield slash (5)'),
    ('sword_attack_1', 'sword and shield attack'), ('sword_attack_2', 'sword and shield attack (2)'),
    ('sword_attack_3', 'sword and shield attack (3)'), ('sword_attack_4', 'sword and shield attack (4)'),
    ('sword_kick', 'sword and shield kick'), ('sword_power_up', 'sword and shield power up'),
    ('sword_cast_1', 'sword and shield casting'), ('sword_cast_2', 'sword and shield casting (2)'),
    ('sword_block_1', 'sword and shield block'), ('sword_block_2', 'sword and shield block (2)'),
    ('sword_block_idle', 'sword and shield block idle', 'FullBody', True),
    ('sword_crouch_block', 'sword and shield crouch block'), ('sword_crouch_block_2', 'sword and shield crouch block (2)'),
    ('sword_crouch_block_idle', 'sword and shield crouch block idle', 'FullBody', True),
    ('sword_crouch_down', 'sword and shield crouch'),
    ('sword_impact_1', 'sword and shield impact'), ('sword_impact_2', 'sword and shield impact (2)'), ('sword_impact_3', 'sword and shield impact (3)'),
    ('sword_death_1', 'sword and shield death'), ('sword_death_2', 'sword and shield death (2)'),
    ('sword_jump_1', 'sword and shield jump'), ('sword_jump_2', 'sword and shield jump (2)'),
    ('sword_turn_180_1', 'sword and shield 180 turn'), ('sword_turn_180_2', 'sword and shield 180 turn (2)'),
    ('sword_sheath_1', 'sheath sword 1'), ('sword_sheath_2', 'sheath sword 2'),
    ('sword_idle_2', 'sword and shield idle (2)'), ('sword_idle_3', 'sword and shield idle (3)'), ('sword_idle_4', 'sword and shield idle (4)'),
    # injured idles and jumps, kneeling
    ('injured_stumble', 'injured stumble idle'), ('injured_wave', 'injured wave idle'), ('injured_hurting', 'injured hurting idle'),
    ('injured_jump_running', 'injured run jump'), ('injured_jump_standing', 'injured standing jump'),
    ('kneel_idle', 'kneeling idle', 'FullBody', True),
    # longbow
    ('bow_draw', 'standing draw arrow'), ('bow_aim_overdraw', 'standing aim overdraw', 'FullBody', True), ('bow_recoil', 'standing aim recoil'),
    # gestures: the upper body only, so a character can gesture while it walks
    ('gesture_look_away', 'look away gesture', 'UpperBody'), ('gesture_dismiss', 'dismissing gesture', 'UpperBody'),
    ('gesture_weight_shift', 'weight shift', 'UpperBody'), ('gesture_nod_sarcastic', 'sarcastic head nod', 'UpperBody'),
    ('gesture_head_no', 'shaking head no', 'UpperBody'), ('gesture_nod_yes', 'head nod yes', 'UpperBody'),
    ('gesture_cocky', 'being cocky', 'UpperBody'), ('gesture_annoyed', 'annoyed head shake', 'UpperBody'),
    ('gesture_nod_hard', 'hard head nod', 'UpperBody'), ('gesture_angry', 'angry gesture', 'UpperBody'),
    ('gesture_happy', 'happy hand gesture', 'UpperBody'), ('gesture_acknowledge', 'acknowledging', 'UpperBody'),
    ('gesture_nod_long', 'lengthy head nod', 'UpperBody'), ('gesture_thoughtful', 'thoughtful head shake', 'UpperBody'),
    ('gesture_sigh', 'relieved sigh', 'UpperBody'),
]


def clips_of(folder):
    out = []
    for f in sorted(os.listdir(folder)):
        stem, ext = os.path.splitext(f)
        if ext.lower() == '.fbx' and stem != 'X Bot':
            out.append(stem)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--base', required=True)
    ap.add_argument('--packs', required=True)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()

    with open(args.base, encoding='utf-8') as f:
        manifest = json.load(f)

    available = {}
    manifest['packs'] = []
    for pack, description in PACKS.items():
        folder = os.path.join(args.packs, pack)
        if not os.path.isdir(folder):
            print('missing pack folder', folder, file=sys.stderr)
            continue
        names = clips_of(folder)
        for n in names:
            available[n] = pack
        manifest['packs'].append({'id': pack, 'description': description, 'clips': names})

    problems = []

    def need(clips, what):
        keep = []
        for c in clips:
            if c in available:
                keep.append(c)
            else:
                problems.append('%s: no clip "%s" in the packs' % (what, c))
        return keep

    own = {s[0] for s in SETS}
    manifest['sets'] = [s for s in manifest.get('sets', []) if s.get('name') not in own]
    for name, pack, note, clips, turns in SETS:
        manifest['sets'].append({'name': name, 'pack': pack, 'note': note,
                                 'clips': need(clips, 'set ' + name), 'turns': need(turns, 'set ' + name + ' turns')})

    names = {m[0] for m in MONTAGES}
    manifest['montages'] = [m for m in manifest.get('montages', []) if m.get('name') not in names]
    have_slots = {s['name'] for s in manifest.get('slots', [])}
    if 'UpperBody' not in have_slots:
        manifest.setdefault('slots', []).append({'name': 'UpperBody', 'rootBone': 'mixamorig:Spine'})
    for m in MONTAGES:
        name, clip = m[0], m[1]
        slot = m[2] if len(m) > 2 else 'FullBody'
        loop = m[3] if len(m) > 3 else False
        if clip not in available:
            problems.append('montage %s: no clip "%s" in the packs' % (name, clip))
            continue
        manifest['montages'].append({
            'name': name, 'clip': clip, 'slot': slot, 'blendIn': 0.2, 'blendOut': 0.25, 'playRate': 1.0,
            'loop': loop, 'stripTravel': True, 'notifies': [], 'sections': []})

    with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
        f.write('\n')
    print('wrote %s: %d set(s), %d montage(s), %d pack(s)' % (args.out, len(manifest['sets']) + 1, len(manifest['montages']), len(manifest['packs'])))
    for p in problems:
        print('warning:', p, file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
