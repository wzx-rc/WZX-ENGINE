// AI control: access file of the command API and the MCP bridge (see AiControl.h).
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "AiControl.h"
#include "Content.h"
#include "Json.h"
#include "WzxRead.h"
#include <bcrypt.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ws2_32.lib")

namespace app {

namespace fs = std::filesystem;

static const int kFirstPort = 7878, kPortCount = 10;   // the editor takes the first free one (GameHost::init)

// --------------------------------------------------------------------------- access file

std::wstring apiFilePath() {
    PWSTR local = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) dir = local;
    CoTaskMemFree(local);
    if (dir.empty()) return L"api.json";
    dir += L"\\WZX Engine";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir + L"\\api.json";
}

bool readApiFile(std::string& token, int& port) {
    std::ifstream f(apiFilePath(), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    Json j;
    if (!Json::parse(ss.str(), j) || j.type != Json::Object) return false;
    token = j["token"].str();
    port = (int)j["port"].num(0);
    return !token.empty();
}

static void writeApiFile(const std::string& token, int port) {
    std::ofstream f(apiFilePath(), std::ios::binary | std::ios::trunc);
    f << "{\"token\": \"" << jsonEscape(token) << "\", \"port\": " << port << ", \"pid\": " << GetCurrentProcessId() << "}\n";
}

static std::string randomToken() {
    unsigned char b[24];
    if (BCryptGenRandom(nullptr, b, sizeof(b), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return std::string();
    static const char* hex = "0123456789abcdef";
    std::string t;
    for (unsigned char c : b) { t.push_back(hex[c >> 4]); t.push_back(hex[c & 15]); }
    return t;
}

std::string apiToken() {
    std::string token;
    int port = 0;
    if (readApiFile(token, port)) return token;
    token = randomToken();
    writeApiFile(token, 0);
    return token;
}

std::string newApiToken() {
    std::string old;
    int port = 0;
    readApiFile(old, port);
    std::string token = randomToken();
    writeApiFile(token, port);
    return token;
}

void writeApiPort(int port) {
    std::string token = apiToken();
    writeApiFile(token, port);
}

static std::wstring exePath() {
    wchar_t p[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, p, (DWORD)(sizeof(p) / sizeof(p[0])));
    return std::wstring(p, n);
}

std::string mcpConfigJson() {
    return "{\n  \"mcpServers\": {\n    \"wzx-engine\": {\n      \"command\": \"" + jsonEscape(toUtf8(exePath())) +
           "\",\n      \"args\": [\"--mcp\"]\n    }\n  }\n}";
}

std::string mcpClaudeCodeCommand() {
    return "claude mcp add wzx-engine -- \"" + toUtf8(exePath()) + "\" --mcp";
}

// --------------------------------------------------------------------------- HTTP client (127.0.0.1)

// POST /cmd to the editor on this port; false when nothing answers there.
static bool postCommand(int port, const std::string& token, const std::string& body, int& status, std::string& response) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;
    sockaddr_in a = {};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    DWORD tmo = 180000;   // a build may take a while (the editor itself gives up after 120 s)
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof tmo);
    if (connect(s, (sockaddr*)&a, sizeof a) != 0) { closesocket(s); return false; }
    std::string req = "POST /cmd HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\nX-WZX-Token: " + token +
                      "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + std::to_string(body.size()) +
                      "\r\n\r\n" + body;
    send(s, req.data(), (int)req.size(), 0);
    std::string data;
    char buf[16384];
    for (;;) {
        int n = recv(s, buf, sizeof buf, 0);
        if (n <= 0) break;
        data.append(buf, n);
    }
    closesocket(s);
    size_t he = data.find("\r\n\r\n"), sp = data.find(' ');
    if (he == std::string::npos || sp == std::string::npos) return false;
    status = atoi(data.c_str() + sp + 1);
    response = data.substr(he + 4);
    return true;
}

// --------------------------------------------------------------------------- MCP tools

struct McpTool {
    const char* name;
    const char* cmd;           // command of the API (nullptr: special)
    const char* description;
    const char* schema;        // JSON schema of the arguments (sent to the API as they are, with "cmd")
};

#define VEC3 R"({"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3})"
#define COLOR R"({"type": "array", "items": {"type": "number"}, "minItems": 3, "maxItems": 3, "description": "sRGB colour, components 0..1"})"
#define TARGET R"({"type": ["integer", "string"], "description": "object index (see wzx_scene_get) or object name"})"
#define NOARGS R"({"type": "object", "properties": {}})"

static const McpTool kTools[] = {
    // state and scene
    {"wzx_state", "state", "State of the editor: Play, selection, object count, graph, project folder, viewport rectangle.", NOARGS},
    {"wzx_help", "help", "Every command of the WZX Engine command API with its parameters (for wzx_command).", NOARGS},
    {"wzx_scene_get", "scene.get", "Objects of the scene (index, name, part, position, yaw, scale, sRGB colour, visibility), camera and selection. Call it first: the other tools take the object index or name.", NOARGS},
    {"wzx_scene_new", "scene.new", "Starts a new scene (built-in Village base, empty graph). Unsaved changes are lost.", NOARGS},
    {"wzx_scene_open", "scene.load", "Opens a .wzx scene file.",
     R"({"type": "object", "properties": {"path": {"type": "string", "description": "file path, relative to the project folder or absolute, default game/scene.wzx"}}})"},
    {"wzx_scene_save", "scene.save", "Saves the scene as a .wzx file.",
     R"({"type": "object", "properties": {"path": {"type": "string", "description": "file path, relative to the project folder or absolute, default game/scene.wzx"}}})"},
    // objects
    {"wzx_object_add", "object.add",
     "Adds an object: a parametric primitive (primitive: cube, sphere, plane, cylinder, torus, disk) or a part of the game library (part: CUBE, SPHERE, BALL...). Units are scene units (Y up). Returns its index and name.",
     R"({"type": "object", "properties": {"primitive": {"type": "string", "enum": ["cube", "sphere", "plane", "cylinder", "torus", "disk"]}, "part": {"type": "string"}, "pos": )" VEC3 R"(, "yaw": {"type": "number", "description": "degrees around Y"}, "scale": )" VEC3 R"(, "color": )" COLOR R"(, "name": {"type": "string"}}})"},
    {"wzx_object_set", "object.set", "Changes an object: position, yaw (absolute, degrees), rotate (relative, degrees), scale, sRGB colour, name, visibility, material (index or name, see wzx_material_list).",
     R"({"type": "object", "required": ["target"], "properties": {"target": )" TARGET R"(, "pos": )" VEC3 R"(, "yaw": {"type": "number"}, "rotate": {"type": "number"}, "scale": )" VEC3 R"(, "color": )" COLOR R"(, "name": {"type": "string"}, "visible": {"type": "boolean"}, "material": {"type": ["integer", "string"]}}})"},
    {"wzx_object_delete", "object.delete", "Deletes an object (undo: wzx_undo).", R"({"type": "object", "required": ["target"], "properties": {"target": )" TARGET R"(}})"},
    {"wzx_object_select", "object.select", "Selects an object (null: clears the selection).", R"({"type": "object", "properties": {"target": )" TARGET R"(}})"},
    {"wzx_frame", "view.frame", "Frames the camera on an object (selected first) or on the selection.", R"({"type": "object", "properties": {"target": )" TARGET R"(}})"},
    // materials
    {"wzx_material_list", "material.list", "Materials of the scene: index, name, sRGB base colour, roughness, metallic, specular, transmission, emission, coat, subsurface, and the objects using each.", NOARGS},
    {"wzx_material_set", "material.set", "Changes a material (every object using it changes). Values 0..1 unless noted.",
     R"({"type": "object", "required": ["target"], "properties": {"target": {"type": ["integer", "string"], "description": "material index or name"}, "name": {"type": "string"}, "baseColor": )" COLOR R"(, "roughness": {"type": "number"}, "metallic": {"type": "number"}, "specular": {"type": "number"}, "transmission": {"type": "number"}, "ior": {"type": "number", "description": "1..3"}, "emission": )" COLOR R"(, "emissionStrength": {"type": "number", "description": ">= 0"}, "coat": {"type": "number"}, "coatRoughness": {"type": "number"}, "subsurface": {"type": "number"}}})"},
    // curves
    {"wzx_curve_add", "curve.add",
     "Adds a curve object: either points (type linear, cv, ep or bezier; local to pos) or a shape (line, circle, square, arc, helix, spiral, star). Curves get a tube mesh in the editor.",
     R"({"type": "object", "properties": {"type": {"type": "string", "enum": ["linear", "cv", "ep", "bezier"]}, "points": {"type": "array", "items": )" VEC3 R"(}, "closed": {"type": "boolean"}, "shape": {"type": "string", "enum": ["line", "circle", "square", "arc", "helix", "spiral", "star"]}, "pos": )" VEC3 R"(, "name": {"type": "string"}}})"},
    {"wzx_curve_get", "curve.get", "Points of a curve object (local to its position), type, closed.", R"({"type": "object", "required": ["target"], "properties": {"target": )" TARGET R"(}})"},
    {"wzx_curve_set", "curve.set", "Replaces the points, type or closed flag of a curve object.",
     R"({"type": "object", "required": ["target"], "properties": {"target": )" TARGET R"(, "type": {"type": "string", "enum": ["linear", "cv", "ep", "bezier"]}, "points": {"type": "array", "items": )" VEC3 R"(}, "closed": {"type": "boolean"}}})"},
    // view and rendering
    {"wzx_camera_set", "camera.set", "Orbit camera of the viewport: target point, yaw and pitch in degrees (positive pitch looks down, -89..89), distance from the target in scene units.",
     R"({"type": "object", "properties": {"target": )" VEC3 R"(, "yaw": {"type": "number"}, "pitch": {"type": "number"}, "distance": {"type": "number"}}})"},
    {"wzx_sun_set", "sun.set", "Sun of the scene: azimuth and elevation in degrees.", R"({"type": "object", "properties": {"azimuth": {"type": "number"}, "elevation": {"type": "number"}}})"},
    {"wzx_render_set", "render.set", "Render mode of the viewport (lit = real-time raster, metal = Lit + GI probes as on iOS, progressive = path tracing, gi = ray traced probes, splitrc = Split Radiance Cascades) and exposure.",
     R"({"type": "object", "properties": {"mode": {"type": "string", "enum": ["lit", "metal", "progressive", "gi", "splitrc"]}, "exposure": {"type": "number"}}})"},
    {"wzx_screenshot", nullptr, "Image of the viewport (or of the whole editor window with window: true), to check the result of a change.",
     R"({"type": "object", "properties": {"window": {"type": "boolean"}}})"},
    {"wzx_undo", "undo", "Undoes the last change of the scene.", NOARGS},
    {"wzx_redo", "redo", "Redoes the last undone change.", NOARGS},
    {"wzx_workspace", "workspace.set", "Shows a workspace of the editor.", R"({"type": "object", "required": ["name"], "properties": {"name": {"type": "string", "enum": ["scene", "graph", "materials", "prefab"]}}})"},
    // game
    {"wzx_play", "play", "Starts Play (the game runs its graph in the viewport).", NOARGS},
    {"wzx_stop", "stop", "Stops Play (the scene comes back as before).", NOARGS},
    {"wzx_input_key", "input.key", "During Play: a key press for the graph (R, C, SPACE...).", R"({"type": "object", "required": ["key"], "properties": {"key": {"type": "string"}}})"},
    {"wzx_input_click", "input.click", "During Play: a click on an object or on the ground.", R"({"type": "object", "properties": {"target": )" TARGET R"(, "pos": )" VEC3 R"(}})"},
    {"wzx_log", "log", "Output log of the graph and its variables.", NOARGS},
    // graph (visual scripting)
    {"wzx_graph_node_types", "graph.nodeTypes", "Node types of the visual scripting graph with their input and output pins.", NOARGS},
    {"wzx_graph_get", "graph.get", "Graph of the scene: nodes, values, links.", NOARGS},
    {"wzx_graph_add_node", "graph.addNode", "Adds a node; returns its id.",
     R"({"type": "object", "required": ["type"], "properties": {"type": {"type": "string"}, "x": {"type": "number"}, "y": {"type": "number"}, "in": {"type": "object", "description": "input pin name -> value"}}})"},
    {"wzx_graph_remove_node", "graph.removeNode", "Removes a node.", R"({"type": "object", "required": ["id"], "properties": {"id": {"type": "integer"}}})"},
    {"wzx_graph_connect", "graph.connect", "Links an output pin to an input pin: from [node id, output name or index], to [node id, input name or index].",
     R"({"type": "object", "required": ["from", "to"], "properties": {"from": {"type": "array"}, "to": {"type": "array"}}})"},
    {"wzx_graph_disconnect", "graph.disconnect", "Removes the links of a pin.", R"({"type": "object", "required": ["id", "pin"], "properties": {"id": {"type": "integer"}, "pin": {"type": ["integer", "string"]}, "output": {"type": "boolean"}}})"},
    {"wzx_graph_set_input", "graph.setInput", "Sets the value of an input pin.", R"({"type": "object", "required": ["id", "pin", "value"], "properties": {"id": {"type": "integer"}, "pin": {"type": ["integer", "string"]}, "value": {}}})"},
    {"wzx_graph_set", "graph.set", "Replaces the whole graph (same format as wzx_graph_get).", R"({"type": "object", "required": ["graph"], "properties": {"graph": {"type": "object"}}})"},
    {"wzx_graph_clear", "graph.clear", "Empties the graph.", NOARGS},
    {"wzx_graph_compile", "graph.compile", "Checks the graph (errors and warnings in the Compiler Results).", NOARGS},
    {"wzx_graph_execute", "graph.execute", "Dry run of the graph (Execute), its path shown in the editor.", NOARGS},
    {"wzx_graph_save", "graph.save", "Saves the graph file.", NOARGS},
    {"wzx_graph_show", "graph.show", "Opens or closes the graph editor.", R"({"type": "object", "properties": {"open": {"type": "boolean"}}})"},
    // RTS village
    {"wzx_village_mode", "village.mode", "Village mode of the editor (houses placed on the RTS grid).", R"({"type": "object", "required": ["on"], "properties": {"on": {"type": "boolean"}}})"},
    {"wzx_rts_list", "rts.list", "Houses of the RTS village and the house types.", NOARGS},
    {"wzx_rts_add", "rts.add", "Adds a house (village mode or Play): type index, position on the ground.", R"({"type": "object", "properties": {"type": {"type": "integer"}, "pos": )" VEC3 R"(}})"},
    {"wzx_rts_move", "rts.move", "Moves a house to a ground position.", R"({"type": "object", "required": ["index", "pos"], "properties": {"index": {"type": "integer"}, "pos": )" VEC3 R"(}})"},
    {"wzx_rts_rotate", "rts.rotate", "Turns a house by 90 degrees.", R"({"type": "object", "required": ["index"], "properties": {"index": {"type": "integer"}}})"},
    {"wzx_rts_remove", "rts.remove", "Removes a house.", R"({"type": "object", "required": ["index"], "properties": {"index": {"type": "integer"}}})"},
    // prefab
    {"wzx_prefab_info", "prefab.info", "Prefab open in the Prefab workspace: name, file, parts, clips.", NOARGS},
    {"wzx_prefab_new", "prefab.new", "New prefab (demo: true = the Outpost sample).", R"({"type": "object", "properties": {"demo": {"type": "boolean"}}})"},
    {"wzx_prefab_open", "prefab.open", "Opens a .prefab file.", R"({"type": "object", "required": ["path"], "properties": {"path": {"type": "string"}}})"},
    {"wzx_prefab_save", "prefab.save", "Saves the prefab (path: a .prefab file, default its current file or game/prefabs/<name>.prefab).", R"({"type": "object", "properties": {"path": {"type": "string"}}})"},
    {"wzx_prefab_place", "prefab.place", "Places the prefab in the scene (a group at the camera target).", NOARGS},
    // build
    {"wzx_build_profiles", "build.profiles", "Build profiles (Windows).", NOARGS},
    {"wzx_build", "build", "Builds the game: a Windows profile, or platform ios (writes game/export for the iOS workflow). Returns the folder.",
     R"({"type": "object", "properties": {"profile": {"type": "string"}, "run": {"type": "boolean"}, "platform": {"type": "string", "enum": ["windows", "ios"]}}})"},
    // anything else
    {"wzx_command", nullptr, "Any command of the API (see wzx_help): cmd and its arguments.",
     R"({"type": "object", "required": ["cmd"], "properties": {"cmd": {"type": "string"}, "args": {"type": "object"}}})"},
};

static const char* kInstructions =
    "WZX Engine is a game editor (RTS games) running on this PC. Start with wzx_scene_get to see the objects; tools take an "
    "object index or name. Positions are scene units with Y up, angles in degrees, colours sRGB 0..1. After a visual change, "
    "check it with wzx_screenshot. Every scene change can be undone with wzx_undo. Gameplay is a node graph "
    "(wzx_graph_node_types, wzx_graph_add_node, wzx_graph_connect), tested with wzx_play / wzx_input_* / wzx_log / wzx_stop.";

static std::string base64(const std::vector<unsigned char>& d) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((d.size() + 2) / 3 * 4);
    for (size_t i = 0; i < d.size(); i += 3) {
        unsigned v = d[i] << 16 | (i + 1 < d.size() ? d[i + 1] << 8 : 0) | (i + 2 < d.size() ? d[i + 2] : 0);
        o.push_back(t[v >> 18 & 63]);
        o.push_back(t[v >> 12 & 63]);
        o.push_back(i + 1 < d.size() ? t[v >> 6 & 63] : '=');
        o.push_back(i + 2 < d.size() ? t[v & 63] : '=');
    }
    return o;
}

class McpBridge {
public:
    int run() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        m_in = GetStdHandle(STD_INPUT_HANDLE);
        m_out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (m_in == INVALID_HANDLE_VALUE || m_in == nullptr || m_out == INVALID_HANDLE_VALUE || m_out == nullptr) return 1;
        std::string pending;
        char buf[65536];
        for (;;) {
            DWORD n = 0;
            if (!ReadFile(m_in, buf, sizeof buf, &n, nullptr) || n == 0) break;   // the client closed stdin: exit
            pending.append(buf, n);
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, nl);
                pending.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.find_first_not_of(" \t") != std::string::npos) handle(line);
            }
        }
        WSACleanup();
        return 0;
    }

private:
    HANDLE m_in = nullptr, m_out = nullptr;
    int m_port = 0;
    std::string m_token;

    void send(const std::string& msg) {
        std::string m = msg + "\n";
        DWORD w = 0;
        WriteFile(m_out, m.data(), (DWORD)m.size(), &w, nullptr);
    }
    void reply(const Json& id, const std::string& result) { send("{\"jsonrpc\": \"2.0\", \"id\": " + jsonText(id) + ", \"result\": " + result + "}"); }
    void error(const Json& id, int code, const std::string& msg) {
        send("{\"jsonrpc\": \"2.0\", \"id\": " + jsonText(id) + ", \"error\": {\"code\": " + std::to_string(code) + ", \"message\": \"" + jsonEscape(msg) + "\"}}");
    }

    void handle(const std::string& line) {
        Json m;
        if (!Json::parse(line, m) || m.type != Json::Object) {
            send("{\"jsonrpc\": \"2.0\", \"id\": null, \"error\": {\"code\": -32700, \"message\": \"parse error\"}}");
            return;
        }
        const std::string method = m["method"].str();
        const Json& id = m["id"];
        const bool request = id.type == Json::Number || id.type == Json::String;
        if (!request) return;   // notifications (initialized, cancelled...): nothing to answer
        if (method == "initialize") {
            std::string version = m["params"]["protocolVersion"].str("2025-06-18");
            reply(id, "{\"protocolVersion\": \"" + jsonEscape(version) + "\", \"capabilities\": {\"tools\": {\"listChanged\": false}}, "
                      "\"serverInfo\": {\"name\": \"wzx-engine\", \"title\": \"WZX Engine\", \"version\": \"0.4\"}, \"instructions\": \"" +
                      jsonEscape(kInstructions) + "\"}");
        } else if (method == "ping") {
            reply(id, "{}");
        } else if (method == "tools/list") {
            std::string r = "{\"tools\": [";
            bool first = true;
            for (const McpTool& t : kTools) {
                r += std::string(first ? "" : ", ") + "{\"name\": \"" + t.name + "\", \"description\": \"" + jsonEscape(t.description) +
                     "\", \"inputSchema\": " + t.schema + "}";
                first = false;
            }
            reply(id, r + "]}");
        } else if (method == "tools/call") {
            callTool(id, m["params"]);
        } else if (method == "resources/list") {
            reply(id, "{\"resources\": []}");
        } else if (method == "prompts/list") {
            reply(id, "{\"prompts\": []}");
        } else {
            error(id, -32601, "method not found: " + method);
        }
    }

    static std::string textResult(const std::string& text, bool isError) {
        return "{\"content\": [{\"type\": \"text\", \"text\": \"" + jsonEscape(text) + "\"}], \"isError\": " + (isError ? "true" : "false") + "}";
    }

    // Sends a command to the running editor (found through api.json, else on the usual ports, else started).
    bool command(const std::string& body, int& status, std::string& response, std::string& err) {
        std::string token;
        int port = 0;
        readApiFile(token, port);
        m_token = token;
        if (m_port && postCommand(m_port, m_token, body, status, response)) return true;
        if (port && postCommand(port, m_token, body, status, response)) { m_port = port; return true; }
        for (int p = kFirstPort; p < kFirstPort + kPortCount; ++p)
            if (p != port && postCommand(p, m_token, body, status, response)) { m_port = p; return true; }
        // no editor: start one (this exe without --mcp) and wait for its API
        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        std::wstring exe = exePath(), cmdLine = L"\"" + exe + L"\"";
        if (!CreateProcessW(exe.c_str(), &cmdLine[0], nullptr, nullptr, FALSE, 0, nullptr, fs::path(exe).parent_path().c_str(), &si, &pi)) {
            err = "WZX Engine is not running and could not be started";
            return false;
        }
        CloseHandle(pi.hThread);
        for (int k = 0; k < 120; ++k) {   // up to 60 s
            Sleep(500);
            DWORD code = 0;
            if (GetExitCodeProcess(pi.hProcess, &code) && code != STILL_ACTIVE) break;
            if (readApiFile(token, port) && port) {
                m_token = token;
                if (postCommand(port, m_token, body, status, response)) { m_port = port; CloseHandle(pi.hProcess); return true; }
            }
        }
        CloseHandle(pi.hProcess);
        err = "WZX Engine was started but its command API does not answer: check Tools > AI Control (MCP) in the editor";
        return false;
    }

    void callTool(const Json& id, const Json& params) {
        const std::string name = params["name"].str();
        const Json& args = params["arguments"];
        const McpTool* tool = nullptr;
        for (const McpTool& t : kTools) if (name == t.name) tool = &t;
        if (!tool) { error(id, -32602, "unknown tool: " + name); return; }
        // body: the arguments with "cmd"
        Json body;
        if (name == "wzx_command") {
            body = args["args"].type == Json::Object ? args["args"] : Json();
            body.type = Json::Object;
            body.o["cmd"] = args["cmd"];
        } else {
            body = args.type == Json::Object ? args : Json();
            body.type = Json::Object;
            Json c;
            c.type = Json::String;
            c.s = tool->cmd ? tool->cmd : "screenshot";
            body.o["cmd"] = c;
        }
        int status = 0;
        std::string response, err;
        if (!command(jsonText(body), status, response, err)) { reply(id, textResult(err, true)); return; }
        Json r;
        const bool parsed = Json::parse(response, r) && r.type == Json::Object;
        const bool ok = status == 200 && parsed && r["ok"].type == Json::Bool && r["ok"].b;
        if (name == "wzx_screenshot" && ok) {
            std::ifstream f(fromUtf8(r["path"].str()), std::ios::binary);
            std::vector<unsigned char> png((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (!png.empty()) {
                reply(id, "{\"content\": [{\"type\": \"image\", \"data\": \"" + base64(png) + "\", \"mimeType\": \"image/png\"}, {\"type\": \"text\", \"text\": \"" +
                              jsonEscape(r["path"].str()) + "\"}], \"isError\": false}");
                return;
            }
        }
        reply(id, textResult(response, !ok));
    }
};

int runMcpBridge() {
    McpBridge b;
    return b.run();
}

} // namespace app
