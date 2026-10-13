// Node type registry: every node available in the graph editor and the command API.
#include "Graph.h"

namespace game {

namespace {

struct B {
    NodeDef d;
    B(Op op, const char* id, const char* label, const char* cat, const char* help) {
        d.op = op;
        d.id = id;
        d.label = label;
        d.category = cat;
        d.help = help;
    }
    B& execIn() { return in("", PinType::Exec); }
    B& execOut(const char* n = "") { d.outs.push_back({n, PinType::Exec}); return *this; }
    B& in(const char* n, PinType t) { d.ins.push_back({n, t}); d.defaults.push_back(Value::None(t)); return *this; }
    B& inF(const char* n, float v) { d.ins.push_back({n, PinType::Float}); d.defaults.push_back(Value::Float(v)); return *this; }
    B& inB(const char* n, bool v) { d.ins.push_back({n, PinType::Bool}); d.defaults.push_back(Value::Bool(v)); return *this; }
    B& inV(const char* n, float3 v) { d.ins.push_back({n, PinType::Vec3}); d.defaults.push_back(Value::Vec3(v)); return *this; }
    B& inS(const char* n, const char* v) { d.ins.push_back({n, PinType::String}); d.defaults.push_back(Value::Str(v)); return *this; }
    B& out(const char* n, PinType t) { d.outs.push_back({n, t}); return *this; }
    B& pure() { d.pure = true; return *this; }
    B& event() { d.event = true; return *this; }
};

std::vector<NodeDef> build() {
    const PinType F = PinType::Float, Bo = PinType::Bool, V = PinType::Vec3, O = PinType::Object, S = PinType::String;
    const char* EV = "Événements";
    const char* FL = "Flux";
    const char* AC = "Actions";
    const char* DA = "Données";
    const char* MA = "Maths";
    const char* RT = "RTS";
    std::vector<B> v;
    // events
    v.push_back(B(Op::OnStart, "event.start", "Au démarrage", EV, "Déclenché une fois quand Play démarre.").event().execOut());
    v.push_back(B(Op::OnTick, "event.tick", "À chaque frame", EV, "Déclenché à chaque image ; Delta = secondes écoulées.").event().execOut().out("Delta", F));
    v.push_back(B(Op::OnClickObject, "event.clickObject", "Clic sur un objet", EV, "Clic gauche sur un objet pendant Play.")
                    .event().execOut().out("Objet", O).out("Point", V));
    v.push_back(B(Op::OnClickGround, "event.clickGround", "Clic au sol", EV, "Clic gauche dans le vide (plan Y = 0) pendant Play.")
                    .event().execOut().out("Point", V));
    v.push_back(B(Op::OnKey, "event.key", "Touche pressée", EV, "Touche du clavier (lettre, chiffre, ESPACE, ENTREE, F1..F12, HAUT...).")
                    .event().inS("Touche", "R").execOut());
    v.push_back(B(Op::OnCustom, "event.custom", "Événement personnalisé", EV, "Déclenché par le noeud « Appeler l'événement » du même nom.")
                    .event().inS("Nom", "MonEvenement").execOut());
    // flow
    v.push_back(B(Op::Branch, "flow.branch", "Si", FL, "Continue par Vrai ou Faux selon la condition.")
                    .execIn().inB("Condition", true).execOut("Vrai").execOut("Faux"));
    v.push_back(B(Op::Sequence, "flow.sequence", "Séquence", FL, "Exécute les sorties dans l'ordre.")
                    .execIn().execOut("Puis 1").execOut("Puis 2").execOut("Puis 3"));
    v.push_back(B(Op::Delay, "flow.delay", "Délai", FL, "Attend un nombre de secondes puis continue.")
                    .execIn().inF("Secondes", 1.0f).execOut());
    v.push_back(B(Op::ForEachPart, "flow.forEachPart", "Pour chaque objet", FL, "Boucle sur les objets visibles d'un type (vide = tous).")
                    .execIn().inS("Type", "CUBE").execOut("Boucle").out("Objet", O).execOut("Terminé"));
    v.push_back(B(Op::DoOnce, "flow.doOnce", "Une seule fois", FL, "Ne laisse passer l'exécution qu'une fois par partie.")
                    .execIn().execOut());
    v.push_back(B(Op::FireCustom, "flow.fireCustom", "Appeler l'événement", FL, "Déclenche les « Événement personnalisé » portant ce nom.")
                    .execIn().inS("Nom", "MonEvenement").execOut());
    // actions
    v.push_back(B(Op::SetPosition, "action.setPosition", "Placer l'objet", AC, "Téléporte l'objet à une position.")
                    .execIn().in("Objet", O).inV("Position", float3(0, 0, 0)).execOut());
    v.push_back(B(Op::MoveTo, "action.moveTo", "Déplacer vers", AC, "Déplace l'objet vers la cible à vitesse constante (unités/s). Arrivé se déclenche à l'arrivée.")
                    .execIn().in("Objet", O).inV("Cible", float3(0, 0, 0)).inF("Vitesse", 20.0f).execOut().execOut("Arrivé"));
    v.push_back(B(Op::RotateY, "action.rotate", "Tourner", AC, "Tourne l'objet autour de l'axe vertical (degrés).")
                    .execIn().in("Objet", O).inF("Degrés", 45.0f).execOut());
    v.push_back(B(Op::Spawn, "action.spawn", "Créer un objet", AC, "Crée un objet de la bibliothèque (CUBE, SPHERE, BALL).")
                    .execIn().inS("Type", "CUBE").inV("Position", float3(0, 0, 0)).inV("Couleur", float3(0.8f, 0.2f, 0.2f)).execOut().out("Objet", O));
    v.push_back(B(Op::Destroy, "action.destroy", "Détruire l'objet", AC, "Retire l'objet de la partie (restauré au Stop).")
                    .execIn().in("Objet", O).execOut());
    v.push_back(B(Op::SetColor, "action.setColor", "Changer la couleur", AC, "Couleur de base RVB entre 0 et 1.")
                    .execIn().in("Objet", O).inV("Couleur", float3(1, 0.8f, 0.2f)).execOut());
    v.push_back(B(Op::SetScale, "action.setScale", "Changer l'échelle", AC, "Échelle de l'objet (1 = taille normale).")
                    .execIn().in("Objet", O).inV("Échelle", float3(1, 1, 1)).execOut());
    v.push_back(B(Op::Select, "action.select", "Sélectionner", AC, "Met l'objet en surbrillance (aucun objet = désélectionne).")
                    .execIn().in("Objet", O).execOut());
    v.push_back(B(Op::Print, "action.print", "Afficher un message", AC, "Affiche un texte à l'écran et dans le journal.")
                    .execIn().inS("Texte", "Bonjour").execOut());
    v.push_back(B(Op::SetVar, "action.setVar", "Définir une variable", AC, "Stocke une valeur sous un nom (variables globales de la partie).")
                    .execIn().inS("Nom", "score").inF("Valeur", 0).execOut());
    v.push_back(B(Op::AddVar, "action.addVar", "Ajouter à une variable", AC, "Ajoute une quantité à une variable numérique.")
                    .execIn().inS("Nom", "score").inF("Quantité", 1).execOut());
    // data
    v.push_back(B(Op::ConstFloat, "data.float", "Nombre", DA, "Valeur numérique constante.").pure().inF("Valeur", 1).out("Valeur", F));
    v.push_back(B(Op::ConstBool, "data.bool", "Booléen", DA, "Vrai ou faux.").pure().inB("Valeur", true).out("Valeur", Bo));
    v.push_back(B(Op::ConstVec3, "data.vec3", "Vecteur", DA, "Vecteur X, Y, Z constant.").pure().inV("Valeur", float3(0, 0, 0)).out("Valeur", V));
    v.push_back(B(Op::ConstString, "data.string", "Texte", DA, "Chaîne de caractères constante.").pure().inS("Valeur", "").out("Valeur", S));
    v.push_back(B(Op::GetVar, "data.getVar", "Lire une variable", DA, "Valeur d'une variable (0 si elle n'existe pas).").pure().inS("Nom", "score").out("Valeur", F));
    v.push_back(B(Op::Selected, "data.selected", "Objet sélectionné", DA, "L'objet actuellement sélectionné.").pure().out("Objet", O));
    v.push_back(B(Op::FindObject, "data.findObject", "Trouver un objet", DA, "Premier objet portant ce nom.").pure().inS("Nom", "Cube").out("Objet", O));
    v.push_back(B(Op::GetPosition, "data.getPosition", "Position de l'objet", DA, "Position de l'objet.").pure().in("Objet", O).out("Position", V));
    v.push_back(B(Op::PartOf, "data.partOf", "Type de l'objet", DA, "Identifiant de bibliothèque (CUBE, SPHERE...).").pure().in("Objet", O).out("Type", S));
    v.push_back(B(Op::IsValid, "data.isValid", "Objet valide ?", DA, "Vrai si l'objet existe et n'a pas été détruit.").pure().in("Objet", O).out("Valide", Bo));
    v.push_back(B(Op::MakeVec3, "data.makeVec3", "Composer un vecteur", DA, "X, Y, Z vers vecteur.").pure().inF("X", 0).inF("Y", 0).inF("Z", 0).out("Vecteur", V));
    v.push_back(B(Op::BreakVec3, "data.breakVec3", "Décomposer un vecteur", DA, "Vecteur vers X, Y, Z.").pure().inV("Vecteur", float3(0, 0, 0)).out("X", F).out("Y", F).out("Z", F));
    v.push_back(B(Op::Distance, "data.distance", "Distance", DA, "Distance entre deux points.").pure().inV("A", float3(0, 0, 0)).inV("B", float3(0, 0, 0)).out("Distance", F));
    v.push_back(B(Op::Time, "data.time", "Temps de jeu", DA, "Secondes écoulées depuis Play.").pure().out("Secondes", F));
    // maths / logic
    v.push_back(B(Op::Add, "math.add", "A + B", MA, "Addition.").pure().inF("A", 0).inF("B", 0).out("Résultat", F));
    v.push_back(B(Op::Sub, "math.sub", "A - B", MA, "Soustraction.").pure().inF("A", 0).inF("B", 0).out("Résultat", F));
    v.push_back(B(Op::Mul, "math.mul", "A × B", MA, "Multiplication.").pure().inF("A", 1).inF("B", 1).out("Résultat", F));
    v.push_back(B(Op::Div, "math.div", "A ÷ B", MA, "Division (0 si B = 0).").pure().inF("A", 1).inF("B", 1).out("Résultat", F));
    v.push_back(B(Op::Greater, "math.greater", "A > B", MA, "Comparaison.").pure().inF("A", 0).inF("B", 0).out("Résultat", Bo));
    v.push_back(B(Op::Less, "math.less", "A < B", MA, "Comparaison.").pure().inF("A", 0).inF("B", 0).out("Résultat", Bo));
    v.push_back(B(Op::Equal, "math.equal", "A = B", MA, "Égalité numérique.").pure().inF("A", 0).inF("B", 0).out("Résultat", Bo));
    v.push_back(B(Op::StrEqual, "math.strEqual", "Textes égaux", MA, "Compare deux textes.").pure().inS("A", "").inS("B", "").out("Résultat", Bo));
    v.push_back(B(Op::And, "math.and", "ET", MA, "Vrai si les deux sont vrais.").pure().inB("A", true).inB("B", true).out("Résultat", Bo));
    v.push_back(B(Op::Or, "math.or", "OU", MA, "Vrai si l'un des deux est vrai.").pure().inB("A", false).inB("B", false).out("Résultat", Bo));
    v.push_back(B(Op::Not, "math.not", "NON", MA, "Inverse un booléen.").pure().inB("A", false).out("Résultat", Bo));
    v.push_back(B(Op::Random, "math.random", "Aléatoire", MA, "Nombre aléatoire entre Min et Max (nouveau à chaque lecture).").pure().inF("Min", 0).inF("Max", 1).out("Valeur", F));
    v.push_back(B(Op::AddVec3, "math.addVec3", "Vecteur A + B", MA, "Somme de deux vecteurs.").pure().inV("A", float3(0, 0, 0)).inV("B", float3(0, 0, 0)).out("Résultat", V));
    v.push_back(B(Op::ScaleVec3, "math.scaleVec3", "Vecteur × nombre", MA, "Multiplie un vecteur.").pure().inV("Vecteur", float3(0, 0, 0)).inF("Facteur", 1).out("Résultat", V));
    v.push_back(B(Op::Concat, "math.concat", "Assembler du texte", MA, "Texte A suivi de B (accepte nombres et vecteurs).").pure().inS("A", "").inS("B", "").out("Texte", S));
    // RTS
    v.push_back(B(Op::SnapGrid, "rts.snap", "Aligner sur la grille", RT, "Arrondit X et Z au pas de grille (Y inchangé).").pure().inV("Position", float3(0, 0, 0)).inF("Pas", 6).out("Position", V));
    v.push_back(B(Op::CellFree, "rts.cellFree", "Case libre ?", RT, "Vrai si aucun objet visible n'occupe la case (rayon = demi-pas).")
                    .pure().inV("Position", float3(0, 0, 0)).inF("Pas", 6).out("Libre", Bo));
    v.push_back(B(Op::CountPart, "rts.count", "Compter les objets", RT, "Nombre d'objets visibles d'un type (vide = tous).").pure().inS("Type", "CUBE").out("Nombre", F));
    std::vector<NodeDef> out;
    for (auto& b : v) out.push_back(b.d);
    return out;
}

} // namespace

const std::vector<NodeDef>& nodeDefs() {
    static const std::vector<NodeDef> defs = build();
    return defs;
}

} // namespace game
