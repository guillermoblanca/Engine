"""Editor --exec for the TERR0 "before/after" shots of a probed world (documentation/02_tecnico/motor/TERRENO_V5.md, section 3).

    python -I tools/terrain_probe_shots.py <dir>/<tag>_info.json <dir>/<tag>_height.f32 <screenshot_dir> "<setup commands>" <prefix>

Prints the camera of each shot (an aerial, along the river, across it, its steepest reach, the lake shore, the steepest slope), placed at
eye height on the probed ground, and the whole --exec line. Standard library only."""
import json, math, struct, sys
info, f32, prefix, setup, name = sys.argv[1:6]
d = json.load(open(info)); n = int(d['n']); size = d['size']
h = struct.unpack('<%df' % (n*n), open(f32, 'rb').read())
sp = size / (n - 1); lo = -size / 2
def ground(x, z):
    fx = min(max((x - lo) / sp, 0), n - 1.001); fz = min(max((z - lo) / sp, 0), n - 1.001)
    i, j = int(fx), int(fz); u, v = fx - i, fz - j
    a, b, c, e = h[j*n+i], h[j*n+i+1], h[(j+1)*n+i], h[(j+1)*n+i+1]
    return a*(1-u)*(1-v) + b*u*(1-v) + c*(1-u)*v + e*u*v
def cam(C, T):
    ox, oy, oz = C[0]-T[0], C[1]-T[1], C[2]-T[2]
    dist = math.sqrt(ox*ox + oy*oy + oz*oz)
    return 'camera %.0f %.1f %.1f %.1f %.2f %.1f' % (math.degrees(math.atan2(ox, oz)), math.degrees(math.asin(oy / dist)), dist, T[0], T[1], T[2])
r = d.get('river', [])
shots = [(name + '_aerial', 'camera 20 50 %.0f 0 0 0' % (size * 0.75))]
big = size > 1000
if r:
    # along: from a bank-side point upstream, at eye height, looking down the river ~ 25-40 m ahead
    k = len(r) // 2; up = r[max(k - 2, 0)]; T = r[min(k + 1, len(r) - 1)]
    dx, dz = T[0] - up[0], T[2] - up[2]; l = math.hypot(dx, dz); sx, sz = -dz / l, dx / l
    C = (up[0] + sx * 4, 0, up[2] + sz * 4); C = (C[0], max(ground(C[0], C[2]), up[1]) + 1.7, C[2])
    shots.append((name + '_river_along', cam(C, (T[0], T[1] + 0.3, T[2]))))
    # across: from 14 m to the side, at eye height, looking at the water (the cross-section: banks, the ribbon's edges)
    k = len(r) // 4; a, b = r[max(k - 1, 0)], r[min(k + 1, len(r) - 1)]
    dx, dz = b[0] - a[0], b[2] - a[2]; l = math.hypot(dx, dz); sx, sz = -dz / l, dx / l
    T = r[k]; C = (T[0] + sx * 14, 0, T[2] + sz * 14); C = (C[0], ground(C[0], C[2]) + 1.7, C[2])
    shots.append((name + '_river_across', cam(C, (T[0], T[1], T[2]))))
    # steepest reach of the course (the biggest drop between two kept points): looking up at it from below
    best, kb = -1, 1
    for i in range(1, len(r)):
        drop = (r[i-1][1] - r[i][1]) / max(math.hypot(r[i][0]-r[i-1][0], r[i][2]-r[i-1][2]), 1)
        if drop > best: best, kb = drop, i
    lowp = r[min(kb + 1, len(r) - 1)]; T = r[kb - 1]
    dx, dz = lowp[0] - T[0], lowp[2] - T[2]; l = max(math.hypot(dx, dz), 1); sx, sz = -dz / l, dx / l
    C = (lowp[0] + sx * 5, 0, lowp[2] + sz * 5); C = (C[0], max(ground(C[0], C[2]), lowp[1]) + 1.7, C[2])
    shots.append((name + '_river_steep', cam(C, (T[0], T[1], T[2]))))
    print('# steepest reach %.2f m/m at point %d (%.1f, %.1f)' % (best, kb, T[0], T[2]))
if 'lake_x' in d:
    # shore: from land 1.5 radii from the centre, opposite the river mouth, looking across the lake
    lx, lz, R, L = d['lake_x'], d['lake_z'], d['lake_r'], d['lake_level']
    mx, mz = (r[-1][0] - lx, r[-1][2] - lz) if r else (1, 0); l = math.hypot(mx, mz) or 1
    ang = math.atan2(mz, mx) + math.radians(150)
    C = (lx + math.cos(ang) * R * 1.45, 0, lz + math.sin(ang) * R * 1.45); C = (C[0], max(ground(C[0], C[2]), L) + 1.7, C[2])
    shots.append((name + '_shore', cam(C, (lx, L, lz))))
# slope: the steepest cell away from the border, seen from 35 m down its fall line at eye height
best, bi, bj, g = 0, n // 2, n // 2, (1, 0)
m = n // 8
for j in range(m, n - m, 2):
    for i in range(m, n - m, 2):
        gx = (h[j*n+i+1] - h[j*n+i-1]) / (2*sp); gz = (h[(j+1)*n+i] - h[(j-1)*n+i]) / (2*sp)
        if gx*gx + gz*gz > best: best, bi, bj, g = gx*gx + gz*gz, i, j, (gx, gz)
x, z = lo + bi*sp, lo + bj*sp; gl = math.hypot(*g); ux, uz = g[0]/gl, g[1]/gl
back = 35 if not big else 80
C = (x - ux*back, 0, z - uz*back); C = (C[0], ground(C[0], C[2]) + 1.7, C[2])
shots.append((name + '_slope', cam(C, (x, ground(x, z), z))))
print('# steepest slope %.0f deg at %.1f %.1f' % (math.degrees(math.atan(math.sqrt(best))), x, z))
exe = setup + '; time 11; ' + ('set camera.far 4500; ' if big else '')
for f, c in shots: exe += c + '; wait 40; screenshot %s/%s.png; ' % (prefix, f)
print(exe + 'quit')
