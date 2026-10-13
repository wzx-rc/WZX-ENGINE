#include "Grid.h"
#include <cmath>

namespace game {

void Grid::init(int cellsX, int cellsZ, float cellSize) {
    m_nx = std::max(1, cellsX);
    m_nz = std::max(1, cellsZ);
    m_size = cellSize > 0 ? cellSize : 1.0f;
    m_owner.assign((size_t)m_nx * m_nz, -1);
}

int Grid::footprintCells(float size) const {
    return std::max(1, (int)std::ceil(size / m_size - 0.2f));
}

Cell Grid::snapOrigin(const float3& p, int w, int d) const {
    // footprint centre = (origin + w/2) * size - half extent  ->  origin = (p + half) / size - w/2
    Cell c;
    c.x = (int)std::lround((p.x + halfExtentX()) / m_size - w * 0.5f);
    c.z = (int)std::lround((p.z + halfExtentZ()) / m_size - d * 0.5f);
    return c;
}

float3 Grid::center(Cell o, int w, int d) const {
    return float3((o.x + w * 0.5f) * m_size - halfExtentX(), 0.0f, (o.z + d * 0.5f) * m_size - halfExtentZ());
}

bool Grid::inside(Cell o, int w, int d) const {
    return o.x >= 0 && o.z >= 0 && o.x + w <= m_nx && o.z + d <= m_nz;
}

bool Grid::canPlace(Cell o, int w, int d, int ignoreOwner) const {
    if (!inside(o, w, d)) return false;
    for (int z = o.z; z < o.z + d; ++z)
        for (int x = o.x; x < o.x + w; ++x) {
            int who = m_owner[(size_t)z * m_nx + x];
            if (who >= 0 && who != ignoreOwner) return false;
        }
    return true;
}

void Grid::place(Cell o, int w, int d, int owner) {
    for (int z = std::max(0, o.z); z < std::min(m_nz, o.z + d); ++z)
        for (int x = std::max(0, o.x); x < std::min(m_nx, o.x + w); ++x) m_owner[(size_t)z * m_nx + x] = owner;
}

void Grid::remove(int owner) {
    for (int& c : m_owner)
        if (c == owner) c = -1;
}

int Grid::owner(int x, int z) const {
    if (x < 0 || z < 0 || x >= m_nx || z >= m_nz) return -1;
    return m_owner[(size_t)z * m_nx + x];
}

bool Grid::findFreeNear(Cell from, int w, int d, Cell& out, int ignoreOwner) const {
    int maxRing = std::max(m_nx, m_nz);
    for (int r = 0; r <= maxRing; ++r) {
        bool found = false;
        float best = 1e30f;
        for (int dz = -r; dz <= r; ++dz)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;   // ring only
                Cell c{from.x + dx, from.z + dz};
                if (!canPlace(c, w, d, ignoreOwner)) continue;
                float dist = (float)(dx * dx + dz * dz);
                if (dist < best) { best = dist; out = c; found = true; }
            }
        if (found) return true;
    }
    return false;
}

int Grid::usedCells() const {
    int n = 0;
    for (int c : m_owner)
        if (c >= 0) ++n;
    return n;
}

} // namespace game
