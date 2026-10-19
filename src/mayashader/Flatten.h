// Compounds flattened for Maya and the preview: the inner nodes of every compound join one flat graph (Hypershade has
// no compounds), with the links through the compound Input / Output nodes rewired to their real ends.
// Flat ids: the root nodes keep theirs, a node inside compound C gets flatId(C) * 1000 + its own id.
#pragma once
#include "game/Graph.h"
#include <vector>

namespace mayashader {

// Where an output pin really comes from in the flat graph: a node pin, or a constant (unconnected compound input).
struct FlatRef {
    int node = 0, pin = -1;
    bool constant = false;
    game::Value value;
    bool valid() const { return constant || node > 0; }
};

game::Graph flatten(const game::Graph& root);

// Output pin `pin` of node `node` in the graph opened at `path` (compound ids from the root), resolved in the flat graph.
FlatRef resolveOut(const game::Graph& root, const std::vector<int>& path, int node, int pin);
// Source of input pin `pin` of node `node` (graph at `path`), or an invalid ref when it is not connected.
FlatRef resolveIn(const game::Graph& root, const std::vector<int>& path, int node, int pin);

} // namespace mayashader
