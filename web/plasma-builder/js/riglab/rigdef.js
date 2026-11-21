// Squelette de l'ork (rig ORK_V3 de Maya, converti par tools/ma2glb) : rôle de chaque joint.
// Toute l'app passe par ce fichier pour nommer les os ; un nouveau rig = changer les noms ici.
// Côté gauche de l'ork = +x = suffixe _l (avant = +z).
import * as THREE from 'three';

export const RIG = 'v3';
const S = { L: 'l', R: 'r' };
const side = (base, s) => base + '_' + S[s];

export const B = {
  hips: 'pelvis',
  spine: ['spine_01', 'spine_02', 'spine_03'],   // bas du dos, dos, torse
  neck: 'neck_01',
  head: 'head',
  clav: s => side('clavicle', s),
  arm: s => side('upperarm', s),
  fore: s => side('lowerarm', s),
  hand: s => side('hand', s),
  thigh: s => side('thigh', s),
  calf: s => side('calf', s),
  foot: s => side('foot', s),
  toe: s => side('ball', s),
};

// Doigts : [pouce, index, majeur, annulaire, auriculaire], joints de la base vers le bout (sans les _end).
const F = s => [['thumb_01', 'thumb_02'], ['index_01', 'index_02', 'index_03'], ['middle_01', 'middle_02', 'middle_03'],
  ['ring_01', 'ring_02', 'ring_03'], ['pinky_01', 'pinky_02', 'pinky_03']].map(c => c.map(n => n + '_' + S[s]));
export const FINGERS = { L: F('L'), R: F('R') };
export const isFinger = name => /^(thumb|index|middle|ring|pinky)_/.test(name);

// Chaînes IK à deux os : racine, milieu, effecteur.
export const CHAINS = {
  handL: [B.arm('L'), B.fore('L'), B.hand('L')],
  handR: [B.arm('R'), B.fore('R'), B.hand('R')],
  footL: [B.thigh('L'), B.calf('L'), B.foot('L')],
  footR: [B.thigh('R'), B.calf('R'), B.foot('R')],
};

// Nom du joint tel qu'affiché comme « nom Maya ».
export const mayaName = name => name;

// ---------------------------------------------------------------- anciennes poses (rig QuickRig de BOY_32mm)
// Poses enregistrées avec l'ancien squelette (Mes poses, projets, presets de main) : nom -> [nouveau nom,
// rotation monde de repos de l'ancien joint (x, y, z, w)]. La rotation relative au repos est réexprimée dans
// le repère du nouveau joint pour garder le même geste.
const LEGACY = {"QuickRigCharacter_Hips":["pelvis",0.52751,0.47089,0.52751,0.47089],"QuickRigCharacter_Spine":["spine_01",0.44969,0.54569,0.44969,0.54569],"QuickRigCharacter_Spine1":["spine_02",0.4954,0.50456,0.4954,0.50456],"QuickRigCharacter_Spine2":["spine_03",0.66219,-0.24801,0.66219,-0.24801],"QuickRigCharacter_Neck":["neck_01",0.69939,0.10417,0.69939,0.10417],"QuickRigCharacter_Head":["head",0.69939,0.10417,0.69939,0.10417],"QuickRigCharacter_LeftArm":["upperarm_l",0.59177,-0.32058,0.33062,-0.66161],"QuickRigCharacter_LeftForeArm":["lowerarm_l",-0.01202,0.32388,0.48229,-0.81385],"QuickRigCharacter_LeftHand":["hand_l",-0.01202,0.32388,0.48229,-0.81385],"QuickRigCharacter_LeftUpLeg":["thigh_l",-0.56626,0.45291,0.30284,-0.61847],"QuickRigCharacter_LeftLeg":["calf_l",-0.06573,0.2561,-0.58881,0.76381],"QuickRigCharacter_LeftFoot":["foot_l",-0.32139,-0.3844,-0.55083,0.66748],"QuickRigCharacter_LeftToeBase":["ball_l",-0.39398,-0.33594,-0.50241,0.69247],"QuickRigCharacter_LeftArm1":["upperarm_r",0.33062,-0.66161,0.59177,-0.32058],"QuickRigCharacter_LeftForeArm1":["lowerarm_r",0.48229,-0.81385,-0.01203,0.32389],"QuickRigCharacter_LeftHand1":["hand_r",0.48229,-0.81385,-0.01203,0.32389],"QuickRigCharacter_LeftUpLeg1":["thigh_r",-0.34018,0.67846,0.51492,-0.39853],"QuickRigCharacter_LeftLeg1":["calf_r",-0.58557,0.73421,-0.14816,0.30999],"QuickRigCharacter_LeftFoot1":["foot_r",-0.50241,0.69247,-0.39398,-0.33594],"QuickRigCharacter_LeftToeBase1":["ball_r",-0.50241,0.69247,-0.39398,-0.33594],"joint40":["clavicle_l",0.0,-0.0,0.0,-1.0],"joint41":["clavicle_r",1.0,-0.0,0.0,-0.0],"joint1":["thumb_01_l",0.13921,0.55077,0.69021,-0.4482],"joint2":["thumb_02_l",-0.17971,0.29641,0.68078,-0.64527],"joint5":["index_01_l",-0.54498,0.693,0.04401,-0.46991],"joint6":["index_02_l",-0.48698,0.81985,0.24859,-0.16999],"joint7":["index_03_l",-0.38885,0.77616,0.46341,-0.17784],"joint9":["middle_01_l",-0.53475,0.65078,0.1533,-0.51675],"joint10":["middle_02_l",-0.4551,0.78408,0.31991,-0.27523],"joint11":["middle_03_l",-0.3836,0.8283,0.39771,-0.09273],"joint13":["ring_01_l",-0.50952,0.60499,0.29116,-0.53815],"joint14":["ring_02_l",-0.36648,0.76066,0.45834,-0.27752],"joint15":["ring_03_l",-0.23922,0.75616,0.58135,-0.18175],"joint17":["pinky_01_l",-0.49619,0.58937,0.30729,-0.55858],"joint18":["pinky_02_l",-0.35713,0.74768,0.46162,-0.31676],"joint19":["pinky_03_l",-0.14664,0.73228,0.63545,-0.19614],"joint37":["thumb_01_r",-0.4482,-0.69021,0.55077,-0.13921],"joint38":["thumb_02_r",-0.64527,-0.68078,0.29641,0.17971],"joint33":["index_01_r",-0.46991,-0.04401,0.693,0.54498],"joint34":["index_02_r",-0.16999,-0.24859,0.81985,0.48698],"joint35":["index_03_r",-0.17784,-0.46341,0.77616,0.38885],"joint29":["middle_01_r",-0.51675,-0.1533,0.65078,0.53475],"joint30":["middle_02_r",-0.27523,-0.31991,0.78408,0.4551],"joint31":["middle_03_r",-0.09273,-0.39771,0.8283,0.3836],"joint25":["ring_01_r",-0.53815,-0.29116,0.60499,0.50952],"joint26":["ring_02_r",-0.27752,-0.45834,0.76066,0.36648],"joint27":["ring_03_r",-0.18175,-0.58135,0.75616,0.23922],"joint21":["pinky_01_r",-0.55858,-0.30729,0.58937,0.49619],"joint22":["pinky_02_r",-0.31676,-0.46162,0.74768,0.35713],"joint23":["pinky_03_r",-0.19614,-0.63545,0.73228,0.14664]};
export const isLegacyPose = pose => !!pose?.rot && Object.keys(pose.rot).some(k => k in LEGACY);
const _a = new THREE.Quaternion(), _b = new THREE.Quaternion();
// restWQ : nom -> Quaternion de repos monde du nouveau squelette.
export function migratePose(pose, restWQ) {
  if (!isLegacyPose(pose)) return pose;
  const rot = {};
  for (const [k, o] of Object.entries(pose.rot)) {
    const L = LEGACY[k];
    if (!L) { rot[k] = o; continue; }
    const rn = restWQ(L[0]);
    if (!rn) continue;
    _a.fromArray(L.slice(1));
    // delta monde = Ro · o · Ro⁻¹ ; nouvel offset = Rn⁻¹ · delta · Rn
    const q = _a.clone().multiply(_b.fromArray(o)).multiply(_a.clone().invert());
    rot[L[0]] = rn.clone().invert().multiply(q).multiply(rn).toArray();
  }
  return { ...pose, rot };
}

// Rôles nommés à la manière de l'ancien rig (QuickRig : « Left… » = gauche de l'ork, « Left…1 » = droite),
// utilisés par les presets de pose : Q('LeftArm1') -> upperarm_r.
const ROLE = { Hips: B.hips, Spine: B.spine[0], Spine1: B.spine[1], Spine2: B.spine[2], Neck: B.neck, Head: B.head };
for (const [s, k] of [['L', ''], ['R', '1']]) Object.assign(ROLE, {
  ['LeftShoulder' + k]: B.clav(s), ['LeftArm' + k]: B.arm(s), ['LeftForeArm' + k]: B.fore(s), ['LeftHand' + k]: B.hand(s),
  ['LeftUpLeg' + k]: B.thigh(s), ['LeftLeg' + k]: B.calf(s), ['LeftFoot' + k]: B.foot(s), ['LeftToeBase' + k]: B.toe(s),
});
export const Q = n => { if (!ROLE[n]) throw new Error('rôle inconnu : ' + n); return ROLE[n]; };

// Échelles de figurine (mm aux yeux) et échelle de la tête : en 40 mm (Nob) et 50 mm (Boss) la tête grandit un peu
// moins que le corps, comme sur les figurines de plus grande taille.
export const SCALES = [
  { mm: 32, label: 'Boy', head: 1 },
  { mm: 40, label: 'Nob', head: 0.94 },
  { mm: 50, label: 'Boss', head: 0.88 },
];
export const scaleOf = mm => SCALES.reduce((a, b) => (Math.abs(b.mm - mm) < Math.abs(a.mm - mm) ? b : a));
