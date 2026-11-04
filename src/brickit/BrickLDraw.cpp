// BrickIt - LDraw models (.ldr, .mpd, .dat): the official LEGO sets of the LDraw OMR and any LDraw model. Every part
// line becomes a brick (its LDraw matrix converted to the BrickIt frame, the parts resolved through the aliases), every
// submodel instance a group (its name), every STEP a step of the reading mode (BrickRead.cpp). The steps of a
// submodel are read where it is first used, as the booklets show it; its next copies come in one step.
#include "BrickApp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <sstream>

namespace brickit {

namespace {

struct LMat {   // LDraw 3 x 4: rows of the 3 x 3 part, then the translation (LDU)
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    float t[3] = {0, 0, 0};
};

LMat mul(const LMat& A, const LMat& B) {   // A * B (B applied first)
    LMat r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) r.m[i * 3 + j] = A.m[i * 3] * B.m[j] + A.m[i * 3 + 1] * B.m[3 + j] + A.m[i * 3 + 2] * B.m[6 + j];
        r.t[i] = A.m[i * 3] * B.t[0] + A.m[i * 3 + 1] * B.t[1] + A.m[i * 3 + 2] * B.t[2] + A.t[i];
    }
    return r;
}

// rotation matrix (rows) -> unit quaternion (x, y, z, w)
float4 quatOf(const float* m) {
    const float tr = m[0] + m[4] + m[8];
    float4 q;
    if (tr > 0) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q = float4((m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s, 0.25f * s);
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const float s = std::sqrt(1.0f + m[0] - m[4] - m[8]) * 2.0f;
        q = float4(0.25f * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s, (m[7] - m[5]) / s);
    } else if (m[4] > m[8]) {
        const float s = std::sqrt(1.0f + m[4] - m[0] - m[8]) * 2.0f;
        q = float4((m[1] + m[3]) / s, 0.25f * s, (m[5] + m[7]) / s, (m[2] - m[6]) / s);
    } else {
        const float s = std::sqrt(1.0f + m[8] - m[0] - m[4]) * 2.0f;
        q = float4((m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25f * s, (m[3] - m[1]) / s);
    }
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return l > 0 ? float4(q.x / l, q.y / l, q.z / l, q.w / l) : float4(0, 0, 0, 1);
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    for (char& c : s) if (c == '\\') c = '/';
    return s;
}

std::string stem(const std::string& f) {   // "parts/3001.dat" -> "3001"
    std::string s = lower(f);
    const size_t sl = s.find_last_of('/');
    if (sl != std::string::npos) s = s.substr(sl + 1);
    if (s.size() > 4 && (s.substr(s.size() - 4) == ".dat" || s.substr(s.size() - 4) == ".ldr" || s.substr(s.size() - 4) == ".mpd")) s.resize(s.size() - 4);
    return s;
}

} // namespace

// The LDraw colours (LDConfig.ldr next to the full library): code -> sRGB, for the codes the palette lacks
static std::map<int, float3> ldrawColours(const std::wstring& fullDir) {
    std::map<int, float3> out;
    if (fullDir.empty()) return out;
    FILE* f = nullptr;
    if (_wfopen_s(&f, (fullDir + L"\\LDConfig.ldr").c_str(), L"rb") != 0 || !f) return out;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        const char* c = strstr(line, "!COLOUR");
        const char* k = strstr(line, " CODE ");
        const char* v = strstr(line, " VALUE #");
        if (!c || !k || !v) continue;
        const int code = atoi(k + 6);
        unsigned rgb = 0;
        sscanf_s(v + 8, "%6x", &rgb);
        out[code] = float3(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f);
    }
    fclose(f);
    return out;
}

bool BrickApp::importLDraw(const std::wstring& path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) { toast("Fichier introuvable"); return false; }
    std::string text;
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);

    // sections of the file: "0 FILE name" (MPD), else one section; the first one is the model
    std::map<std::string, std::vector<std::string>> sections;
    std::vector<std::string> order;
    {
        std::istringstream in(text);
        std::string line, cur;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t a = line.find_first_not_of(" \t");
            if (a == std::string::npos) continue;
            line = line.substr(a);
            if (line.rfind("0 FILE ", 0) == 0) {
                cur = lower(line.substr(7));
                while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t')) cur.pop_back();
                if (!sections.count(cur)) order.push_back(cur);
                sections[cur];
                continue;
            }
            if (line.rfind("0 NOFILE", 0) == 0) { cur.clear(); continue; }
            if (cur.empty()) { cur = "main.ldr"; order.push_back(cur); }
            sections[cur].push_back(line);
        }
    }
    if (order.empty()) { toast("Modèle LDraw vide"); return false; }

    const std::map<int, float3> ldc = ldrawColours(m_lib.fullDir);
    auto colourIndex = [&](int code) {
        for (int k = 0; k < (int)m_lib.palette.size(); ++k)
            if (m_lib.palette[k].id == code) return k;
        auto it = ldc.find(code);   // not in the palette: the nearest colour of the palette
        int best = 0;
        float bd = 1e30f;
        if (it != ldc.end())
            for (int k = 0; k < (int)m_lib.palette.size(); ++k) {
                const float3 d = m_lib.palette[k].srgb - it->second;
                const float e = dot(d, d);
                if (e < bd) { bd = e; best = k; }
            }
        return best;
    };

    std::vector<Brick> out;
    std::vector<Group> groups;
    std::vector<std::string> stepTitle;   // per step: the submodel being built ("" = the model)
    std::map<std::string, bool> built;    // submodels whose steps were read already
    int missing = 0, mirrored = 0;
    std::map<std::string, int> missingNames;
    int step = 0;
    const std::string mainName = order[0];
    std::function<void(const std::string&, const LMat&, int, const std::vector<int>&, bool)> expand =
        [&](const std::string& name, const LMat& M, int colour, const std::vector<int>& path, bool withSteps) {
        const std::vector<std::string>& lines = sections[name];
        bool any = false;   // parts in the current step
        auto endStep = [&]() {
            if (!withSteps || !any) return;
            ++step;
            stepTitle.push_back(name == mainName ? std::string() : stem(name));
            any = false;
        };
        for (const std::string& l : lines) {
            if (l.rfind("0 STEP", 0) == 0 || l.rfind("0 ROTSTEP", 0) == 0) { endStep(); continue; }
            if (l[0] != '1') continue;
            std::istringstream ls(l);
            int type = 0, c = 16;
            LMat P;
            std::string file;
            ls >> type >> c >> P.t[0] >> P.t[1] >> P.t[2];
            for (int k = 0; k < 9; ++k) ls >> P.m[k];
            std::getline(ls, file);
            size_t a = file.find_first_not_of(" \t");
            if (a == std::string::npos) continue;
            file = lower(file.substr(a));
            while (!file.empty() && (file.back() == ' ' || file.back() == '\t')) file.pop_back();
            const int col = c == 16 ? colour : c;
            const LMat W = mul(M, P);
            if (sections.count(file)) {   // a submodel: a group, its steps read the first time
                std::vector<int> sub = path;
                sub.push_back((int)groups.size());
                Group g;
                g.name = stem(file);
                groups.push_back(g);
                const bool first = !built[file];
                built[file] = true;
                if (withSteps && first) { endStep(); expand(file, W, col, sub, true); }
                else { expand(file, W, col, sub, false); any = true; }
                continue;
            }
            const int pi = m_lib.find(stem(file));
            if (pi < 0 || !m_lib.get(m_lib.parts[pi].name)) { ++missing; ++missingNames[stem(file)]; continue; }
            const Part& part = m_lib.parts[pi];
            // the BrickIt frame: (x, y, z)_BrickIt = (x, -y, -z)_LDraw * 0.04 cm; the part origin at its minimum corner
            float R[9];
            static const float S[3] = {1, -1, -1};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) R[i * 3 + j] = S[i] * W.m[i * 3 + j] * S[j];
            const float det = R[0] * (R[4] * R[8] - R[5] * R[7]) - R[1] * (R[3] * R[8] - R[5] * R[6]) + R[2] * (R[3] * R[7] - R[4] * R[6]);
            if (det < 0) ++mirrored;
            Brick b;
            b.part = pi;
            b.color = colourIndex(col);
            b.rot = quatOf(R);
            const float3 o = part.hasOrigin ? part.ldrawOrigin : float3(0);
            const float3 ro = qRot(b.rot, o);
            b.pos = float3(W.t[0] * 0.04f, -W.t[1] * 0.04f, -W.t[2] * 0.04f) - ro;
            b.path = path;
            b.step = step;   // (a copy of a submodel built already: the step of the line that uses it)
            out.push_back(b);
            any = true;
        }
        endStep();
    };
    LMat I;
    expand(mainName, I, 16, {}, true);
    if (out.empty()) { toast("Aucune pièce reconnue dans ce modèle"); return false; }

    pushUndo();
    m_bricks.swap(out);
    m_build.groups = groups;
    m_readTitles = stepTitle;
    m_readSteps = std::max(1, step);
    m_modelName = stem(std::string(path.begin(), path.end()));
    m_selected = -1;
    m_multi.clear();
    m_groupCtx.clear();
    m_scenePath.clear();
    rebuildScene();
    frameAll();
    char t[200];
    snprintf(t, sizeof(t), "%s : %d pièces, %d étapes, %d sous-modèles%s", m_modelName.c_str(), (int)m_bricks.size(), m_readSteps,
             (int)sections.size() - 1, missing ? " (des pièces inconnues ignorées)" : "");
    toast(t);
    m_importReport.clear();
    {
        char r[160];
        snprintf(r, sizeof(r), "pieces=%d steps=%d submodels=%d missing=%d mirrored=%d\n", (int)m_bricks.size(), m_readSteps, (int)sections.size() - 1, missing, mirrored);
        m_importReport = r;
        for (auto& [nm, k] : missingNames) m_importReport += "missing " + nm + " x" + std::to_string(k) + "\n";
    }
    startReading(1);
    return true;
}

} // namespace brickit
