# Driving WZX Engine with an AI (MCP)

WZX Engine can be driven by an AI app (Claude Desktop, Claude Code, Cursor, Windsurf...) through the Model Context
Protocol. The AI gets tools to read and change the scene, the materials, the curves, the gameplay graph, to play the
game, to build it, and to look at the viewport.

## Connect an AI app

In the editor: **Tools > AI Control (MCP)…** The window shows the lines to copy.

- Claude Desktop, Cursor, Windsurf: add this to the MCP configuration file (Claude Desktop: Settings > Developer >
  Edit Config), then restart the app.

  ```json
  {
    "mcpServers": {
      "wzx-engine": {
        "command": "C:\\path\\to\\Viewport3D.exe",
        "args": ["--mcp"]
      }
    }
  }
  ```

- Claude Code: `claude mcp add wzx-engine -- "C:\path\to\Viewport3D.exe" --mcp`

The AI app starts `Viewport3D.exe --mcp` (no window): it relays the tools to the editor running on this PC, and starts
the editor if none runs.

## Security

- The command API listens on 127.0.0.1 only (port 7878, or the next free one).
- Every request carries the token of this installation (header `X-WZX-Token` or `Authorization: Bearer`), stored in
  `%LOCALAPPDATA%\WZX Engine\api.json` with the port. Only programs of this Windows user can read it.
- Web pages are refused: no CORS header, and a request whose `Host` is not `127.0.0.1` / `localhost` is refused
  (DNS rebinding).
- **Tools > AI Control (MCP)**: switch the API off, or make a new token (every connected AI app is refused until it
  restarts its MCP server).
- An exported game keeps the API only when its build profile has *Command API* on: leave it off for a game you send.

## Tools

| Area | Tools |
|---|---|
| State, scene | `wzx_state`, `wzx_help`, `wzx_scene_get`, `wzx_scene_new`, `wzx_scene_open`, `wzx_scene_save` |
| Objects | `wzx_object_add` (primitives or game parts), `wzx_object_set`, `wzx_object_delete`, `wzx_object_select`, `wzx_frame` |
| Materials | `wzx_material_list`, `wzx_material_set` |
| Curves | `wzx_curve_add` (points or shapes), `wzx_curve_get`, `wzx_curve_set` |
| View, render | `wzx_camera_set`, `wzx_sun_set`, `wzx_render_set`, `wzx_screenshot` (image), `wzx_workspace` |
| Undo | `wzx_undo`, `wzx_redo` |
| Play | `wzx_play`, `wzx_stop`, `wzx_input_key`, `wzx_input_click`, `wzx_log` |
| Graph | `wzx_graph_node_types`, `wzx_graph_get`, `wzx_graph_add_node`, `wzx_graph_remove_node`, `wzx_graph_connect`, `wzx_graph_disconnect`, `wzx_graph_set_input`, `wzx_graph_set`, `wzx_graph_clear`, `wzx_graph_compile`, `wzx_graph_execute`, `wzx_graph_save`, `wzx_graph_show` |
| RTS village | `wzx_village_mode`, `wzx_rts_list`, `wzx_rts_add`, `wzx_rts_move`, `wzx_rts_rotate`, `wzx_rts_remove` |
| Prefab | `wzx_prefab_info`, `wzx_prefab_new`, `wzx_prefab_open`, `wzx_prefab_save`, `wzx_prefab_place` |
| Build | `wzx_build_profiles`, `wzx_build` (Windows profile or iOS export) |
| Anything else | `wzx_command` (any command of the API, see `wzx_help`) |

Conventions: positions in scene units with Y up, angles in degrees, colours sRGB 0..1, objects by index or name.

## Without MCP

The same commands are plain HTTP: `POST http://127.0.0.1:7878/cmd` with a JSON body `{"cmd": "...", ...}` and the
header `X-WZX-Token`. `GET /help` lists them. Code: `src/game/CommandServer.*` (server), `src/app/AppGame.cpp` and
`src/app/AppAi.cpp` (commands), `src/app/AiControl.*` (token file and MCP bridge).
