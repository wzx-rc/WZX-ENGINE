// RTS placement grid: square cells on the ground (XZ), centred on the origin, with an owner per cell.
// A building occupies a rectangle of cells (its footprint), addressed by its lower-left cell (origin).
#pragma once
#include "vp3d/Math.h"
#include <vector>

namespace game {

using vp3d::float3;

struct Cell { int x = 0, z = 0; };

class Grid {
public:
    void init(int cellsX, int cellsZ, float cellSize);

    int cellsX() const { return m_nx; }
    int cellsZ() const { return m_nz; }
    float cellSize() const { return m_size; }
    float halfExtentX() const { return m_nx * m_size * 0.5f; }
    float halfExtentZ() const { return m_nz * m_size * 0.5f; }

    // Footprint in cells of an object of the given size (metres): ceil(size / cell - 0.2), at least 1.
    int footprintCells(float size) const;
    // Lower-left cell of a w x d footprint whose centre is the closest to the world point.
    Cell snapOrigin(const float3& p, int w, int d) const;
    // World centre (y = 0) of a footprint placed at the origin cell.
    float3 center(Cell origin, int w, int d) const;

    bool inside(Cell origin, int w, int d) const;
    // Inside the grid and every cell free or already owned by `ignoreOwner`.
    bool canPlace(Cell origin, int w, int d, int ignoreOwner = -1) const;
    void place(Cell origin, int w, int d, int owner);
    void remove(int owner);
    int owner(int x, int z) const;
    // Free origin closest to `from` (search by growing square rings). Returns false when the grid is full.
    bool findFreeNear(Cell from, int w, int d, Cell& out, int ignoreOwner = -1) const;
    int usedCells() const;

private:
    int m_nx = 20, m_nz = 20;
    float m_size = 1.0f;
    std::vector<int> m_owner;   // -1 = free
};

} // namespace game
