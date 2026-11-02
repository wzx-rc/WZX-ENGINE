#include "Svg.h"
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace svg {

static const float kPi = 3.14159265f;

struct Parser {
    const char* s;
    void ws() { while (*s && (std::isspace((unsigned char)*s) || *s == ',')) ++s; }
    bool num(float& v) {
        ws();
        if (!*s || !(std::isdigit((unsigned char)*s) || *s == '-' || *s == '+' || *s == '.')) return false;
        char* e;
        v = std::strtof(s, &e);
        if (e == s) return false;
        s = e;
        return true;
    }
    bool flag(bool& f) {   // arc flags may be written without separators ("0 1 1" or "011")
        ws();
        if (*s == '0' || *s == '1') { f = *s == '1'; ++s; return true; }
        return false;
    }
};

static void flush(ImDrawList* dl, std::vector<ImVec2>& pts, bool closed, ImU32 col, float thick, bool fill) {
    if (pts.size() >= 2) {
        if (fill && pts.size() >= 3) dl->AddConvexPolyFilled(pts.data(), (int)pts.size(), col);
        else dl->AddPolyline(pts.data(), (int)pts.size(), col, closed ? ImDrawFlags_Closed : ImDrawFlags_None, thick);
    }
    pts.clear();
}

void path(ImDrawList* dl, const char* d, ImVec2 o, float scale, ImU32 col, float thick, bool fill) {
    Parser p{d};
    std::vector<ImVec2> pts;
    float cx = 0, cy = 0, sx = 0, sy = 0;
    char cmd = 0;
    auto P = [&](float x, float y) { return ImVec2(o.x + x * scale, o.y + y * scale); };
    for (;;) {
        p.ws();
        if (!*p.s) break;
        if (std::isalpha((unsigned char)*p.s)) cmd = *p.s++;
        else if (!cmd) break;
        const bool rel = std::islower((unsigned char)cmd) != 0;
        float a, b, c2, d2, e, f;
        switch (std::tolower((unsigned char)cmd)) {
        case 'm':
            if (!p.num(a) || !p.num(b)) return;
            flush(dl, pts, false, col, thick, fill);
            cx = rel ? cx + a : a; cy = rel ? cy + b : b;
            sx = cx; sy = cy;
            pts.push_back(P(cx, cy));
            cmd = rel ? 'l' : 'L';   // following pairs are lines
            break;
        case 'l':
            if (!p.num(a) || !p.num(b)) return;
            cx = rel ? cx + a : a; cy = rel ? cy + b : b;
            pts.push_back(P(cx, cy));
            break;
        case 'h':
            if (!p.num(a)) return;
            cx = rel ? cx + a : a;
            pts.push_back(P(cx, cy));
            break;
        case 'v':
            if (!p.num(a)) return;
            cy = rel ? cy + a : a;
            pts.push_back(P(cx, cy));
            break;
        case 'c': {
            if (!p.num(a) || !p.num(b) || !p.num(c2) || !p.num(d2) || !p.num(e) || !p.num(f)) return;
            float x1 = rel ? cx + a : a, y1 = rel ? cy + b : b, x2 = rel ? cx + c2 : c2, y2 = rel ? cy + d2 : d2;
            float x3 = rel ? cx + e : e, y3 = rel ? cy + f : f;
            for (int i = 1; i <= 12; ++i) {
                float t = i / 12.0f, u = 1 - t;
                float x = u * u * u * cx + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3;
                float y = u * u * u * cy + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3;
                pts.push_back(P(x, y));
            }
            cx = x3; cy = y3;
            break;
        }
        case 'a': {
            float rx, ry, rot;
            bool large, sweep;
            if (!p.num(rx) || !p.num(ry) || !p.num(rot) || !p.flag(large) || !p.flag(sweep) || !p.num(e) || !p.num(f)) return;
            float x2 = rel ? cx + e : e, y2 = rel ? cy + f : f;
            // endpoint to centre parameterization (rotation ignored: the icons use circles)
            rx = std::fabs(rx); ry = std::fabs(ry);
            float dx = (cx - x2) * 0.5f, dy = (cy - y2) * 0.5f;
            float lam = dx * dx / (rx * rx) + dy * dy / (ry * ry);
            if (lam > 1) { float k = std::sqrt(lam); rx *= k; ry *= k; }
            float num_ = rx * rx * ry * ry - rx * rx * dy * dy - ry * ry * dx * dx, den = rx * rx * dy * dy + ry * ry * dx * dx;
            float co = den > 0 ? std::sqrt(std::fmax(0.0f, num_ / den)) : 0;
            if (large == sweep) co = -co;
            float ccx = co * rx * dy / ry + (cx + x2) * 0.5f, ccy = -co * ry * dx / rx + (cy + y2) * 0.5f;
            float t1 = std::atan2((cy - ccy) / ry, (cx - ccx) / rx), t2 = std::atan2((y2 - ccy) / ry, (x2 - ccx) / rx);
            float dt = t2 - t1;
            if (sweep && dt < 0) dt += 2 * kPi;
            if (!sweep && dt > 0) dt -= 2 * kPi;
            int n = (int)std::ceil(std::fabs(dt) / (kPi / 12)) + 1;
            for (int i = 1; i <= n; ++i) {
                float t = t1 + dt * i / n;
                pts.push_back(P(ccx + rx * std::cos(t), ccy + ry * std::sin(t)));
            }
            cx = x2; cy = y2;
            break;
        }
        case 'z':
            cx = sx; cy = sy;
            flush(dl, pts, true, col, thick, fill);
            pts.push_back(P(cx, cy));
            break;
        default: return;
        }
    }
    flush(dl, pts, false, col, thick, fill);
}

void circle(ImDrawList* dl, float cx, float cy, float r, ImVec2 o, float scale, ImU32 col, float thick, bool fill, float dash) {
    ImVec2 c(o.x + cx * scale, o.y + cy * scale);
    if (fill) { dl->AddCircleFilled(c, r * scale, col, 24); return; }
    if (dash <= 0) { dl->AddCircle(c, r * scale, col, 32, thick); return; }
    const int n = (int)std::round(2 * kPi * r / (dash * 2));
    for (int i = 0; i < n; ++i) {
        float a0 = 2 * kPi * i / n, a1 = a0 + kPi / n;
        dl->PathArcTo(c, r * scale, a0, a1, 4);
        dl->PathStroke(col, ImDrawFlags_None, thick);
    }
}

void rect(ImDrawList* dl, float x, float y, float w, float h, float rx, ImVec2 o, float scale, ImU32 col, float thick) {
    dl->AddRect(ImVec2(o.x + x * scale, o.y + y * scale), ImVec2(o.x + (x + w) * scale, o.y + (y + h) * scale), col, rx * scale, 0, thick);
}

static ImU32 withAlpha(ImU32 c, float a) { return (c & 0x00FFFFFF) | ((ImU32)(((c >> 24) & 0xFF) * a) << 24); }

void icon(ImDrawList* dl, Icon i, ImVec2 c, float size, ImU32 col) {
    const float k = size / 24.0f, t = 2.0f * k;
    const ImVec2 o(c.x - 12 * k, c.y - 12 * k);
    switch (i) {
    case I_UNDO: path(dl, "M9 14 4 9l5-5", o, k, col, t); path(dl, "M4 9h11a5 5 0 0 1 0 10h-3", o, k, col, t); break;
    case I_REDO: path(dl, "m15 14 5-5-5-5", o, k, col, t); path(dl, "M20 9H9a5 5 0 0 0 0 10h3", o, k, col, t); break;
    case I_SELECT: path(dl, "M4 3l7 17 2.5-7L21 10.5z", o, k, col, t); break;
    case I_VISOR: path(dl, "M4 8V4h4M16 4h4v4M20 16v4h-4M8 20H4v-4", o, k, col, t); circle(dl, 12, 12, 2.5f, o, k, col, t); break;
    case I_MOVE: path(dl, "M12 2v20M2 12h20M12 2l-3 3M12 2l3 3M12 22l-3-3M12 22l3-3M2 12l3-3M2 12l3 3M22 12l-3-3M22 12l-3 3", o, k, col, t); break;
    case I_ROTATE: path(dl, "M20 12a8 8 0 1 1-2.3-5.7", o, k, col, t); path(dl, "M20 4v5h-5", o, k, col, t); break;
    case I_GRID: path(dl, "M3 9h18M3 15h18M9 3v18M15 3v18", o, k, col, t); rect(dl, 3, 3, 18, 18, 2, o, k, col, t); break;
    case I_PIVOT:
        circle(dl, 12, 12, 7, o, k, col, t);
        circle(dl, 12, 12, 1.5f, o, k, col, t, true);
        path(dl, "M12 2v3M12 19v3M2 12h3M19 12h3", o, k, col, t);
        break;
    case I_SAFETY: path(dl, "M12 3l8 3v6c0 4.5-3.4 8-8 9-4.6-1-8-4.5-8-9V6z", o, k, col, t); path(dl, "m9 12 2 2 4-4", o, k, col, t); break;
    case I_FOCUS:
        circle(dl, 12, 12, 3.2f, o, k, col, t);
        path(dl, "M12 2.5v3M12 18.5v3M2.5 12h3M18.5 12h3", o, k, col, t);
        circle(dl, 12, 12, 7.5f, o, k, withAlpha(col, 0.55f), t, false, 2.5f);
        break;
    case I_ANIM: path(dl, "M5 3l14 9-14 9z", o, k, col, t); break;
    case I_VOXELS:
        circle(dl, 6, 12, 2.2f, o, k, col, t, true);
        circle(dl, 12, 12, 2.2f, o, k, col, t, true);
        circle(dl, 18, 12, 2.2f, o, k, col, t, true);
        path(dl, "M3 6h18M3 18h18", o, k, withAlpha(col, 0.45f), t);
        break;
    case I_DELETE: path(dl, "M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3", o, k, col, t); break;
    case I_MECH:
        circle(dl, 12, 12, 3, o, k, col, t);
        path(dl, "M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1", o, k, col, t);
        break;
    case I_WINDOWS: rect(dl, 3, 4, 18, 16, 2, o, k, col, t); path(dl, "M3 9h18M9 9v11", o, k, col, t); break;
    case I_FILE: path(dl, "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z", o, k, col, t); break;
    case I_RECENT: circle(dl, 12, 12, 9, o, k, col, t); path(dl, "M12 7v5l3 2", o, k, col, t); break;
    case I_FAV: path(dl, "m12 3 2.7 5.6 6.1.8-4.5 4.2 1.1 6.1L12 16.8l-5.4 2.9 1.1-6.1-4.5-4.2 6.1-.8z", o, k, col, t); break;
    case I_SEARCH: circle(dl, 10.5f, 10.5f, 6.5f, o, k, col, 2.2f * k); path(dl, "m20 20-4.8-4.8", o, k, col, 2.2f * k); break;
    case I_SIZE:
        rect(dl, 3, 3, 7, 7, 1.5f, o, k, col, t); rect(dl, 13, 3, 8, 8, 1.5f, o, k, col, t);
        rect(dl, 3, 13, 8, 8, 1.5f, o, k, col, t); rect(dl, 13, 13, 8, 8, 1.5f, o, k, col, t);
        break;
    case I_CLOSE: path(dl, "M6 6l12 12M18 6 6 18", o, k, col, t); break;
    case I_SETTINGS:   // gear (native settings button)
        path(dl, "M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z", o, k, col, t);
        circle(dl, 12, 12, 3, o, k, col, t);
        break;
    }
}

} // namespace svg
