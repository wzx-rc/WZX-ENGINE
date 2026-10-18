// 3D curves of the editor (Create > Curve): linear, CV (cubic B-spline, Maya style), EP (passes through its
// points) and Bezier (anchors with in / out handles), open or closed, plus parametric curve primitives
// (line, circle, square, arc, helix, spiral, star). Points are in the object's local space; the object transform
// places the curve. The editor draws the curve line over the viewport; a curve can also carry a tube mesh swept
// along it (CurveMesh, 3ds Max "Rendering" rollout), which the renderers draw as any mesh.
#pragma once
#include "Math.h"
#include <cstdint>
#include <string>
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

// Tube swept along the curve (3ds Max spline "Rendering": Enable In Viewport, radial thickness, sides, angle).
struct CurveMesh {
    bool enabled = false;
    float radius = 0.1f;
    int sides = 12;
    float angle = 0.0f;      // degrees: turns the tube section (and the frames) around the curve
    bool caps = true;        // open curve: closes both ends
};

// Display of the frames along the curve: forward (tangent), up (normal) and side vectors, the rotation an object
// following the curve would have.
struct CurveFrames {
    bool show = false;
    float length = 0.5f;     // vector length in scene units
    int count = 24;          // frames shown along the curve
};

struct CurveData {
    int type = CURVE_NONE;
    bool closed = false;
    std::vector<float3> points;      // CVs (linear, CV), edit points (EP) or anchors (Bezier)
    std::vector<float3> hin, hout;   // Bezier: handle positions (same space as the points), one per anchor
    std::vector<uint8_t> broken;     // Bezier: 1 = the two handles move independently
    int steps = 16;                  // display: samples per span
    CurveShapeParams shape;
    CurveMesh mesh;
    CurveFrames frames;

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

// Fillet (3ds Max Editable Spline): each listed point becomes a circular arc of the given radius tangent to its two
// segments (the radius shrinks when the segments are too short). The result is a Bezier curve; linear segments
// stay straight. Open end points and straight points are left as they are.
CurveData filletCurve(const CurveData& c, const std::vector<int>& points, float radius);

// The same shape as a Bezier curve (exact: every span of these curves is a cubic), and the curve moved by a transform.
CurveData toBezierCurve(const CurveData& c);
CurveData transformCurve(const CurveData& c, const Affine& xf);

// Boolean of two closed curves lying in the same plane (3ds Max Editable Spline Boolean), both in the same space.
// The result keeps the exact curved segments (Bezier, or linear when every segment is straight); several loops
// when the shapes are apart or leave a hole. False with a message when it cannot be done.
enum CurveBoolOp { CURVE_UNION = 0, CURVE_SUBTRACT, CURVE_INTERSECT };
bool curveBoolean(const CurveData& a, const CurveData& b, int op, std::vector<CurveData>& out, std::string& error);
float curveArea(const CurveData& c);   // area enclosed by a closed planar curve (absolute value)

// Frames along the curve (rotation minimizing, the up vector starts as close to +Y as possible, closed curves
// close without a jump, then turned by mesh.angle): positions, forward (tangent), up and side unit vectors.
void curveFrames(const CurveData& c, std::vector<float3>& pos, std::vector<float3>& fwd, std::vector<float3>& up,
                 std::vector<float3>& side, std::vector<float>* arcLength = nullptr);

// Tube swept along the curve with the parameters of c.mesh (local space of the curve).
struct MeshData;
MeshData makeCurveTube(const CurveData& c);

// Builds the points of a primitive from c.shape (sets the type and closed flag of that primitive).
void buildCurveShape(CurveData& c);
CurveData makeCurveShape(int shape);

} // namespace vp3d
