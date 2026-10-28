// Poses de départ de l'ork riggé, partagées par le Rig Lab et le builder.
// Construites par rotations dans l'espace de la figurine (gauche = +x, avant = +z, cm).
import { CHAINS } from './model.js';

const HIPS = 'QuickRigCharacter_Hips';
// Clé localStorage des poses enregistrées dans le Rig Lab (« Mes poses »), lues aussi par le builder.
export const MY_POSES = 'plasmaork.riglab.poses';

// Les bras du rig sont au repos en A ; le genou et le coude plient autour de x.
const X = [1, 0, 0], Y = [0, 1, 0], Z = [0, 0, 1];
export const PRESETS = [
  { id: 'repos', name: 'Repos', build: () => {} },
  { id: 'garde', name: 'Garde', curl: [1.1, 1.1], build: m => {
    m.hips.position.y -= 0.9;
    for (const s of ['', '1']) {
      m.rotateWorld('QuickRigCharacter_LeftUpLeg' + s, X, -18);
      m.rotateWorld('QuickRigCharacter_LeftLeg' + s, X, 30);
      m.rotateWorld('QuickRigCharacter_LeftFoot' + s, X, -12);
    }
    m.rotateWorld('QuickRigCharacter_Spine1', X, 8);
    m.rotateWorld('QuickRigCharacter_LeftArm', X, -25); m.rotateWorld('QuickRigCharacter_LeftForeArm', X, -55);
    m.rotateWorld('QuickRigCharacter_LeftArm1', X, -25); m.rotateWorld('QuickRigCharacter_LeftForeArm1', X, -55);
  } },
  { id: 'charge', name: 'Charge', curl: [1.4, 1.2], build: m => {
    m.hips.position.y -= 0.6; m.hips.position.z += 1.2;
    m.rotateWorld(HIPS, X, 10);
    m.rotateWorld('QuickRigCharacter_Spine', X, 12);
    m.rotateWorld('QuickRigCharacter_Neck', X, -8);
    m.rotateWorld('QuickRigCharacter_LeftUpLeg', X, -55); m.rotateWorld('QuickRigCharacter_LeftLeg', X, 60);
    m.rotateWorld('QuickRigCharacter_LeftUpLeg1', X, 20); m.rotateWorld('QuickRigCharacter_LeftLeg1', X, 45);
    m.rotateWorld('QuickRigCharacter_LeftFoot1', X, 25);
    m.rotateWorld('QuickRigCharacter_LeftArm', X, -95); m.rotateWorld('QuickRigCharacter_LeftForeArm', X, -30);
    m.rotateWorld('QuickRigCharacter_LeftArm1', X, 35); m.rotateWorld('QuickRigCharacter_LeftForeArm1', X, -70);
  } },
  { id: 'waaagh', name: 'Waaagh !', curl: [1.5, 1.5], build: m => {
    m.rotateWorld('QuickRigCharacter_Spine2', X, -10);
    m.rotateWorld('QuickRigCharacter_Neck', X, -22);
    m.rotateWorld('QuickRigCharacter_LeftArm', Z, 105); m.rotateWorld('QuickRigCharacter_LeftForeArm', Z, 35);
    m.rotateWorld('QuickRigCharacter_LeftArm1', Z, -105); m.rotateWorld('QuickRigCharacter_LeftForeArm1', Z, -35);
    m.rotateWorld('QuickRigCharacter_LeftUpLeg', Z, 8); m.rotateWorld('QuickRigCharacter_LeftUpLeg1', Z, -8);
    m.hips.position.y -= 0.4;
  } },
  { id: 'tir', name: 'Tir', curl: [1.2, 0.9], build: m => {
    m.rotateWorld('QuickRigCharacter_Spine1', Y, 18);
    m.rotateWorld('QuickRigCharacter_Neck', Y, -14);
    m.rotateWorld('QuickRigCharacter_LeftArm1', X, -70); m.rotateWorld('QuickRigCharacter_LeftArm1', Y, 15);
    m.rotateWorld('QuickRigCharacter_LeftForeArm1', X, -15);
    m.rotateWorld('QuickRigCharacter_LeftArm', X, -55); m.rotateWorld('QuickRigCharacter_LeftArm', Y, -30);
    m.rotateWorld('QuickRigCharacter_LeftForeArm', X, -50);
    m.rotateWorld('QuickRigCharacter_LeftUpLeg', X, -15); m.rotateWorld('QuickRigCharacter_LeftLeg', X, 18);
    m.rotateWorld('QuickRigCharacter_LeftUpLeg1', X, 12); m.rotateWorld('QuickRigCharacter_LeftLeg1', X, 10);
  } },
];

// Applique une pose de départ (pieds reposés au sol) et rend la fermeture des mains { L, R }.
export function buildPreset(model, p) {
  model.setPose(null);
  p.build(model);
  model.root.updateMatrixWorld(true);
  // les pieds restent posés : on ramène les chevilles à hauteur du repos sans changer leur x / z
  if (p.id !== 'repos') for (const id of ['footL', 'footR']) {
    const end = model.bone(CHAINS[id][2]), t = model.figurePos(end);
    t.y = end.userData.restW.y;
    model.solveIK(id, t);
  }
  const curl = { L: p.curl?.[0] || 0, R: p.curl?.[1] || 0 };
  model.curl('L', curl.L); model.curl('R', curl.R);
  return curl;
}

export function loadMyPoses() {
  try { return JSON.parse(localStorage.getItem(MY_POSES) || '[]'); } catch { return []; }
}
