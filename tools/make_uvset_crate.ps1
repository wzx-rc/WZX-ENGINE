# Writes assets/PROPS/Crate_UVSets.gltf: a 0.5 x 0.36 x 0.36 crate with three UV sets, used by the Prefab
# studio sample to show the UV Editor (UV set comparison, UDIM tiles, checks).
#   UV0 "Tiling"   : every face fills the 0-1 square (overlapping on purpose, tiling texture)
#   UV1 "Lightmap" : the six faces packed in a 3 x 2 atlas inside 0-1 (face -X mirrored on purpose: "flipped" check)
#   UV2 "UDIM"     : one face per UDIM tile (1001, 1002, 1003, 1011, 1012, 1013)
# Usage: powershell -File tools\make_uvset_crate.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$W = 0.5; $H = 0.36; $D = 0.36
# face: normal, U axis, V axis, size along U, size along V (cross(U, V) = normal)
$faces = @(
  @{ n = @(1, 0, 0);  u = @(0, 0, -1); v = @(0, 1, 0);  su = $D; sv = $H },
  @{ n = @(-1, 0, 0); u = @(0, 0, 1);  v = @(0, 1, 0);  su = $D; sv = $H },
  @{ n = @(0, 0, 1);  u = @(1, 0, 0);  v = @(0, 1, 0);  su = $W; sv = $H },
  @{ n = @(0, 0, -1); u = @(-1, 0, 0); v = @(0, 1, 0);  su = $W; sv = $H },
  @{ n = @(0, 1, 0);  u = @(1, 0, 0);  v = @(0, 0, -1); su = $W; sv = $D },
  @{ n = @(0, -1, 0); u = @(1, 0, 0);  v = @(0, 0, 1);  su = $W; sv = $D }
)
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
$pos = @(); $nrm = @(); $uv0 = @(); $uv1 = @(); $uv2 = @(); $idx = @()
$corners = @(@(0, 0), @(1, 0), @(1, 1), @(0, 1))
for ($f = 0; $f -lt 6; $f++) {
  $face = $faces[$f]
  $c = @(($face.n[0] * $W / 2), ($face.n[1] * $H / 2 + $H / 2), ($face.n[2] * $D / 2))
  foreach ($k in $corners) {
    $a = ($k[0] - 0.5) * $face.su; $b = ($k[1] - 0.5) * $face.sv
    $pos += ,@(($c[0] + $face.u[0] * $a + $face.v[0] * $b), ($c[1] + $face.u[1] * $a + $face.v[1] * $b), ($c[2] + $face.u[2] * $a + $face.v[2] * $b))
    $nrm += ,$face.n
    $lu = $k[0]; $lv = $k[1]
    $uv0 += ,@($lu, $lv)
    $col = $f % 3; $row = [math]::Floor($f / 3); $pad = 0.02
    $mu = if ($f -eq 1) { 1 - $lu } else { $lu }
    $uv1 += ,@(($col / 3 + $pad + $mu * (1 / 3 - 2 * $pad)), ($row / 2 + $pad + $lv * (0.5 - 2 * $pad)))
    $uv2 += ,@(($col + 0.1 + 0.8 * $lu), ($row + 0.1 + 0.8 * $lv))
  }
  $base = $f * 4
  $idx += @($base, ($base + 1), ($base + 2), $base, ($base + 2), ($base + 3))
}
foreach ($p in $pos) { foreach ($x in $p) { $bw.Write([single]$x) } }
foreach ($p in $nrm) { foreach ($x in $p) { $bw.Write([single]$x) } }
# glTF texture coordinates: v down (v = 1 - v of the usual UV space)
foreach ($set in @($uv0, $uv1, $uv2)) { foreach ($p in $set) { $bw.Write([single]$p[0]); $bw.Write([single](1 - $p[1])) } }
foreach ($i in $idx) { $bw.Write([uint16]$i) }
$bw.Flush()
$bytes = $ms.ToArray()
$b64 = [Convert]::ToBase64String($bytes)
$gltf = @"
{
  "asset": { "version": "2.0", "generator": "WZX Engine tools/make_uvset_crate.ps1" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "Crate", "mesh": 0 } ],
  "meshes": [ {
    "name": "Crate",
    "extras": { "uvSetNames": ["Tiling", "Lightmap", "UDIM"] },
    "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2, "TEXCOORD_1": 3, "TEXCOORD_2": 4 }, "indices": 5 } ]
  } ],
  "buffers": [ { "byteLength": $($bytes.Length), "uri": "data:application/octet-stream;base64,$b64" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 288 },
    { "buffer": 0, "byteOffset": 288, "byteLength": 288 },
    { "buffer": 0, "byteOffset": 576, "byteLength": 192 },
    { "buffer": 0, "byteOffset": 768, "byteLength": 192 },
    { "buffer": 0, "byteOffset": 960, "byteLength": 192 },
    { "buffer": 0, "byteOffset": 1152, "byteLength": 72 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 24, "type": "VEC3", "min": [$(-$W / 2), 0, $(-$D / 2)], "max": [$($W / 2), $H, $($D / 2)] },
    { "bufferView": 1, "componentType": 5126, "count": 24, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 24, "type": "VEC2" },
    { "bufferView": 3, "componentType": 5126, "count": 24, "type": "VEC2" },
    { "bufferView": 4, "componentType": 5126, "count": 24, "type": "VEC2" },
    { "bufferView": 5, "componentType": 5123, "count": 36, "type": "SCALAR" }
  ]
}
"@
$dir = Join-Path $root "assets\PROPS"
New-Item -ItemType Directory -Force $dir | Out-Null
[System.IO.File]::WriteAllText((Join-Path $dir "Crate_UVSets.gltf"), $gltf, (New-Object System.Text.UTF8Encoding($false)))
"written: $($bytes.Length) bytes of buffer"
