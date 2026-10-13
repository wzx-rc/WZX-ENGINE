#pragma once
#include "imgui.h"
#include <cmath>

namespace atelier::inspector {
// Independent, named glyphs for the approved Inspector proposal. Coordinates
// are normalized inside the size square; no previous editor code is included.
enum class Glyph { Properties, Sun, Image, Search, ChevronDown, ChevronRight,
                   Reset, Undo, Redo, FolderOpen, Clear, Check };
inline void DrawGlyph(ImDrawList* draw, Glyph glyph, ImVec2 origin, float size, ImU32 color, float pixelScale = 1.f) {
    const float stroke = (size / pixelScale < 14 ? 1.0f : 1.25f) * pixelScale;
    constexpr float pi = 3.14159265358979323846f;
    auto p=[&](float x,float y){return ImVec2(origin.x+x*size,origin.y+y*size);};
    auto line=[&](float x,float y,float xx,float yy){draw->AddLine(p(x,y),p(xx,yy),color,stroke);};
    auto box=[&](float x,float y,float xx,float yy,float r=0.f){draw->AddRect(p(x,y),p(xx,yy),color,r*size,stroke);};
    auto circle=[&](float x,float y,float r){draw->AddCircle(p(x,y),r*size,color,20,stroke);};
    auto arc=[&](float x,float y,float r,float a,float b){draw->PathArcTo(p(x,y),r*size,a,b,20);draw->PathStroke(color,stroke);};
    switch(glyph){
    case Glyph::Properties:
        line(.16f,.27f,.84f,.27f);line(.16f,.5f,.84f,.5f);line(.16f,.73f,.84f,.73f);
        box(.32f,.17f,.46f,.37f,.03f);box(.57f,.4f,.71f,.6f,.03f);box(.26f,.63f,.4f,.83f,.03f);break;
    case Glyph::Sun:
        circle(.5f,.5f,.2f);for(int i=0;i<8;++i){float a=i*pi/4;line(.5f+std::cos(a)*.31f,.5f+std::sin(a)*.31f,.5f+std::cos(a)*.43f,.5f+std::sin(a)*.43f);}break;
    case Glyph::Image:
        box(.12f,.15f,.88f,.85f,.08f);circle(.35f,.36f,.07f);line(.16f,.75f,.44f,.49f);line(.44f,.49f,.59f,.65f);line(.59f,.65f,.73f,.51f);line(.73f,.51f,.87f,.65f);break;
    case Glyph::Search:circle(.43f,.43f,.27f);line(.63f,.63f,.87f,.87f);break;
    case Glyph::ChevronDown:line(.23f,.36f,.5f,.64f);line(.5f,.64f,.77f,.36f);break;
    case Glyph::ChevronRight:line(.36f,.23f,.64f,.5f);line(.64f,.5f,.36f,.77f);break;
    case Glyph::Reset:
        arc(.52f,.51f,.32f,-pi*.8f,pi*.9f);line(.21f,.27f,.21f,.48f);line(.21f,.48f,.42f,.48f);break;
    case Glyph::Undo:
        draw->AddBezierCubic(p(.83f,.77f),p(.84f,.33f),p(.5f,.27f),p(.22f,.44f),color,stroke);
        line(.22f,.44f,.22f,.22f);line(.22f,.44f,.44f,.44f);break;
    case Glyph::Redo:
        draw->AddBezierCubic(p(.17f,.77f),p(.16f,.33f),p(.5f,.27f),p(.78f,.44f),color,stroke);
        line(.78f,.44f,.78f,.22f);line(.78f,.44f,.56f,.44f);break;
    case Glyph::FolderOpen:
        line(.13f,.34f,.13f,.8f);line(.13f,.34f,.36f,.34f);line(.36f,.34f,.45f,.43f);line(.45f,.43f,.78f,.43f);line(.78f,.43f,.78f,.52f);
        line(.13f,.8f,.81f,.8f);line(.81f,.8f,.91f,.52f);line(.91f,.52f,.29f,.52f);line(.29f,.52f,.13f,.8f);break;
    case Glyph::Clear:line(.24f,.24f,.76f,.76f);line(.76f,.24f,.24f,.76f);break;
    case Glyph::Check:line(.18f,.49f,.41f,.72f);line(.41f,.72f,.82f,.27f);break;
    }
}
} // namespace atelier::inspector
