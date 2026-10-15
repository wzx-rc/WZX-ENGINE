// Debug: prints the joints of an FBX (scene global position) next to their bind position (TransformLink).
// Build: cl /nologo /std:c++17 /EHsc /O2 /Isrc /DNOMINMAX tools\fbxdump.cpp src\app\FbxImport.cpp src\app\MeshFile.cpp src\vp3d\AssetImport.cpp src\vp3d\MeshGen.cpp /Fe:build\fbxdump.exe ole32.lib windowscodecs.lib
#include "app/FbxImport.h"
#include <cstdio>
using namespace app;
struct JW { float3 position; float4 rot; float3 scale; };
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 1;
    FbxScene s;
    std::string err;
    if (!loadFbx(argv[1], s, &err)) { printf("error: %s\n", err.c_str()); return 1; }
    float3 mn, mx;
    s.mesh.bounds(mn, mx);
    printf("mesh %zu verts, bounds (%g %g %g) - (%g %g %g), joints %zu, clusters %zu\n", s.mesh.vertices.size(), mn.x, mn.y, mn.z, mx.x, mx.y, mx.z, s.joints.size(), s.clusters.size());
    std::vector<JW> w(s.joints.size());
    for (size_t i = 0; i < s.joints.size(); ++i) {
        const FbxJoint& j = s.joints[i];
        JW l{j.translation, j.rotation, j.scale};
        if (j.parent >= 0) {
            const JW& p = w[j.parent];
            w[i].position = p.position + qRot(p.rot, j.translation * p.scale);
            w[i].rot = qMul(p.rot, j.rotation);
            w[i].scale = p.scale * j.scale;
        } else w[i] = l;
    }
    if (!s.clusters.empty()) {
        const Affine& m = s.clusters[0].bindMesh;
        printf("mesh bind: X (%g %g %g) Y (%g %g %g) Z (%g %g %g) T (%g %g %g)\n", m.m[0][0], m.m[1][0], m.m[2][0], m.m[0][1], m.m[1][1], m.m[2][1], m.m[0][2], m.m[1][2], m.m[2][2], m.m[0][3], m.m[1][3], m.m[2][3]);
        const Affine& j = s.clusters[0].bindJoint;
        printf("joint0 bind: X (%g %g %g) Y (%g %g %g) Z (%g %g %g)\n", j.m[0][0], j.m[1][0], j.m[2][0], j.m[0][1], j.m[1][1], j.m[2][1], j.m[0][2], j.m[1][2], j.m[2][2]);
    }
    for (size_t c = 0; c < s.clusters.size() && c < 12; ++c) {
        const FbxCluster& cl = s.clusters[c];
        const JW& g = w[cl.joint];
        printf("%-28s scene (%8.3f %8.3f %8.3f) s %.3f  bind (%8.3f %8.3f %8.3f)\n", s.joints[cl.joint].name.c_str(), g.position.x, g.position.y, g.position.z, g.scale.x,
               cl.bindJoint.m[0][3], cl.bindJoint.m[1][3], cl.bindJoint.m[2][3]);
    }
    for (const FbxClip& c : s.clips) {
        printf("clip %s frames %d\n", c.name.c_str(), c.frames);
        for (size_t j = 0; j < s.joints.size() && j < 6; ++j)
            printf("  %-20s rest t (%g %g %g) s (%g %g %g) | f0 t (%g %g %g) s (%g %g %g)\n", s.joints[j].name.c_str(), s.joints[j].translation.x, s.joints[j].translation.y, s.joints[j].translation.z, s.joints[j].scale.x, s.joints[j].scale.y, s.joints[j].scale.z, c.translation[j][0].x, c.translation[j][0].y, c.translation[j][0].z, c.scale[j][0].x, c.scale[j][0].y, c.scale[j][0].z);
    }
    return 0;
}
