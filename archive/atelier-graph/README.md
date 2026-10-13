# Archive : ancien graphe Atelier (BP_AncientCoast)

Ces fichiers sont l'ancien éditeur de graphe de l'espace Atelier « Graph » : son modèle, son interface,
sa documentation et le fichier de l'éditeur de référence qui l'appelait. Ils ne sont plus compilés ni utilisés.

- L'onglet Graph est maintenant l'éditeur nodal du jeu : `src/game/NodeEditor.*` (éditeur), `src/game/NodeLib.cpp` (nœuds), `src/game/Runtime.*` (exécution).
- Les sources actives (`graph_model.*`, `graph_ui.*`) avaient été retirées de `src/app/atelier/` au commit `bbbb0d2` (branche `graph-unify`).
- Les fichiers de cette archive ont été déplacés avec `git mv` : leur historique est conservé (`git log --follow <fichier>`).

Pour retrouver une version : `git show bbbb0d2^:src/app/atelier/graph_ui.cpp` (ou le fichier équivalent), ou ouvrir le fichier ici.
