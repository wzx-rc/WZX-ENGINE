// Vector editor icons taken from the Atelier ImGui reproduction (00 - REFERENCES/UI/01 - Dear ImGui/Theme Blue 01).
#pragma once

#include "imgui.h"
#include <cmath>

namespace app {
namespace atl {

enum class Icon {
    Cube, Folder, FolderOpen, Search, Eye, Lock, World, Scene, Plus,
    ArrowLeft, ArrowRight, ChevronDown, ChevronRight, Play, Pause, Step,
    Settings, Sun, Grid, List, Filter, Move, Rotate, Scale, Magnet, Camera,
    Material, Animation, Blueprint, Light, Volume, Info, Check, Link, More,
    Save, Trash, Undo, Redo, Home, Pin, Person, Monitor, Refresh
};

// Small editor glyphs drawn as vectors: p is the top-left of a size x size square.
// Coordinates stay inside that square, so the same glyph works in tabs and buttons.
inline void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 p, float size, ImU32 color) {
    if (!dl || size <= 0.0f) return;
    const float stroke = size >= 18.0f ? 1.45f : 1.05f;
    const float pi = 3.14159265358979323846f;
    auto point = [&](float x, float y) { return ImVec2(p.x + x * size, p.y + y * size); };
    auto line = [&](float ax, float ay, float bx, float by, float width = 0.0f) {
        dl->AddLine(point(ax, ay), point(bx, by), color, width > 0.0f ? width : stroke);
    };
    auto rect = [&](float ax, float ay, float bx, float by, float radius = 0.0f) {
        dl->AddRect(point(ax, ay), point(bx, by), color, radius * size, 0, stroke);
    };
    auto circle = [&](float x, float y, float radius) {
        dl->AddCircle(point(x, y), radius * size, color, 16, stroke);
    };
    auto dot = [&](float x, float y, float radius = 0.055f) {
        dl->AddCircleFilled(point(x, y), radius * size, color, 12);
    };
    auto arc = [&](float x, float y, float radius, float start, float end) {
        dl->PathArcTo(point(x, y), radius * size, start, end, 16);
        dl->PathStroke(color, 0, stroke);
    };
    auto triangle = [&](float ax, float ay, float bx, float by, float cx, float cy) {
        dl->AddTriangleFilled(point(ax, ay), point(bx, by), point(cx, cy), color);
    };
    auto arrow = [&](float ax, float ay, float bx, float by) {
        line(ax, ay, bx, by);
        float dx = bx - ax, dy = by - ay;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f) return;
        dx /= len; dy /= len;
        line(bx, by, bx - dx * .19f - dy * .14f, by - dy * .19f + dx * .14f);
        line(bx, by, bx - dx * .19f + dy * .14f, by - dy * .19f - dx * .14f);
    };
    auto cube = [&]() {
        line(.18f, .30f, .50f, .13f); line(.50f, .13f, .82f, .30f);
        line(.82f, .30f, .82f, .69f); line(.82f, .69f, .50f, .88f);
        line(.50f, .88f, .18f, .69f); line(.18f, .69f, .18f, .30f);
        line(.18f, .30f, .50f, .49f); line(.50f, .49f, .82f, .30f);
        line(.50f, .49f, .50f, .88f);
    };
    switch (icon) {
    case Icon::Cube:
        cube(); break;
    case Icon::Folder:
        line(.13f,.27f,.39f,.27f); line(.39f,.27f,.48f,.37f);
        line(.48f,.37f,.87f,.37f); line(.87f,.37f,.87f,.80f);
        line(.87f,.80f,.13f,.80f); line(.13f,.80f,.13f,.27f);
        break;
    case Icon::FolderOpen:
        line(.14f,.34f,.14f,.78f); line(.14f,.34f,.37f,.34f);
        line(.37f,.34f,.46f,.43f); line(.46f,.43f,.77f,.43f);
        line(.77f,.43f,.77f,.51f); line(.14f,.78f,.83f,.78f);
        line(.83f,.78f,.92f,.51f); line(.92f,.51f,.30f,.51f);
        line(.30f,.51f,.14f,.78f); break;
    case Icon::Search:
        circle(.43f,.42f,.25f); line(.61f,.61f,.85f,.86f); break;
    case Icon::Eye:
        dl->AddBezierCubic(point(.09f,.50f), point(.32f,.12f), point(.68f,.12f), point(.91f,.50f), color, stroke);
        dl->AddBezierCubic(point(.09f,.50f), point(.32f,.88f), point(.68f,.88f), point(.91f,.50f), color, stroke);
        circle(.50f,.50f,.14f); break;
    case Icon::Lock:
        rect(.24f,.46f,.76f,.87f,.04f); arc(.50f,.43f,.18f,pi,2*pi);
        line(.32f,.43f,.32f,.48f); line(.68f,.43f,.68f,.48f);
        dot(.50f,.63f,.045f); line(.50f,.63f,.50f,.73f); break;
    case Icon::World:
        circle(.50f,.50f,.36f); line(.14f,.50f,.86f,.50f);
        arc(.50f,.50f,.24f,-pi*.5f,pi*.5f); arc(.50f,.50f,.24f,pi*.5f,pi*1.5f);
        line(.24f,.29f,.76f,.29f); line(.24f,.71f,.76f,.71f); break;
    case Icon::Scene:
        line(.19f,.33f,.50f,.13f); line(.50f,.13f,.81f,.33f);
        line(.19f,.53f,.50f,.33f); line(.50f,.33f,.81f,.53f);
        line(.19f,.73f,.50f,.53f); line(.50f,.53f,.81f,.73f);
        break;
    case Icon::Plus:
        line(.19f,.50f,.81f,.50f); line(.50f,.19f,.50f,.81f); break;
    case Icon::ArrowLeft:
        arrow(.83f,.50f,.18f,.50f); break;
    case Icon::ArrowRight:
        arrow(.17f,.50f,.82f,.50f); break;
    case Icon::ChevronDown:
        line(.25f,.38f,.50f,.63f); line(.50f,.63f,.75f,.38f); break;
    case Icon::ChevronRight:
        line(.38f,.25f,.63f,.50f); line(.63f,.50f,.38f,.75f); break;
    case Icon::Play:
        triangle(.29f,.18f,.79f,.50f,.29f,.82f); break;
    case Icon::Pause:
        dl->AddRectFilled(point(.25f,.22f), point(.40f,.78f), color, size*.025f);
        dl->AddRectFilled(point(.60f,.22f), point(.75f,.78f), color, size*.025f); break;
    case Icon::Step:
        triangle(.19f,.23f,.62f,.50f,.19f,.77f); line(.75f,.23f,.75f,.77f,stroke*1.6f); break;
    case Icon::Settings:
        circle(.50f,.50f,.23f); circle(.50f,.50f,.075f);
        for(int i=0;i<8;++i) {
            const float a=i*pi/4;
            line(.50f+std::cos(a)*.26f,.50f+std::sin(a)*.26f,.50f+std::cos(a)*.37f,.50f+std::sin(a)*.37f);
        }
        break;
    case Icon::Sun:
        circle(.50f,.50f,.17f);
        for(int i=0;i<8;++i) {
            const float a=i*pi/4;
            line(.50f+std::cos(a)*.28f,.50f+std::sin(a)*.28f,.50f+std::cos(a)*.40f,.50f+std::sin(a)*.40f);
        }
        break;
    case Icon::Grid:
        for(int y=0;y<3;++y) for(int x=0;x<3;++x)
            dl->AddRectFilled(point(.18f+x*.25f,.18f+y*.25f), point(.31f+x*.25f,.31f+y*.25f), color);
        break;
    case Icon::List:
        for(int y=0;y<3;++y) { dot(.20f,.26f+y*.24f,.05f); line(.36f,.26f+y*.24f,.83f,.26f+y*.24f); }
        break;
    case Icon::Filter:
        line(.16f,.21f,.84f,.21f); line(.84f,.21f,.59f,.50f);
        line(.59f,.50f,.59f,.77f); line(.59f,.77f,.42f,.85f);
        line(.42f,.85f,.42f,.50f); line(.42f,.50f,.16f,.21f); break;
    case Icon::Move:
        arrow(.50f,.50f,.50f,.12f); arrow(.50f,.50f,.50f,.88f);
        arrow(.50f,.50f,.12f,.50f); arrow(.50f,.50f,.88f,.50f); break;
    case Icon::Rotate:
        arc(.50f,.50f,.32f,-pi*.18f,pi*1.39f);
        line(.42f,.17f,.56f,.12f); line(.42f,.17f,.46f,.33f); dot(.50f,.50f,.045f); break;
    case Icon::Scale:
        rect(.16f,.60f,.40f,.84f); line(.42f,.58f,.80f,.20f);
        line(.54f,.20f,.80f,.20f); line(.80f,.20f,.80f,.46f); break;
    case Icon::Magnet:
        arc(.50f,.51f,.28f,0,pi);
        line(.22f,.51f,.22f,.18f); line(.78f,.51f,.78f,.18f);
        line(.22f,.18f,.38f,.18f); line(.78f,.18f,.62f,.18f);
        line(.38f,.18f,.38f,.51f); line(.62f,.18f,.62f,.51f);
        arc(.50f,.51f,.12f,0,pi); line(.22f,.34f,.38f,.34f); line(.62f,.34f,.78f,.34f); break;
    case Icon::Camera:
        rect(.15f,.32f,.85f,.79f,.04f); circle(.50f,.55f,.15f);
        line(.30f,.32f,.36f,.22f); line(.36f,.22f,.64f,.22f); line(.64f,.22f,.70f,.32f); break;
    case Icon::Material:
        circle(.50f,.50f,.35f); arc(.45f,.46f,.23f,pi*.75f,pi*1.65f);
        line(.26f,.74f,.72f,.25f); break;
    case Icon::Animation:
        dot(.52f,.20f,.07f); line(.47f,.33f,.60f,.48f);
        line(.47f,.33f,.31f,.48f); line(.31f,.48f,.18f,.45f);
        line(.47f,.33f,.44f,.58f); line(.44f,.58f,.28f,.81f);
        line(.44f,.58f,.64f,.63f); line(.64f,.63f,.75f,.82f); line(.60f,.48f,.79f,.41f); break;
    case Icon::Blueprint:
        rect(.12f,.19f,.47f,.81f,.025f); rect(.53f,.19f,.88f,.81f,.025f);
        line(.47f,.24f,.53f,.24f); line(.47f,.76f,.53f,.76f); break;
    case Icon::Light:
        circle(.50f,.34f,.22f); line(.36f,.53f,.39f,.69f); line(.64f,.53f,.61f,.69f);
        line(.39f,.69f,.61f,.69f); line(.40f,.79f,.60f,.79f); line(.45f,.87f,.55f,.87f); break;
    case Icon::Volume:
        line(.13f,.39f,.32f,.39f); line(.32f,.39f,.52f,.23f);
        line(.52f,.23f,.52f,.77f); line(.52f,.77f,.32f,.61f);
        line(.32f,.61f,.13f,.61f); line(.13f,.61f,.13f,.39f);
        arc(.51f,.50f,.23f,-pi*.32f,pi*.32f); arc(.51f,.50f,.37f,-pi*.32f,pi*.32f); break;
    case Icon::Info:
        circle(.50f,.50f,.35f); dot(.50f,.31f,.05f); line(.50f,.46f,.50f,.69f); break;
    case Icon::Check:
        line(.20f,.49f,.41f,.69f); line(.41f,.69f,.80f,.28f); break;
    case Icon::Link:
        arc(.37f,.63f,.20f,pi*.20f,pi*1.80f); arc(.63f,.37f,.20f,-pi*.80f,pi*.80f);
        line(.40f,.60f,.60f,.40f); break;
    case Icon::More:
        dot(.25f,.50f,.065f); dot(.50f,.50f,.065f); dot(.75f,.50f,.065f); break;
    case Icon::Save:
        rect(.18f,.15f,.82f,.85f,.02f); rect(.30f,.15f,.68f,.40f); rect(.31f,.58f,.69f,.85f); break;
    case Icon::Trash:
        line(.22f,.28f,.78f,.28f); line(.35f,.19f,.65f,.19f);
        line(.27f,.28f,.32f,.83f); line(.32f,.83f,.68f,.83f); line(.68f,.83f,.73f,.28f);
        line(.43f,.41f,.43f,.70f); line(.57f,.41f,.57f,.70f); break;
    case Icon::Undo:
        dl->AddBezierCubic(point(.83f,.74f),point(.86f,.29f),point(.43f,.23f),point(.20f,.44f),color,stroke);
        line(.20f,.44f,.20f,.18f); line(.20f,.44f,.46f,.44f); break;
    case Icon::Redo:
        dl->AddBezierCubic(point(.17f,.74f),point(.14f,.29f),point(.57f,.23f),point(.80f,.44f),color,stroke);
        line(.80f,.44f,.80f,.18f); line(.80f,.44f,.54f,.44f); break;
    case Icon::Home:
        line(.13f,.44f,.50f,.15f); line(.50f,.15f,.87f,.44f);
        line(.24f,.37f,.24f,.83f); line(.24f,.83f,.76f,.83f); line(.76f,.83f,.76f,.37f);
        rect(.43f,.58f,.58f,.83f); break;
    case Icon::Pin:
        line(.38f,.16f,.72f,.28f); line(.43f,.20f,.33f,.47f);
        line(.67f,.26f,.57f,.52f); line(.33f,.47f,.20f,.49f);
        line(.20f,.49f,.63f,.64f); line(.63f,.64f,.57f,.52f); line(.40f,.57f,.27f,.87f); break;
    case Icon::Person:
        circle(.50f,.31f,.16f);
        dl->AddBezierCubic(point(.22f,.84f),point(.22f,.51f),point(.78f,.51f),point(.78f,.84f),color,stroke);
        line(.22f,.84f,.78f,.84f); break;
    case Icon::Monitor:
        rect(.13f,.18f,.87f,.68f,.03f); line(.50f,.68f,.50f,.83f); line(.32f,.83f,.68f,.83f); break;
    case Icon::Refresh:
        arc(.50f,.50f,.30f,-pi*.78f,pi*.18f); arc(.50f,.50f,.30f,pi*.22f,pi*1.18f);
        line(.77f,.64f,.81f,.42f); line(.77f,.64f,.57f,.60f);
        line(.23f,.36f,.19f,.58f); line(.23f,.36f,.43f,.40f); break;
    }
}

} // namespace atl
} // namespace app
