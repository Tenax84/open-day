# Debug server (MCP)

`openday --mcp` starts a [Model Context Protocol](https://modelcontextprotocol.io)
server inside the running game. A client such as Claude Code can then take
screenshots, send input, read the state of a battle (units, positions,
status) and give battle orders. It is meant for debugging at run time and
for playing scenarios through automatically.

This is an OpenDay tool; the original game has nothing like it.

## Starting

```
openday --mcp                 # http://127.0.0.1:47800/mcp
openday --mcp-port 48000      # another port
```

The game prints `debug server: http://127.0.0.1:47800/mcp` on start (on
Windows, when started from a terminal). Registering it with Claude Code,
once per project:

```
claude mcp add --transport http openday http://127.0.0.1:47800/mcp
```

The client connects when it starts; if the game was not running then, start
the game and reconnect (`/mcp` in Claude Code).

## Transport and security

- Streamable HTTP transport, JSON responses only (no server-sent event
  stream: `GET` answers 405). Protocol revisions 2024-11-05 to 2025-11-25;
  the tools are the same in all.
- The server listens on the loopback interface (`127.0.0.1`) only and is
  off unless `--mcp` or `--mcp-port` is given. Requests with an `Origin`
  header other than `localhost` / `127.0.0.1` are refused (403), against
  DNS rebinding from web pages. There is no authentication: every program of
  the local user can drive the game while the server runs.
- Each connection has its own thread; the tool calls are queued and carried
  out by the game loop, one at a time, between two frames (the game state
  belongs to the main thread). A call that waits (`wait`, `wait_idle`,
  battle orders) holds the following calls back until it is done.

## Coordinates

`click` and `mouse_move` take logical screen pixels: the coordinates of the
`screenshot` image, 640 × 480 or 1024 × 768 depending on the screen. Battle
tools take map tiles (x, y); `battle_state` gives the screen position of the
units that are in view, `battle_center` moves the view.

## Tools

### General

| Tool | |
|---|---|
| `get_screen` | the screen stack (class names, bottom first), size of the top screen, game speed, battle summary |
| `screenshot` | the last frame as an 8-bit PNG (compressed, about 30-70 KB) |
| `click` | `x`, `y`, `button` (left/right/middle), `hold` (press without release: the info card) |
| `mouse_move` | `x`, `y` (hover, edge scrolling) |
| `key` | `key` (letter, digit, enter, esc, backspace, tab, space, arrows, pageup/pagedown, home/end, f1-f6), `times`, `shift`, `ctrl` |
| `type_text` | `text`, one key press per character |
| `wait` | `ms` of game time |
| `wait_idle` | in a battle: until the animations have played and you can give orders again (or the battle ended); `timeout_ms` (real time, default 120 s) |
| `set_speed` | game speed factor 0.1-20: the elapsed time of each frame is multiplied by it |
| `list_scenarios` | the skirmish catalog: index, title, province, map size, budget, file name |
| `start_skirmish` | `scenario` (index, title or file name), `briefing`: starts it from any screen |
| `main_menu` | back to the main menu from anywhere |

Input tools answer after `wait_ms` of game time (default 150) with the
`get_screen` information; with `screenshot: true` the frame after the wait
is attached.

### Battle

The battle tools find the battle screen anywhere in the screen stack.
Objects are given by their **index** in the battle state (the order of the
server's lists; destroyed objects keep their index). Player slot 0 is the
computer, 1 the local player.

| Tool | |
|---|---|
| `battle_state` | mode, dialog, turn, turn limit, players, current and displayed player, objective (kind, name, turns, progress, ids, zone), budget, income, selection, message strip, the tiles at the corners of the view, and the objects: `include` = units, buildings (default), walls, mines, roads, ruins, deploy (zone, pending reinforcements, automated units); `owner`; `dead` |
| `battle_unit` | one unit with its weapon and scripted order, and the 25 nearest enemy units and buildings with the shot check (`ok`, `out of range`, `no line of fire`, ...) and the hit chance |
| `battle_map` | a text map of a rectangle (default the whole map) with a legend |
| `battle_path` | the path a unit would take to a tile: steps with their cost, total cost, steps affordable this turn, where it stops |
| `battle_center` | centres the view on a tile |
| `battle_deploy_types` | the unit types for sale now, with cost, AP, armor, range |
| `battle_deploy` | `type_id`, `x`, `y`: buys and places a unit (deployment; in your turn a reinforcement) |
| `battle_undeploy` | takes back a unit placed during this deployment |
| `battle_finish_deployment` | CONTINUE: ends the deployment (or the reinforcement purchase) |
| `battle_move` | `units`, `x`, `y`: the interface's two-click move with the same planning (group rules, AP limit, Overwatch reserve) |
| `battle_shoot` | `units`, `x`, `y`: fire; a unit out of range or line of fire walks to a firing tile first when it can |
| `battle_end_turn` | ends the turn; the other side's turn follows |
| `battle_events` | the event log from `since` on (`limit`) |
| `battle_force_result` | `win`: ends the battle (the request of the cheat keys), for testing what follows a battle |

Modes: `deploy` (deployment or buying reinforcements), `tactic` (your turn,
orders possible), `wait` (another player's turn, or animations playing),
`automated` (the automated units view).

Orders go the way of the interface's clicks: they close an open dialog
(goal, options), are refused while another screen (the tactical map, the
army screen) is open over the battle, and wait in the queue while the
animations of the previous order play (up to a minute). Their answer lists
the events they caused (`describeEvent`, at most 300; the rest with
`battle_events`), the battle summary and the ordered units afterwards. With
`wait` (default) a move, shot or end of turn answers once the animations
have played and it is your turn again or the battle is over; the end of the
turn thus includes the whole turn of the computer player.

At the start of a deployment the tactical map is shown over the battle;
close it with `key esc` before the first order.

### Event log

The battle screen keeps every event it receives as one line, for example
`UnitMoved unit=8 to=(10,6) steps=6 facing=7 ap=190` or
`UnitDestroyed unit=3 by=1`. With the environment variable `OPENDAY_LOG` set
the same lines go to the standard error output.

## A session

```
start_skirmish {"scenario": 1}
key {"key": "esc"}                       # close the tactical map
battle_deploy_types {}
battle_deploy {"type_id": 8, "x": 6, "y": 0}
battle_finish_deployment {}
set_speed {"speed": 4}
battle_map {}
battle_move {"units": [8, 9], "x": 10, "y": 6}
battle_unit {"unit": 8}
battle_shoot {"units": [8, 9], "x": 11, "y": 11, "screenshot": true}
battle_end_turn {}
```
