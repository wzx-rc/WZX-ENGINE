// Generates the prefabs of the mecha RTS level (game/prefabs/mecha/*.prefab): kit-bashed from the editor's parametric
// primitives (cube, cylinder, sphere, torus, disk), pivots for the joints, materials, animation clips. The same files
// the Prefab studio reads and writes; open them there (File > Open) to edit by hand.
// Usage: node tools/mecha/gen_prefabs.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const outDir = path.join(root, 'game', 'prefabs', 'mecha');
fs.mkdirSync(outDir, { recursive: true });

// ---------------------------------------------------------------- palette (soft stylised sci-fi, sRGB 0..1)
const M = {
  armor:   { name: 'Armor White', baseColor: [0.80, 0.82, 0.84], roughness: 0.42, metallic: 0.0, specular: 0.5, coat: 0.4 },
  armorB:  { name: 'Cobalt',      baseColor: [0.16, 0.34, 0.70], roughness: 0.38, metallic: 0.1, specular: 0.5, coat: 0.5 },
  steel:   { name: 'Gunmetal',    baseColor: [0.36, 0.38, 0.42], roughness: 0.4, metallic: 0.75 },
  dark:    { name: 'Dark Rubber', baseColor: [0.06, 0.06, 0.07], roughness: 0.75 },
  amber:   { name: 'Amber Hazard',baseColor: [0.95, 0.62, 0.12], roughness: 0.45, specular: 0.5 },
  glass:   { name: 'Canopy Glass',baseColor: [0.55, 0.80, 0.95], roughness: 0.05, transmission: 0.85, ior: 1.45 },
  visor:   { name: 'Visor Glow',  baseColor: [0.2, 0.7, 1.0], emission: [0.2, 0.65, 1.0], emissionStrength: 2.2, roughness: 0.2 },
  green:   { name: 'Vitae Glow',  baseColor: [0.2, 0.95, 0.35], emission: [0.18, 1.0, 0.32], emissionStrength: 1.6, roughness: 0.15 },
  greenT:  { name: 'Vitae Tank',  baseColor: [0.55, 1.0, 0.65], roughness: 0.04, transmission: 0.9, ior: 1.33, emission: [0.05, 0.4, 0.12], emissionStrength: 0.25 },
  red:     { name: 'Beacon Red',  baseColor: [1.0, 0.2, 0.15], emission: [1.0, 0.22, 0.15], emissionStrength: 2.5 },
  lamp:    { name: 'Lamp Warm',   baseColor: [1.0, 0.85, 0.6], emission: [1.0, 0.78, 0.5], emissionStrength: 2.8 },
  concrete:{ name: 'Concrete',    baseColor: [0.46, 0.47, 0.48], roughness: 0.85 },
  pad:     { name: 'Pad Metal',   baseColor: [0.30, 0.32, 0.35], roughness: 0.6, metallic: 0.5 },
  rock:    { name: 'Basalt',      baseColor: [0.24, 0.22, 0.24], roughness: 0.9 },
  rock2:   { name: 'Ochre Rock',  baseColor: [0.55, 0.40, 0.28], roughness: 0.92 },
  crate:   { name: 'Crate Olive', baseColor: [0.34, 0.38, 0.26], roughness: 0.7 },
  team:    { name: 'Team Blue',   baseColor: [0.10, 0.52, 1.0], roughness: 0.32, specular: 0.6, coat: 0.6, emission: [0.05, 0.3, 0.8], emissionStrength: 0.35 },
};
function material(m) {
  return {
    name: m.name, baseColor: m.baseColor, texture: null, roughness: m.roughness ?? 0.5, metallic: m.metallic ?? 0,
    specular: m.specular ?? 0.5, transmission: m.transmission ?? 0, ior: m.ior ?? 1.5, coat: m.coat ?? 0,
    coatRoughness: 0.08, emission: m.emission ?? [0, 0, 0], emissionStrength: m.emissionStrength ?? 0, preset: 0,
  };
}

// ---------------------------------------------------------------- shapes (pivot at the centre of the base, y up)
const box = (w, h, d, subdiv = 1) => ({ primitive: 'Cube', size: [w, h, d], radius: 0.5, radius2: 0.15, segments: 32, rings: 16, subdiv });
const cyl = (r, h, seg = 24) => ({ primitive: 'Cylinder', size: [1, h, 1], radius: r, radius2: 0.15, segments: seg, rings: 16, subdiv: 1 });
const sph = (r, seg = 32, rings = 16) => ({ primitive: 'Sphere', size: [1, 1, 1], radius: r, radius2: 0.15, segments: seg, rings, subdiv: 1 });
const tor = (R, r, seg = 32) => ({ primitive: 'Torus', size: [1, 1, 1], radius: R, radius2: r, segments: seg, rings: 12, subdiv: 1 });
const disk = (r, seg = 32) => ({ primitive: 'Disk', size: [1, 1, 1], radius: r, radius2: 0.15, segments: seg, rings: 1, subdiv: 1 });

class Prefab {
  constructor(name, game) { this.name = name; this.game = game; this.parts = []; this.clips = []; this.next = 1; }
  // part(parent id, name, shape or null (pivot), position, rotation (deg, YXZ), scale, material key)
  part(parent, name, shape, pos = [0, 0, 0], rot = [0, 0, 0], scale = [1, 1, 1], mat = null) {
    const id = this.next++;
    const p = { id, parent, name };
    if (shape) p.shape = shape;
    Object.assign(p, { position: pos, rotation: rot, scale, visible: true, collapsed: false });
    if (shape) p.material = material(M[mat ?? 'armor']);
    this.parts.push(p);
    return id;
  }
  pivot(parent, name, pos = [0, 0, 0], rot = [0, 0, 0]) { return this.part(parent, name, null, pos, rot); }
  // box centred on (x, z), standing on y (the primitive's pivot is at its base)
  box(parent, name, w, h, d, pos, mat, rot = [0, 0, 0]) { return this.part(parent, name, box(w, h, d), pos, rot, [1, 1, 1], mat); }
  // horizontal cylinder along X or Z, centred on pos
  hcyl(parent, name, r, len, pos, axis, mat, seg = 20) {
    const rot = axis === 'x' ? [0, 0, -90] : [90, 0, 0];
    const off = axis === 'x' ? [-len / 2, 0, 0] : [0, 0, -len / 2];
    return this.part(parent, name, cyl(r, len, seg), [pos[0] + off[0], pos[1] + off[1], pos[2] + off[2]], rot, [1, 1, 1], mat);
  }
  clip(name, length, loop, tracks) {
    this.clips.push({
      name, length, fps: 30, loop,
      tracks: tracks.map(([part, channel, keys]) => ({ part, channel, keys: keys.map(([t, v]) => ({ t, v, interp: 'Smooth' })) })),
    });
  }
  save() {
    const json = { format: 'wzx-prefab', version: 1, name: this.name, game: this.game, parts: this.parts, clips: this.clips };
    const file = path.join(outDir, this.name.replace(/\s+/g, '') + '.prefab');
    fs.writeFileSync(file, JSON.stringify(json, null, 2));
    console.log('wrote', path.relative(root, file), this.parts.length, 'parts', this.clips.length, 'clips');
  }
}
const loop4 = (len, a, b) => [[0, a], [len / 4, b], [len / 2, a], [3 * len / 4, b.map(v => -v)], [len, a]];

// ---------------------------------------------------------------- worker mecha (harvester, ~1.3 tall)
function worker() {
  const P = new Prefab('Mecha Worker', { category: 'Unit', footprintX: 1, footprintZ: 1, selectionRadius: 0.85, height: 1.9, blocksPath: false });
  const root = P.part(0, 'Root', null, [0, 0, 0], [0, 0, 0], [1.35, 1.35, 1.35]);   // oversized for readability
  const body = P.pivot(root, 'Body', [0, 0.72, 0]);
  P.box(body, 'Hull', 0.78, 0.42, 0.62, [0, 0, 0], 'armor');
  P.box(body, 'Hull Stripe', 0.80, 0.08, 0.64, [0, 0.26, 0], 'team');
  P.box(body, 'Hull Top', 0.62, 0.12, 0.5, [0, 0.42, -0.02], 'team');
  P.part(body, 'Cockpit', sph(0.2), [0, 0.42, 0.12], [0, 0, 0], [1, 0.75, 1.1], 'glass');
  P.box(body, 'Visor', 0.36, 0.05, 0.03, [0, 0.27, 0.43], 'visor');
  // back tank of Vitae (the resource it carries)
  P.part(body, 'Back Tank', cyl(0.14, 0.42), [0, 0.1, -0.46], [0, 0, 0], [1, 1, 1], 'greenT');
  P.part(body, 'Back Tank Core', cyl(0.08, 0.38), [0, 0.12, -0.46], [0, 0, 0], [1, 1, 1], 'green');
  P.part(body, 'Tank Ring', tor(0.15, 0.025), [0, 0.48, -0.46], [0, 0, 0], [1, 1, 1], 'steel');
  P.part(body, 'Antenna', cyl(0.012, 0.42), [0.26, 0.5, -0.2], [0, 0, 0], [1, 1, 1], 'steel');
  P.part(body, 'Antenna Tip', sph(0.03), [0.26, 0.9, -0.2], [0, 0, 0], [1, 1, 1], 'red');
  // claw arm (right)
  const shoulder = P.pivot(body, 'Arm Pivot', [0.45, 0.2, 0.05], [0, 0, 0]);
  P.box(shoulder, 'Arm Upper', 0.1, 0.1, 0.42, [0.04, -0.05, 0.2], 'steel');
  const elbow = P.pivot(shoulder, 'Claw Pivot', [0.04, 0, 0.42]);
  P.box(elbow, 'Claw A', 0.04, 0.16, 0.06, [-0.04, -0.12, 0.02], 'amber', [0, 0, 0]);
  P.box(elbow, 'Claw B', 0.04, 0.16, 0.06, [0.06, -0.12, 0.02], 'amber', [0, 0, 0]);
  // legs
  const legs = [];
  for (const s of [-1, 1]) {
    const hip = P.pivot(root, s < 0 ? 'Hip L' : 'Hip R', [0.26 * s, 0.74, 0]);
    P.part(hip, 'Hip Joint', sph(0.08), [0, -0.08, 0], [0, 0, 90], [1, 1, 1], 'steel');
    P.box(hip, 'Thigh', 0.12, 0.34, 0.14, [0, -0.38, 0.02], 'armorB');
    const knee = P.pivot(hip, s < 0 ? 'Knee L' : 'Knee R', [0, -0.38, 0.02]);
    P.box(knee, 'Shin', 0.11, 0.32, 0.12, [0, -0.32, -0.02], 'armor');
    P.box(knee, 'Foot', 0.18, 0.06, 0.28, [0, -0.36, 0.03], 'steel');
    legs.push({ hip, knee });
  }
  const [L, R] = legs;
  P.clip('Idle', 2, true, [
    [body, 'Position', [[0, [0, 0.72, 0]], [1, [0, 0.69, 0]], [2, [0, 0.72, 0]]]],
    [shoulder, 'Rotation', [[0, [0, 0, 0]], [1, [-8, 6, 0]], [2, [0, 0, 0]]]],
  ]);
  P.clip('Walk', 0.8, true, [
    [L.hip, 'Rotation', [[0, [28, 0, 0]], [0.4, [-28, 0, 0]], [0.8, [28, 0, 0]]]],
    [R.hip, 'Rotation', [[0, [-28, 0, 0]], [0.4, [28, 0, 0]], [0.8, [-28, 0, 0]]]],
    [L.knee, 'Rotation', [[0, [0, 0, 0]], [0.2, [-30, 0, 0]], [0.4, [0, 0, 0]], [0.8, [0, 0, 0]]]],
    [R.knee, 'Rotation', [[0, [0, 0, 0]], [0.4, [0, 0, 0]], [0.6, [-30, 0, 0]], [0.8, [0, 0, 0]]]],
    [body, 'Position', [[0, [0, 0.72, 0]], [0.2, [0, 0.76, 0]], [0.4, [0, 0.72, 0]], [0.6, [0, 0.76, 0]], [0.8, [0, 0.72, 0]]]],
  ]);
  P.clip('Harvest', 1.2, true, [
    [shoulder, 'Rotation', [[0, [0, 0, 0]], [0.4, [35, 0, 0]], [0.8, [35, 0, 0]], [1.2, [0, 0, 0]]]],
    [elbow, 'Rotation', [[0, [0, 0, 0]], [0.5, [0, 0, 18]], [0.7, [0, 0, -18]], [1.2, [0, 0, 0]]]],
    [body, 'Position', [[0, [0, 0.72, 0]], [0.4, [0, 0.66, 0]], [0.8, [0, 0.66, 0]], [1.2, [0, 0.72, 0]]]],
  ]);
  P.save();
}

// ---------------------------------------------------------------- combat mecha (~2.4 tall)
function warden() {
  const P = new Prefab('Mecha Warden', { category: 'Unit', footprintX: 1, footprintZ: 1, selectionRadius: 1.15, height: 3.0, blocksPath: false });
  const root = P.part(0, 'Root', null, [0, 0, 0], [0, 0, 0], [1.2, 1.2, 1.2]);
  const pelvis = P.pivot(root, 'Pelvis', [0, 1.25, 0]);
  P.box(pelvis, 'Pelvis Block', 0.7, 0.26, 0.5, [0, -0.1, 0], 'steel');
  const torso = P.pivot(pelvis, 'Torso', [0, 0.16, 0]);
  P.box(torso, 'Chest', 1.1, 0.62, 0.78, [0, 0, 0], 'armor');
  P.box(torso, 'Chest Plate', 0.86, 0.38, 0.06, [0, 0.12, 0.4], 'team');
  P.box(torso, 'Chest Stripe', 1.12, 0.07, 0.8, [0, 0.46, 0], 'amber');
  P.box(torso, 'Head', 0.42, 0.26, 0.4, [0, 0.62, 0.08], 'armor');
  P.box(torso, 'Visor', 0.34, 0.06, 0.02, [0, 0.74, 0.29], 'visor');
  P.box(torso, 'Backpack', 0.74, 0.5, 0.3, [0, 0.06, -0.52], 'steel');
  P.hcyl(torso, 'Exhaust L', 0.07, 0.2, [-0.22, 0.58, -0.6], 'z', 'dark');
  P.hcyl(torso, 'Exhaust R', 0.07, 0.2, [0.22, 0.58, -0.6], 'z', 'dark');
  P.part(torso, 'Beacon', sph(0.04), [0.3, 0.66, -0.52], [0, 0, 0], [1, 1, 1], 'red');
  const arms = [];
  for (const s of [-1, 1]) {
    const sh = P.pivot(torso, s < 0 ? 'Shoulder L' : 'Shoulder R', [0.7 * s, 0.38, 0]);
    P.box(sh, 'Pauldron', 0.36, 0.3, 0.5, [0.06 * s, -0.12, 0], 'team');
    P.box(sh, 'Arm', 0.2, 0.5, 0.22, [0.06 * s, -0.6, 0], 'steel');
    // twin cannon
    P.hcyl(sh, 'Cannon', 0.09, 0.9, [0.06 * s, -0.62, 0.3], 'z', 'steel');
    P.hcyl(sh, 'Cannon Muzzle', 0.11, 0.12, [0.06 * s, -0.62, 0.78], 'z', 'dark');
    P.part(sh, 'Muzzle Glow', disk(0.07), [0.06 * s, -0.62, 0.845], [90, 0, 0], [1, 1, 1], 'visor');
    arms.push(sh);
  }
  const legs = [];
  for (const s of [-1, 1]) {
    const hip = P.pivot(root, s < 0 ? 'Hip L' : 'Hip R', [0.34 * s, 1.12, 0]);
    P.box(hip, 'Thigh', 0.24, 0.58, 0.3, [0, -0.6, 0.04], 'armor');
    const knee = P.pivot(hip, 'Knee', [0, -0.6, 0.06]);
    P.part(knee, 'Knee Cap', sph(0.13), [0, -0.12, 0.1], [0, 0, 0], [1, 1, 1], 'armorB');
    P.box(knee, 'Shin', 0.22, 0.5, 0.26, [0, -0.5, -0.04], 'steel');
    P.box(knee, 'Foot', 0.34, 0.1, 0.5, [0, -0.56, 0.06], 'dark');
    legs.push({ hip, knee });
  }
  P.clip('Idle', 3, true, [
    [pelvis, 'Position', [[0, [0, 1.25, 0]], [1.5, [0, 1.21, 0]], [3, [0, 1.25, 0]]]],
    [torso, 'Rotation', [[0, [0, -6, 0]], [1.5, [0, 6, 0]], [3, [0, -6, 0]]]],
  ]);
  P.clip('Walk', 1.1, true, [
    [legs[0].hip, 'Rotation', [[0, [24, 0, 0]], [0.55, [-24, 0, 0]], [1.1, [24, 0, 0]]]],
    [legs[1].hip, 'Rotation', [[0, [-24, 0, 0]], [0.55, [24, 0, 0]], [1.1, [-24, 0, 0]]]],
    [legs[0].knee, 'Rotation', [[0, [0, 0, 0]], [0.27, [-34, 0, 0]], [0.55, [0, 0, 0]], [1.1, [0, 0, 0]]]],
    [legs[1].knee, 'Rotation', [[0, [0, 0, 0]], [0.55, [0, 0, 0]], [0.82, [-34, 0, 0]], [1.1, [0, 0, 0]]]],
    [pelvis, 'Position', [[0, [0, 1.25, 0]], [0.27, [0, 1.3, 0]], [0.55, [0, 1.25, 0]], [0.82, [0, 1.3, 0]], [1.1, [0, 1.25, 0]]]],
    [torso, 'Rotation', [[0, [0, 5, 0]], [0.55, [0, -5, 0]], [1.1, [0, 5, 0]]]],
  ]);
  P.clip('Fire', 0.6, true, [
    [arms[0], 'Position', [[0, [-0.7, 0.38, 0]], [0.05, [-0.7, 0.38, -0.12]], [0.3, [-0.7, 0.38, 0]], [0.6, [-0.7, 0.38, 0]]]],
    [arms[1], 'Position', [[0, [0.7, 0.38, 0]], [0.3, [0.7, 0.38, 0]], [0.35, [0.7, 0.38, -0.12]], [0.6, [0.7, 0.38, 0]]]],
  ]);
  P.save();
}

// ---------------------------------------------------------------- command base (4 x 4 cells)
function bastion() {
  const P = new Prefab('Bastion HQ', { category: 'Building', footprintX: 4, footprintZ: 4, selectionRadius: 2.8, height: 4.2, blocksPath: true });
  const root = P.pivot(0, 'Root');
  P.part(root, 'Foundation', cyl(2.2, 0.3, 8), [0, 0, 0], [0, 22.5, 0], [1, 1, 1], 'concrete');
  P.part(root, 'Foundation Trim', cyl(2.24, 0.07, 8), [0, 0.12, 0], [0, 22.5, 0], [1, 1, 1], 'amber');
  P.part(root, 'Core', cyl(1.5, 1.5, 8), [0, 0.36, 0], [0, 22.5, 0], [1, 1, 1], 'armor');
  P.part(root, 'Core Band', cyl(1.53, 0.16, 8), [0, 1.1, 0], [0, 22.5, 0], [1, 1, 1], 'armorB');
  P.part(root, 'Upper', cyl(1.1, 0.8, 8), [0, 1.86, 0], [0, 22.5, 0], [1, 1, 1], 'armor');
  P.part(root, 'Window Ring', cyl(1.12, 0.12, 8), [0, 2.3, 0], [0, 22.5, 0], [1, 1, 1], 'visor');
  P.part(root, 'Roof', cyl(1.2, 0.12, 8), [0, 2.66, 0], [0, 22.5, 0], [1, 1, 1], 'steel');
  // garage door + lights
  P.box(root, 'Door Frame', 1.3, 1.0, 0.2, [0, 0.36, 1.45], 'steel');
  P.box(root, 'Door', 1.06, 0.84, 0.06, [0, 0.36, 1.56], 'dark');
  P.box(root, 'Door Light', 1.06, 0.05, 0.04, [0, 1.24, 1.58], 'lamp');
  for (const s of [-1, 1]) P.box(root, 'Hazard', 0.08, 0.84, 0.03, [0.6 * s, 0.36, 1.57], 'amber');
  // side towers
  for (const [x, z] of [[1.5, -1.0], [-1.5, -1.0]]) {
    P.part(root, 'Tower', cyl(0.35, 2.4, 12), [x, 0.3, z], [0, 0, 0], [1, 1, 1], 'armorB');
    P.part(root, 'Tower Cap', sph(0.36, 24, 12), [x, 2.52, z], [0, 0, 0], [1, 0.6, 1], 'armor');
    P.part(root, 'Tower Light', tor(0.36, 0.03), [x, 2.4, z], [0, 0, 0], [1, 1, 1], 'lamp');
  }
  // rotating radar on the roof
  const mast = P.part(root, 'Radar Mast', cyl(0.08, 0.7), [0, 2.78, 0], [0, 0, 0], [1, 1, 1], 'steel');
  const radar = P.pivot(root, 'Radar', [0, 3.45, 0]);
  P.box(radar, 'Radar Arm', 0.12, 0.08, 0.7, [0, 0, 0.1], 'steel');
  P.part(radar, 'Radar Dish', sph(0.42, 32, 16), [0, -0.1, 0.5], [-70, 0, 0], [1, 0.35, 1], 'armor');
  P.part(radar, 'Radar Feed', sph(0.05), [0, 0.2, 0.62], [0, 0, 0], [1, 1, 1], 'red');
  // landing pad
  P.part(root, 'Pad', cyl(0.9, 0.06, 24), [-0.2, 2.78, -0.2], [0, 0, 0], [1, 0.0001, 1], 'pad');
  P.clip('Idle', 6, true, [
    [radar, 'Rotation', [[0, [0, 0, 0]], [2, [0, 120, 0]], [4, [0, 240, 0]], [6, [0, 360, 0]]]],
  ]);
  void mast;
  P.save();
}

// ---------------------------------------------------------------- Vitae tank station (resource node, 3 x 3)
function tankStation() {
  const P = new Prefab('Vitae Tanks', { category: 'Resource', footprintX: 3, footprintZ: 3, selectionRadius: 2.0, height: 2.6, blocksPath: true });
  const root = P.pivot(0, 'Root');
  P.box(root, 'Platform', 3.0, 0.2, 3.0, [0, 0, 0], 'pad');
  P.box(root, 'Platform Edge', 3.06, 0.06, 3.06, [0, 0.07, 0], 'amber');
  const cores = [];
  const spots = [[-0.75, -0.75], [0.75, -0.75], [-0.75, 0.75], [0.75, 0.75]];
  spots.forEach(([x, z], i) => {
    const h = i === 3 ? 1.5 : 2.0;
    P.part(root, 'Tank Base', cyl(0.48, 0.18, 24), [x, 0.2, z], [0, 0, 0], [1, 1, 1], 'steel');
    P.part(root, 'Tank Glass', cyl(0.42, h, 32), [x, 0.38, z], [0, 0, 0], [1, 1, 1], 'greenT');
    const core = P.part(root, 'Tank Liquid', cyl(0.36, h * 0.8, 32), [x, 0.4, z], [0, 0, 0], [1, 1, 1], 'green');
    cores.push({ core, h });
    P.part(root, 'Tank Ring', tor(0.44, 0.04), [x, 0.38 + h * 0.33, z], [0, 0, 0], [1, 1, 1], 'steel');
    P.part(root, 'Tank Ring', tor(0.44, 0.04), [x, 0.38 + h * 0.66, z], [0, 0, 0], [1, 1, 1], 'steel');
    P.part(root, 'Tank Cap', sph(0.44, 32, 16), [x, 0.3 + h, z], [0, 0, 0], [1, 0.4, 1], 'steel');
    P.part(root, 'Cap Valve', cyl(0.06, 0.16), [x, 0.48 + h, z], [0, 0, 0], [1, 1, 1], 'amber');
  });
  // pipes between the tanks
  P.hcyl(root, 'Pipe', 0.07, 1.5, [0, 0.62, -0.75], 'x', 'steel');
  P.hcyl(root, 'Pipe', 0.07, 1.5, [0, 0.62, 0.75], 'x', 'steel');
  P.hcyl(root, 'Pipe', 0.07, 1.5, [-0.75, 0.9, 0], 'z', 'steel');
  P.hcyl(root, 'Pipe Glow', 0.035, 1.5, [0.75, 0.9, 0], 'z', 'green');
  P.part(root, 'Pump', cyl(0.22, 0.5, 16), [0, 0.2, 0], [0, 0, 0], [1, 1, 1], 'armorB');
  P.part(root, 'Pump Light', sph(0.08), [0, 0.7, 0], [0, 0, 0], [1, 1, 1], 'green');
  P.clip('Idle', 2.4, true, cores.flatMap(({ core }, i) => [
    [core, 'Scale', [[0, [1, 1, 1]], [0.6 + i * 0.2, [1.04, 0.96, 1.04]], [2.4, [1, 1, 1]]]],
  ]));
  P.save();
}

// ---------------------------------------------------------------- factory (mecha bay, 4 x 3)
function factory() {
  const P = new Prefab('Mecha Factory', { category: 'Building', footprintX: 4, footprintZ: 3, selectionRadius: 2.6, height: 3.4, blocksPath: true });
  const root = P.pivot(0, 'Root');
  P.box(root, 'Slab', 4.0, 0.2, 3.0, [0, 0, 0], 'concrete');
  P.box(root, 'Hall', 3.4, 1.9, 2.4, [0, 0.2, -0.1], 'armor');
  P.box(root, 'Hall Band', 3.44, 0.14, 2.44, [0, 1.5, -0.1], 'armorB');
  P.hcyl(root, 'Roof Vault', 1.2, 3.4, [0, 2.1, -0.1], 'x', 'steel', 32);
  P.box(root, 'Bay Door', 1.6, 1.5, 0.08, [0, 0.2, 1.12], 'dark');
  P.box(root, 'Bay Light', 1.6, 0.06, 0.06, [0, 1.74, 1.14], 'lamp');
  for (const s of [-1, 1]) P.box(root, 'Bay Hazard', 0.1, 1.5, 0.06, [0.86 * s, 0.2, 1.13], 'amber');
  P.part(root, 'Chimney', cyl(0.22, 1.4, 16), [1.3, 2.0, -0.9], [0, 0, 0], [1, 1, 1], 'steel');
  P.part(root, 'Chimney Glow', tor(0.22, 0.03), [1.3, 3.36, -0.9], [0, 0, 0], [1, 1, 1], 'red');
  // crane arm swinging over the yard
  P.part(root, 'Crane Post', cyl(0.1, 1.2, 12), [-1.4, 2.0, 0.8], [0, 0, 0], [1, 1, 1], 'amber');
  const crane = P.pivot(root, 'Crane', [-1.4, 3.2, 0.8]);
  P.box(crane, 'Crane Jib', 0.12, 0.12, 1.6, [0, 0, 0.6], 'amber');
  P.part(crane, 'Crane Hook', cyl(0.015, 0.6), [0, -0.6, 1.3], [0, 0, 0], [1, 1, 1], 'steel');
  P.clip('Idle', 8, true, [
    [crane, 'Rotation', [[0, [0, -30, 0]], [4, [0, 60, 0]], [8, [0, -30, 0]]]],
  ]);
  P.save();
}

// ---------------------------------------------------------------- depot (drop-off point, 2 x 2)
function depot() {
  const P = new Prefab('Vitae Depot', { category: 'Building', footprintX: 2, footprintZ: 2, selectionRadius: 1.4, height: 1.8, blocksPath: true });
  const root = P.pivot(0, 'Root');
  P.box(root, 'Slab', 2.0, 0.16, 2.0, [0, 0, 0], 'concrete');
  P.box(root, 'Body', 1.5, 0.9, 1.4, [0, 0.16, -0.1], 'armor');
  P.box(root, 'Roof', 1.6, 0.1, 1.5, [0, 1.06, -0.1], 'armorB');
  P.part(root, 'Silo', cyl(0.36, 1.2, 24), [0.5, 1.16, -0.3], [0, 0, 0], [1, 1, 1], 'greenT');
  P.part(root, 'Silo Core', cyl(0.28, 1.0, 24), [0.5, 1.2, -0.3], [0, 0, 0], [1, 1, 1], 'green');
  P.part(root, 'Silo Cap', sph(0.38, 24, 12), [0.5, 2.26, -0.3], [0, 0, 0], [1, 0.4, 1], 'steel');
  P.box(root, 'Intake', 0.7, 0.4, 0.3, [-0.3, 0.16, 0.65], 'steel');
  P.box(root, 'Intake Glow', 0.5, 0.05, 0.02, [-0.3, 0.46, 0.81], 'green');
  P.save();
}

// ---------------------------------------------------------------- props
function props() {
  {
    const P = new Prefab('Prop Crates', { category: 'Prop', footprintX: 1, footprintZ: 1, selectionRadius: 0.6, height: 0.8, blocksPath: true });
    const r = P.pivot(0, 'Root');
    P.box(r, 'Crate', 0.5, 0.5, 0.5, [0, 0, 0], 'crate', [0, 8, 0]);
    P.box(r, 'Crate', 0.4, 0.4, 0.4, [0.5, 0, 0.1], 'crate', [0, -14, 0]);
    P.box(r, 'Crate', 0.36, 0.36, 0.36, [0.05, 0.5, 0.02], 'amber', [0, 30, 0]);
    P.box(r, 'Strap', 0.52, 0.06, 0.52, [0, 0.22, 0], 'dark', [0, 8, 0]);
    P.save();
  }
  {
    const P = new Prefab('Prop Lamp', { category: 'Prop', footprintX: 1, footprintZ: 1, selectionRadius: 0.3, height: 1.8, blocksPath: true });
    const r = P.pivot(0, 'Root');
    P.part(r, 'Base', cyl(0.14, 0.1, 12), [0, 0, 0], [0, 0, 0], [1, 1, 1], 'steel');
    P.part(r, 'Pole', cyl(0.035, 1.6, 12), [0, 0.1, 0], [0, 0, 0], [1, 1, 1], 'steel');
    P.box(r, 'Arm', 0.05, 0.05, 0.4, [0, 1.62, 0.15], 'steel');
    const bulb = P.part(r, 'Bulb', sph(0.07), [0, 1.5, 0.32], [0, 0, 0], [1, 0.7, 1], 'lamp');
    P.clip('Idle', 3, true, [[bulb, 'Scale', [[0, [1, 0.7, 1]], [1.5, [1.15, 0.8, 1.15]], [3, [1, 0.7, 1]]]]]);
    P.save();
  }
  {
    const P = new Prefab('Prop Antenna', { category: 'Prop', footprintX: 1, footprintZ: 1, selectionRadius: 0.4, height: 2.6, blocksPath: true });
    const r = P.pivot(0, 'Root');
    P.box(r, 'Base', 0.5, 0.2, 0.5, [0, 0, 0], 'concrete');
    for (let k = 0; k < 4; ++k) {
      const a = k * Math.PI / 2 + Math.PI / 4;
      P.part(r, 'Strut', cyl(0.025, 2.2, 8), [Math.cos(a) * 0.18, 0.2, Math.sin(a) * 0.18], [0, 0, 0], [1, 1, 1], 'steel');
    }
    P.part(r, 'Mast', cyl(0.04, 2.6, 8), [0, 0.2, 0], [0, 0, 0], [1, 1, 1], 'armor');
    const tip = P.part(r, 'Blink', sph(0.06), [0, 2.82, 0], [0, 0, 0], [1, 1, 1], 'red');
    P.clip('Idle', 1.6, true, [[tip, 'Scale', [[0, [1, 1, 1]], [0.2, [1.6, 1.6, 1.6]], [0.4, [1, 1, 1]], [1.6, [1, 1, 1]]]]]);
    P.save();
  }
  {
    const P = new Prefab('Prop Rocks', { category: 'Prop', footprintX: 2, footprintZ: 2, selectionRadius: 1.2, height: 1.0, blocksPath: true });
    const r = P.pivot(0, 'Root');
    P.part(r, 'Rock', sph(0.7, 9, 6), [0, -0.25, 0], [12, 20, 0], [1.2, 0.8, 1.0], 'rock');
    P.part(r, 'Rock', sph(0.45, 8, 5), [0.85, -0.15, 0.3], [-8, 70, 10], [1.0, 0.9, 1.3], 'rock2');
    P.part(r, 'Rock', sph(0.3, 7, 5), [-0.7, -0.08, 0.5], [0, 10, 15], [1.2, 0.8, 1.0], 'rock');
    P.save();
  }
  {
    const P = new Prefab('Prop Crystal', { category: 'Prop', footprintX: 1, footprintZ: 1, selectionRadius: 0.6, height: 1.2, blocksPath: true });
    const r = P.pivot(0, 'Root');
    P.part(r, 'Shard', cyl(0.14, 1.1, 6), [0, 0, 0], [8, 0, 6], [1, 1, 1], 'green');
    P.part(r, 'Shard', cyl(0.1, 0.7, 6), [0.22, 0, 0.1], [-6, 30, -22], [1, 1, 1], 'green');
    P.part(r, 'Shard', cyl(0.09, 0.55, 6), [-0.2, 0, 0.12], [10, 60, 25], [1, 1, 1], 'green');
    P.part(r, 'Base Rock', sph(0.3, 8, 5), [0, -0.12, 0], [0, 0, 0], [1.4, 0.6, 1.2], 'rock');
    P.save();
  }
}

worker();
warden();
bastion();
tankStation();
factory();
depot();
props();
