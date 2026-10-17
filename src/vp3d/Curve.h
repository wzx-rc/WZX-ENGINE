// 3D curves of the editor (Create > Curve): linear, CV (cubic B-spline, Maya style), EP (passes through its
// points) and Bezier (anchors with in / out handles), open or closed, plus parametric curve primitives
// (line, circle, square, arc, helix, spiral, star). Points are in the object's local space; the object transform
// places the curve. The renderers ignore curves (mesh -1): the editor draws them over the viewport.
#pragma once
#include "Math.h"
#include <cstdint>
#include <vector>

namespace vp3d {

enum CurveType { CURVE_NONE = 0, CURVE_LINEAR, CURVE_CV, CURVE_EP, CURVE_BEZIER, CURVE_TYPE_COUNT };
enum CurveShape { CSHAPE_NONE = 0, CSHAPE_LINE, CSHAPE_CIRCLE, CSHAPE_SQUARE, CSHAPE_ARC, CSHAPE_HELIX, CSHAPE_SPIRAL, CSHAPE_STAR, CSHAPE_COUNT };

// Parameters of a curve primitive: the points are rebuilt from them (editing a point turns it into a free curve).
struct CurveShapeParams {
    int type = CSHAPE_NONE;
    float radius = 2.0f;     // circle, arc, helix, spiral (outer), star (outer)
    float radius2 = 0.5f;    // spiral: inner radius; star: inner radius
    float width = 4.0f;      // square / line: length along X
    float depth = 4.0f;      // square: length along Z
    float height = 4.0f;     // helix: total height
    float sweep = 180.0f;    // arc: degrees
    float turns = 3.0f;      // helix, spiral
    int sections = 8;        // circle / arc / helix / spiral: points per turn (or along the arc); star: branches
};

struct CurveData {
    int type = CURVE_NONE;
    bool closed = false;
    std::vector<float3> points;      // CVs (linear, CV), edit points (EP) or anchors (Bezier)
    std::vector<float3> hin, hout;   // Bezier: handle positions (same space as the points), one per anchor
    std::vector<uint8_t> broken;     // Bezier: 1 = the two handles move independently
    int steps = 16;                  // display: samples per span
    CurveShapeParams shape;

    bool valid() const { return type != CURVE_NONE; }
    int count() const { return (int)points.size(); }
};

const char* curveTypeName(int type);
const char* curveShapeName(int shape);
int curveDegree(const CurveData& c);   // 1 linear, 3 smooth (fewer points: lower degree)
int curveSpans(const CurveData& c);

// Polyline of the curve in local space (a closed curve repeats its first sample at the end).
// spanOf (optional) receives the span index of each sample.
void sampleCurve(const CurveData& c, std::vector<float3>& out, std::vector<int>* spanOf = nullptr);
float curveLength(const CurveData& c);
void curveBounds(const CurveData& c, float3& mn, float3& mx);   // samples and control points

// Bezier helpers: handles placed for a smooth curve (Catmull-Rom tangents), handle arrays sized to the points.
void bezierFixArrays(CurveData& c);
void bezierAutoHandles(CurveData& c, int i);
void bezierAutoHandlesAll(CurveData& c);

// Converts to another type keeping the points (to Bezier: smooth handles).
void convertCurve(CurveData& c, int type);
void reverseCurve(CurveData& c);
// Resamples the curve into n evenly spaced points (same type; Bezier: smooth handles).
void rebuildCurve(CurveData& c, int n);
// Inserts a point after index i (at position p, local space), deletes a point.
void insertCurvePoint(CurveData& c, int after, const float3& p);
void eraseCurvePoint(CurveData& c, int i);

// Builds the points of a primitive from c.shape (sets the type and closed flag of that primitive).
void buildCurveShape(CurveData& c);
CurveData makeCurveShape(int shape);

} // namespace vp3d
