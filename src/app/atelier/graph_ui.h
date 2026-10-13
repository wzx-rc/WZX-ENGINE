#pragma once
#include <string>
#include "graph_model.h"

namespace atelier::graph {
void Initialize(const std::string& graphFile);
// Draws the complete editor workspace. Returns true when Scene was requested.
bool Draw();
bool WantsMaterials();
void Shutdown();
const Graph& GetGraph();
const RunResult& GetLastRun();
}
