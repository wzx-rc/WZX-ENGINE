// BrickIt - scene export to Maya (port of the web maya_export.js + the shared maya_template.py): part meshes, brick
// matrices, shaders, rig (one group per rigid body, base + driven group per joint, pivot on the axle) and set driven
// keys sampled from the kinematic solver, closed loops included. The data is JSON, zlib-compressed and base64-encoded
// into the Python template (assets/brickit/maya_template.py, identical to the MAYA_TEMPLATE string of the web).
#include "BrickApp.h"
#include "Deflate.h"
#include <algorithm>
#include <cmath>
#include <commdlg.h>
#include <cstdio>
#include <map>
#include <random>
#include <set>

namespace brickit {

static double r6(double x) { return std::round(x * 1e6) / 1e6; }
static double r4(double x) { return std::round(x * 1e4) / 1e4; }
// JSON.stringify of a number (shortest form for these rounded values)
static std::string jn(double x) {
    if (x == 0) return "0";
    char b[40];
    snprintf(b, sizeof(b), "%.12g", x);
    return b;
}
static std::string js(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(c); }
        else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o.push_back(c);
    }
    return o + "\"";
}

// THREE.Quaternion.setFromUnitVectors(X, u) then THREE.Euler.setFromQuaternion(q, "ZYX"), in degrees
static void eulerZYX(const float4& q, double out[3]) {
    const double x = q.x, y = q.y, z = q.z, w = q.w;
    const double m11 = 1 - 2 * (y * y + z * z), m12 = 2 * (x * y - z * w);
    const double m21 = 2 * (x * y + z * w), m22 = 1 - 2 * (x * x + z * z);
    const double m31 = 2 * (x * z - y * w), m32 = 2 * (y * z + x * w), m33 = 1 - 2 * (x * x + y * y);
    double ex, ey, ez;
    ey = std::asin(-std::clamp(m31, -1.0, 1.0));
    if (std::fabs(m31) < 0.9999999) { ex = std::atan2(m32, m33); ez = std::atan2(m21, m11); }
    else { ex = 0; ez = std::atan2(-m12, m22); }
    const double k = 180.0 / 3.14159265358979323846;
    out[0] = ex * k; out[1] = ey * k; out[2] = ez * k;
}
static float4 fromUnitVectors(const float3& from, const float3& to) {
    double r = dot(from, to) + 1;
    float4 q;
    if (r < 1e-8) {
        r = 0;
        if (std::fabs(from.x) > std::fabs(from.z)) q = float4(-from.y, from.x, 0, (float)r);
        else q = float4(0, -from.z, from.y, (float)r);
    } else {
        float3 c = cross(from, to);
        q = float4(c.x, c.y, c.z, (float)r);
    }
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return float4(q.x / l, q.y / l, q.z / l, q.w / l);
}

bool BrickApp::mayaExportScript(std::string& py, MayaStats& st, std::string& err) {
    if (m_bricks.empty()) { err = "Scène vide"; return false; }
    MechModel m;
    if (!m_mech.build(m_bricks, m)) { err = "Mécanisme non construit"; return false; }
    // --- meshes (local space of the part, vertices merged by position + normal)
    std::string jm = "{";
    std::set<int> partsUsed;
    for (const Brick& b : m_bricks) partsUsed.insert(b.part);
    bool firstMesh = true;
    for (int pi : partsUsed) {
        Part* p = m_lib.get(m_lib.parts[pi].name);
        if (!p) continue;
        std::map<std::string, int> map;
        std::vector<double> v;
        std::vector<int> remap(p->mesh.vertices.size());
        for (size_t k = 0; k < p->mesh.vertices.size(); ++k) {
            const auto& vx = p->mesh.vertices[k];
            char key[160];
            snprintf(key, sizeof(key), "%ld,%ld,%ld|%ld,%ld,%ld", std::lround(vx.pos.x * 1e4), std::lround(vx.pos.y * 1e4), std::lround(vx.pos.z * 1e4),
                     std::lround(vx.nrm.x * 100), std::lround(vx.nrm.y * 100), std::lround(vx.nrm.z * 100));
            auto it = map.find(key);
            int id;
            if (it == map.end()) { id = (int)(v.size() / 3); map[key] = id; v.push_back(r4(vx.pos.x)); v.push_back(r4(vx.pos.y)); v.push_back(r4(vx.pos.z)); }
            else id = it->second;
            remap[k] = id;
        }
        std::string sv = "[", si = "[";
        for (size_t k = 0; k < v.size(); ++k) sv += (k ? "," : "") + jn(v[k]);
        bool first = true;
        for (size_t t = 0; t + 2 < p->mesh.indices.size(); t += 3) {
            int a = remap[p->mesh.indices[t]], b = remap[p->mesh.indices[t + 1]], c = remap[p->mesh.indices[t + 2]];
            if (a == b || b == c || a == c) continue;
            si += (first ? "" : ",") + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c);
            first = false;
        }
        jm += (firstMesh ? "" : ",") + js(p->name) + ":{\"v\":" + sv + "],\"i\":" + si + "]}";
        firstMesh = false;
    }
    jm += "}";
    // --- colours used
    std::map<int, int> colorsUsed;   // LDraw id -> palette index
    for (const Brick& b : m_bricks) colorsUsed[m_lib.palette[b.color].id] = b.color;
    std::string jc = "{";
    bool fc = true;
    for (auto& [id, ci] : colorsUsed) {
        const Color& c = m_lib.palette[ci];
        char rgb[64];
        snprintf(rgb, sizeof(rgb), "[%d,%d,%d]", (int)std::lround(c.srgb.x * 255), (int)std::lround(c.srgb.y * 255), (int)std::lround(c.srgb.z * 255));
        jc += (fc ? "" : ",") + js(std::to_string(id)) + ":{\"name\":" + js((c.name.empty() ? std::string("c") : c.name) + "_" + std::to_string(id)) + ",\"rgb\":" + rgb +
              ",\"group\":" + js(c.group.empty() ? std::string("solid") : c.group) + "}";
        fc = false;
    }
    jc += "}";
    // --- bricks (rest pose, world = body group local space)
    MechModel rest = m;
    for (auto& a : rest.mats) a = Affine();
    std::string jb = "[";
    for (size_t i = 0; i < m_bricks.size(); ++i) {
        const Brick& b = m_bricks[i];
        Affine a = m_mech.brickMatrix(rest, b, (int)i);
        const double el[16] = {a.m[0][0], a.m[1][0], a.m[2][0], 0, a.m[0][1], a.m[1][1], a.m[2][1], 0, a.m[0][2], a.m[1][2], a.m[2][2], 0, a.m[0][3], a.m[1][3], a.m[2][3], 1};
        std::string me = "[";
        for (int k = 0; k < 16; ++k) me += (k ? "," : "") + jn(r6(el[k]));
        const Part& p = m_lib.parts[b.part];
        jb += std::string(i ? "," : "") + "{\"part\":" + js(p.name) + ",\"color\":" + std::to_string(m_lib.palette[b.color].id) + ",\"body\":" + std::to_string(m.brickBody[i]) +
              ",\"m\":" + me + "],\"ldraw\":" + js(p.ldraw.empty() ? p.name : p.ldraw) + "}";
    }
    jb += "]";
    // --- joints: channel + alignment
    struct JD { std::string chan; int s = 1; bool align = false; double al[3], un[3]; double base = 0; std::map<int, std::vector<std::pair<double, double>>> sdk; };
    std::vector<JD> jd(m.tree.size());
    static const char* AX[3] = {"X", "Y", "Z"};
    for (size_t k = 0; k < m.tree.size(); ++k) {
        float3 u = normalize(m.tree[k].j.u);
        const float c[3] = {u.x, u.y, u.z};
        int ai = -1;
        for (int q = 0; q < 3; ++q) if (std::fabs(std::fabs(c[q]) - 1) < 1e-6) ai = q;
        const char* axis = "X";
        if (ai >= 0) { axis = AX[ai]; jd[k].s = c[ai] > 0 ? 1 : -1; }
        else {
            float4 q = fromUnitVectors(float3(1, 0, 0), u);
            eulerZYX(q, jd[k].al);
            eulerZYX(float4(-q.x, -q.y, -q.z, q.w), jd[k].un);
            for (int e = 0; e < 3; ++e) { jd[k].al[e] = r6(jd[k].al[e]); jd[k].un[e] = r6(jd[k].un[e]); }
            jd[k].align = true;
        }
        jd[k].chan = std::string(m.tree[k].j.type == 1 ? "translate" : "rotate") + axis;
    }
    const double DEG = 3.14159265358979323846 / 180;
    auto chanVal = [&](size_t k, double q) { return m.tree[k].j.type == 1 ? q * jd[k].s : q / DEG * jd[k].s; };
    // --- solve helpers
    auto setCmds = [&](const std::vector<double>& vals) { for (size_t i = 0; i < m.cmds.size(); ++i) m.cmds[i].value = (float)vals[i]; };
    auto solveAt = [&](const std::vector<double>& vals, std::vector<double>& q) { setCmds(vals); if (!m_mech.solve(m)) return false; q = m.q; return true; };
    const std::vector<double> zeros(m.cmds.size(), 0.0);
    std::fill(m.q.begin(), m.q.end(), 0.0);
    std::vector<double> q0;
    if (!solveAt(zeros, q0)) q0 = m.q;
    for (size_t k = 0; k < m.tree.size(); ++k) jd[k].base = r6(chanVal(k, q0[m.tree[k].qi]));
    // --- sample each command alone, marching out from 0 so the solver keeps the same branch of the loops
    struct CO { std::string name, attr, type; double lo, hi; };
    std::vector<CO> cmdsOut;
    std::vector<std::string> fails;
    for (size_t ci = 0; ci < m.cmds.size(); ++ci) {
        std::vector<double> list;   // motors: -360..360 degrees by 30; actuators: their travel in 16 steps (cm)
        const bool cyl = m.cmds[ci].type == 1;
        if (cyl) { const double lo = std::max<double>(m.cmds[ci].lo, -50), hi = std::min<double>(m.cmds[ci].hi, 50); for (int s = 0; s <= 16; ++s) list.push_back(r6(lo + (hi - lo) * s / 16)); }
        else for (int a = -360; a <= 360; a += 30) list.push_back(a);
        std::map<double, std::vector<double>> res;
        res[0] = q0;
        for (int dir : {1, -1}) {
            m.q = q0;
            std::vector<double> last = q0;
            std::vector<double> sel;
            for (double x : list) if (dir > 0 ? x > 0 : x < 0) sel.push_back(x);
            std::sort(sel.begin(), sel.end(), [&](double a, double b) { return dir * (a - b) < 0; });
            for (double x : sel) {
                std::vector<double> vals = zeros, q;
                vals[ci] = cyl ? x : x * DEG;
                if (!solveAt(vals, q)) { fails.push_back(m.cmds[ci].name + " = " + std::to_string(x)); m.q = last; break; }
                res[x] = q;
                last = q;
            }
        }
        for (size_t k = 0; k < m.tree.size(); ++k) {
            const int qi = m.tree[k].qi;
            std::vector<std::pair<double, double>> keys;
            double mx = 0;
            for (auto& [x, q] : res) { double v = chanVal(k, q[qi]) - chanVal(k, q0[qi]); keys.push_back({x, r6(v)}); mx = std::max(mx, std::fabs(v)); }
            if (mx < 1e-5) continue;
            jd[k].sdk[(int)ci] = keys;
        }
        int nSame = 0;
        for (auto& o : cmdsOut) nSame += o.type == (cyl ? "cyl" : "motor");
        if (cyl) cmdsOut.push_back({m.cmds[ci].name, "verin" + std::to_string(nSame + 1), "cyl", r6(res.begin()->first), r6(res.rbegin()->first)});
        else cmdsOut.push_back({m.cmds[ci].name, "moteur" + std::to_string(nSame + 1), "motor", -360, 360});
    }
    // --- coupling check: Maya adds the per-command curves; compare with the true solve for random combinations
    double coupling = 0;
    std::mt19937 rng(1234);
    std::uniform_real_distribution<double> U(0, 1);
    for (int trial = 0; trial < 6 && !m.cmds.empty(); ++trial) {
        std::vector<double> vals(m.cmds.size());
        for (size_t ci = 0; ci < vals.size(); ++ci) vals[ci] = cmdsOut[ci].lo + U(rng) * (cmdsOut[ci].hi - cmdsOut[ci].lo);
        m.q = q0;
        std::vector<double> q;
        bool ok = true;
        for (int s = 1; s <= 8 && ok; ++s) {
            std::vector<double> v(vals.size());
            for (size_t ci = 0; ci < vals.size(); ++ci) v[ci] = vals[ci] * (m.cmds[ci].type == 1 ? 1.0 : DEG) * (s / 8.0);
            ok = solveAt(v, q);
        }
        if (!ok) continue;
        for (size_t k = 0; k < m.tree.size(); ++k) {
            if (jd[k].sdk.empty()) continue;
            double sum = 0;
            for (auto& [ci, keys] : jd[k].sdk) {
                const double x = vals[ci];
                size_t j = 1;
                while (j < keys.size() - 1 && keys[j].first < x) ++j;
                const double x0 = keys[j - 1].first, y0 = keys[j - 1].second, x1 = keys[j].first, y1 = keys[j].second;
                sum += y0 + (y1 - y0) * (x - x0) / ((x1 - x0) != 0 ? (x1 - x0) : 1);
            }
            const int qi = m.tree[k].qi;
            const double truth = chanVal(k, q[qi]) - chanVal(k, q0[qi]);
            coupling = std::max(coupling, std::fabs(truth - sum));
        }
    }
    // --- DATA
    std::string jj = "[";
    for (size_t k = 0; k < m.tree.size(); ++k) {
        const MechTreeJ& t = m.tree[k];
        const JD& d = jd[k];
        std::string o = "{\"k\":" + std::to_string(k) + ",\"type\":" + js(t.j.type == 1 ? "pri" : "rev") + ",\"parent\":" + std::to_string(t.parent) + ",\"child\":" + std::to_string(t.child) +
                        ",\"p\":[" + jn(r6(t.j.p.x)) + "," + jn(r6(t.j.p.y)) + "," + jn(r6(t.j.p.z)) + "]";
        if (d.align) o += ",\"align\":[" + jn(d.al[0]) + "," + jn(d.al[1]) + "," + jn(d.al[2]) + "],\"unalign\":[" + jn(d.un[0]) + "," + jn(d.un[1]) + "," + jn(d.un[2]) + "]";
        else o += ",\"align\":null";
        o += ",\"s\":" + std::to_string(d.s) + ",\"chan\":" + js(d.chan) + ",\"base\":" + jn(d.base);
        if (!d.sdk.empty()) {
            o += ",\"sdk\":{";
            bool f1 = true;
            for (auto& [ci, keys] : d.sdk) {
                o += (f1 ? "" : ",") + js(std::to_string(ci)) + ":[";
                for (size_t i = 0; i < keys.size(); ++i) o += (i ? "," : "") + std::string("[") + jn(keys[i].first) + "," + jn(keys[i].second) + "]";
                o += "]";
                f1 = false;
            }
            o += "}";
        }
        jj += (k ? "," : "") + o + "}";
    }
    jj += "]";
    std::string jr = "[";
    for (size_t i = 0; i < m.roots.size(); ++i) jr += (i ? "," : "") + std::to_string(m.roots[i]);
    jr += "]";
    std::string jcmd = "[";
    for (size_t i = 0; i < cmdsOut.size(); ++i)
        jcmd += (i ? "," : "") + std::string("{\"name\":") + js(cmdsOut[i].name) + ",\"attr\":" + js(cmdsOut[i].attr) + ",\"type\":" + js(cmdsOut[i].type) + ",\"lo\":" + jn(cmdsOut[i].lo) + ",\"hi\":" + jn(cmdsOut[i].hi) + "}";
    jcmd += "]";
    const std::string name = "BrickIt_scene";
    const std::string json = "{\"name\":" + js(name) + ",\"bevel\":0.02,\"colors\":" + jc + ",\"meshes\":" + jm + ",\"bricks\":" + jb + ",\"roots\":" + jr + ",\"joints\":" + jj + ",\"cmds\":" + jcmd + "}";
    std::string b64 = base64(zlibCompress(json)), lines;
    for (size_t i = 0; i < b64.size(); i += 120) lines += b64.substr(i, 120) + "\n";   // .replace(/(.{1,120})/g, "$1\n")
    std::string tpl;
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, (m_exeDir + L"\\assets\\brickit\\maya_template.py").c_str(), L"rb") != 0 || !f) { err = "assets/brickit/maya_template.py introuvable"; return false; }
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) tpl.append(buf, n);
        fclose(f);
    }
    const size_t at = tpl.find("\"__DATA__\"");
    if (at == std::string::npos) { err = "maya_template.py : marqueur __DATA__ absent"; return false; }
    py = tpl.substr(0, at) + "\"\"\"\n" + lines + "\"\"\"" + tpl.substr(at + 10);
    st.bricks = (int)m_bricks.size();
    st.parts = (int)partsUsed.size();
    st.bodies = m.bodyCount;
    st.joints = (int)m.tree.size();
    st.cmds = (int)cmdsOut.size();
    st.fails = fails;
    st.coupling = r4(coupling);
    return true;
}

struct ReadmeStats { int bricks, parts, bodies, joints, cmds; double coupling; };
static std::string mayaReadme(const std::string& name, const ReadmeStats& st) {
    char cp[32];
    snprintf(cp, sizeof(cp), "%g", st.coupling);
    return "BrickIt Builder -> Maya · " + name + "\n\n"
           "1. Maya > Windows > General Editors > Script Editor, onglet Python.\n"
           "2. File > Load Script… > " + name + ".py, puis Execute (Ctrl+Entrée). Ou glisse le .py dans la fenêtre de Maya.\n"
           "3. Le groupe " + name + " contient la scène. Sélectionne BRICKIT_CTRL : un attribut par moteur (degrés) et par vérin (cm).\n"
           "   Une animation de démo est posée sur la timeline (chaque commande va au max, au min, puis revient à 0).\n\n"
           "Contenu : " + std::to_string(st.bricks) + " briques (" + std::to_string(st.parts) + " pièces LDraw différentes), " + std::to_string(st.bodies) + " corps rigides, " +
           std::to_string(st.joints) + " liaisons, " + std::to_string(st.cmds) + " commandes.\n"
           "Shaders : aiStandardSurface + aiRoundCorners (biseau) si Arnold (mtoa) est chargé, sinon standardSurface, sinon blinn.\n"
           "Rig : un groupe par corps rigide, une paire de groupes par liaison (Jn_repos + Jn piloté, pivot sur l'axe).\n"
           "Les liaisons sont pilotées par set driven keys échantillonnées depuis le solveur BrickIt, boucles fermées comprises.\n"
           "Écart max quand plusieurs commandes bougent ensemble : " + std::string(cp) + " (degrés, ou mm pour les glissières).\n"
           "Unités : centimètres, Y vers le haut. Chaque brique porte un attribut « ldraw » (référence de la pièce).\n";
}

// "Exporter vers Maya" of the « Scène partagée » sheet (web: mayaExportScene)
void BrickApp::mayaExportScene(const std::wstring& forcedPath) {
    if (m_bricks.empty()) { toast("Scène vide"); return; }
    toast("Export Maya en cours…");
    std::string py, err;
    MayaStats st;
    if (!mayaExportScript(py, st, err)) { toast("Export Maya impossible : " + err); return; }
    const std::string nm = "BrickIt_scene";
    const std::string zip = zipStored({{nm + ".py", py}, {"LISEZMOI.txt", mayaReadme(nm, ReadmeStats{st.bricks, st.parts, st.bodies, st.joints, st.cmds, st.coupling})}});
    std::wstring path = forcedPath;
    if (path.empty()) {
        wchar_t file[MAX_PATH] = L"BrickIt_scene_maya.zip";
        OPENFILENAMEW ofn = {sizeof(ofn)};
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFilter = L"Archive ZIP (*.zip)\0*.zip\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"zip";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&ofn)) return;
        path = file;
    }
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) { toast("Export Maya impossible : fichier non écrit"); return; }
    fwrite(zip.data(), 1, zip.size(), f);
    fclose(f);
    if (path.size() > 4) {   // the script next to the zip too when scripted (tests)
        if (!forcedPath.empty()) {
            std::wstring pyPath = path.substr(0, path.size() - 4) + L".py";
            if (_wfopen_s(&f, pyPath.c_str(), L"wb") == 0 && f) { fwrite(py.data(), 1, py.size(), f); fclose(f); }
        }
    }
    toast(nm + "_maya.zip : " + std::to_string(st.bricks) + " briques, " + std::to_string(st.joints) + " liaisons, " + std::to_string(st.cmds) + " commandes");
}

} // namespace brickit
