// BrickIt - zlib stream compressor (RFC 1950 / 1951: LZ77 with hash chains, fixed Huffman codes), for the Maya
// export: the web compresses the scene with CompressionStream("deflate") and the script reads it with zlib.decompress.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace brickit {

std::string zlibCompress(const std::string& data);
std::string base64(const std::string& data);
// ZIP archive without compression (stored), names and texts in UTF-8, as the web devZip
std::string zipStored(const std::vector<std::pair<std::string, std::string>>& files);

} // namespace brickit
