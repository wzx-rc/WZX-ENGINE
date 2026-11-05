// AI control of the editor: the access file of the local command API (token and port, shared by the editor and the
// MCP bridge) and the MCP bridge itself (Viewport3D.exe --mcp), so that an AI app (Claude, Cursor...) drives a running
// editor through the Model Context Protocol. The command API and its commands: GameHost (AppGame.cpp).
#pragma once
#include <string>

namespace app {

// %LOCALAPPDATA%\WZX Engine\api.json: {"token": ..., "port": ..., "pid": ...}. Readable by this Windows user only
// (default rights of the user profile). The token is made once per installation; newApiToken() revokes it.
std::wstring apiFilePath();
std::string apiToken();                  // the token of this installation, created on first use
std::string newApiToken();               // a new token (the AI apps connected with the old one are refused)
void writeApiPort(int port);             // the editor listening on this port (0: none)
bool readApiFile(std::string& token, int& port);

// MCP server over stdin / stdout (JSON-RPC 2.0, one message per line); returns the exit code.
int runMcpBridge();

// Configuration lines for the AI apps (the path of this exe with --mcp).
std::string mcpConfigJson();             // "mcpServers" block (Claude Desktop, Cursor, Windsurf...)
std::string mcpClaudeCodeCommand();      // claude mcp add ...

} // namespace app
