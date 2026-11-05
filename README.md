# EMBERFALL

An original **open-world MMORPG for PC**, built toward Unreal Engine 5.5.
The intended experience is a shared persistent realm: players explore the same
continent, rebuild settlements, form guilds, enter party dungeons, and fight
cooperative world bosses. Best combat gear is earned through play, never sold.

**Current deliverable: an early native C++ shared-world foundation, not a finished
MMO.** It includes replication and dedicated-server target source, but Unreal is
not installed in the development cloud machine. No Unreal build, multiplayer
session, packaged executable, or gameplay footage has been verified here.

## Included source

- A small runtime-generated overworld blockout with a sanctuary/forge, story beacon,
  cooperative boss arena, and a physically separate opt-in PvP training zone.
- A third-person character: movement, melee strike, heavy attack, guard, dodge,
  self-heal, death recovery, and a Canvas HUD. Art uses engine primitives.
- A shared Cinderwarden encounter: 600 health, three phases, targeted ground
  impacts and a ring attack with an inner safe area. Telegraphs are rendered mesh
  outlines, not debug-only indicators.
- Server-owned damage, stamina, cooldowns, encounter state, reward eligibility,
  and upgrades. Clients request actions rather than providing damage/loot amounts.
- Personal rewards for damage contributors: one Ember Sigil per clear. The first
  upgrade costs three sigils; the second costs six more. Press E near the defeated
  boss to repeat the event; kills are not first-tag or last-hit owned.
- PvP damage requires **both players to opt in and both to be within the Red March**.
  Gear damage is normalized in PvP. Player capsules ignore other players to prevent
  body-blocking. The PvE boss does not flag participants for PvP.
- Standalone local saves remain available as an offline iteration mode.
  Authenticated network play now
  uses the SQLite realm service for durable characters, resources, and upgrades.
- Account registration/sign-in, one-use join tickets, heartbeat-renewed character
  leases, and atomic persistent rewards. Replaying a reward request cannot grant
  it twice. A new admission cannot reuse an expired server's lease.
- Persistent parties (five members) and guild membership (50 members), with
  leader invitations, recipient-only acceptance, and leader succession via the CLI.
- Party-bound Drowned Archive rooms, entrance/exit flow, Keeper Serael boss,
  wipe recovery, and personal dungeon rewards in Unreal source. Four development
  rooms use isolated coordinates in the same process; these are not separate
  dungeon servers or a full authored dungeon campaign.
- Emberstone gathering nodes with server proximity/cooldown checks and a persistent
  resource counter. World bosses automatically return after 30 seconds; players
  can also replay a defeated encounter sooner.

The development connection cap is 16, **not a tested capacity**. Seamless streaming
terrain, cross-process realm routing, raid content, scheduled PvP events, support-role
contribution, world-event overflow, full social UI/chat, anti-cheat, production
operations, and commercial content remain unfinished. The local service is a
single-machine development implementation, not an MMO fleet. No shop or payment
integration is included; a no-pay-to-win policy alone is not an anti-RMT system.

See [game direction](docs/GAME_DESIGN.md) for the original opening story, two
proposed five-player dungeons, later raids, world bosses, progression, and economy.
See [architecture](docs/ARCHITECTURE.md) for shared realms, dedicated servers,
persistence, and measured scaling. Inspiration means broad design qualities;
this project does not reuse another game's lore, characters, or assets.

## Open in Unreal on Windows

1. Install **Unreal Engine 5.5**, Visual Studio 2022, the C++ game-development
   workload, and the compatible Windows SDK from Epic's UE 5.5 prerequisites.
2. Right-click `Emberfall.uproject` and generate Visual Studio project files.
3. Build **Development Editor / Win64** (`EmberfallEditor`), then open the project.
4. The default `/Engine/Maps/Entry` map is intentionally empty. The native game
   mode builds the blockout at runtime. Press **Play**; do not save a map over
   engine content.

The repository contains no binary map, marketplace content, or starter-content
dependency. Engine primitives are referenced in constructors and included in cook
settings. Cook/package still requires validation in Unreal. The `5.5` association
and target include-order are deliberate; switching engine versions requires a
fresh build and validation.

Equivalent PowerShell commands, after installing the engine/toolchain:

```powershell
$UE = 'C:\Program Files\Epic Games\UE_5.5'
$Project = (Resolve-Path '.\Emberfall.uproject').Path
& "$UE\Engine\Build\BatchFiles\Build.bat" EmberfallEditor Win64 Development "-Project=$Project" -WaitMutex
& "$UE\Engine\Binaries\Win64\UnrealEditor.exe" $Project
```

## Play the first loop

| Input | Action |
| --- | --- |
| WASD / mouse | Move / look |
| Left mouse | Melee strike; face and approach the target |
| Right mouse held | Guard; spends stamina when hit |
| Q | Heavy strike, 22 stamina, six-second cooldown |
| Space | Directional dodge, 28 stamina, short invulnerability window |
| R | Mend 35 health, 25 stamina, twelve-second cooldown |
| E | Interact with nearby beacon, forge, or defeated boss |
| P | Toggle PvP consent; changes blocked shortly after combat |
| Escape | Release/reacquire mouse |

Start at Dawn's Rest. Follow the beacon ahead at **(700, 0)** and press E.
Continue east to the Cinderwarden at **(2500, 0)**. Move out of its marked impacts;
in later phases move inside the safe ring for Cinder Halo. Strike between attacks.
Defeat returns you to the sanctuary after five seconds without losing earned gear.

Every eligible damage contributor receives a sigil, even if another player lands
the final blow. E within 500 units of the fallen guardian resets it for another
attempt. Return to the sanctuary and press E to forge when you have enough sigils.
Three clears buy Tempered; six additional clears buy Dawnbound. There is no paid
shortcut. These short prototype thresholds are not final MMO balance.

The Archive entrance is south at **(0, -1500)**. Light the beacon first, leave
combat, and press E at its circle. Standalone can enter solo. In network play the
party leader enters for the current roster; all members must be authenticated,
chapter-ready, alive, and within 350 units of the entrance on the same server.
Inside, defeat Keeper Serael and return to the entrance circle [E] to exit.
The first room reuses the boss attack patterns and placeholder art. It is not yet
a multi-boss, hand-authored dungeon. Empty rooms close after 30 seconds.

Emberstone seams are marked at **(-500, 1000)** and **(800, -900)**. E nearby gathers
one resource per 15-second personal cooldown. These resources are tracked in
inventory; the current combat upgrades still require earned boss sigils.

The Red March is north at **(0, 2500)**. Both players must deliberately enable PvP
and enter its marked boundary for player damage. No main-story objective requires
entering. Scheduled PvP events are a design proposal, not included functionality.

## Persistent realm service

The new Python standard-library backend is runnable without Unreal or third-party
packages. Follow [realm setup and account instructions](docs/REALM_SERVICE.md) to
start the service, register/sign in, create parties/guilds, and get a join ticket.
Passwords stay in private prompts. The game-server credential belongs only to the
trusted Unreal server and the backend; do not distribute it with player clients.

```bash
python3 -m unittest discover -s backend -p 'test_*.py' -v
```

Backend tests verify real HTTP requests and disposable SQLite databases, including
restart durability, simultaneous duplicate rewards, stale leases, and social
permissions. Networking in Unreal is a separate, still-unrun validation gate.

## Two-client shared-world test

**Start and configure the realm service first** using the instructions above.
The trusted server needs `EMBER_BACKEND_URL` and `EMBER_SERVER_TOKEN` in its process
environment. Each network client needs its own account and one-use ticket. After
joining, open the Unreal console and enter `Login <ticket>`. Gameplay stays disabled
until the server loads that character. No account password is sent through Unreal.
Standalone Play mode remains available for offline local-save iteration.

In the editor Play dropdown: choose **Number of Players = 2**, **Net Mode = Play
As Client**, and enable launching a separate server. Use separate windows. First
test locally; LAN/remote sessions need the appropriate network routing.

Alternatively, after building the editor, start these in separate PowerShell
terminals using the same `$UE` and `$Project` values:

```powershell
# Host one shared world. This is a listen host, not a production realm service.
& "$UE\Engine\Binaries\Win64\UnrealEditor.exe" $Project '/Engine/Maps/Entry?listen' -game -log -port=7777
# Join that same world, once per additional player window.
& "$UE\Engine\Binaries\Win64\UnrealEditor.exe" $Project '127.0.0.1:7777' -game -log
```

For a non-rendering development server, use the editor's dedicated mode:

```powershell
& "$UE\Engine\Binaries\Win64\UnrealEditor.exe" $Project /Engine/Maps/Entry -server -log -port=7777
```

For a **standalone dedicated executable**, `EmberfallServer.Target.cs` is included.
Use a **source-built UE 5.5 engine with server targets supported**; Epic Launcher
engine distributions generally cannot build standalone server targets. Build
`EmberfallServer Win64 Development`, then cook and stage server content using
that engine's AutomationTool. A build target alone is not a packaged deployment.
See [validation checklist](docs/VALIDATION.md) before calling networking working.

Developer-operated Unreal servers can run on rented infrastructure or owned
hardware. Unreal and self-hosting are compatible; owning hardware is not inherently
cheaper. The local admission service is implemented; production deployment, account recovery,
moderation, and fleet operations are still required.

## Linux developer helpers

With a compatible installed Linux UE 5.5 engine/toolchain:

```bash
export UE_ROOT=/path/to/UnrealEngine
./scripts/ue.sh build-editor
./scripts/ue.sh editor
# Separate terminals after building:
./scripts/ue.sh server
./scripts/ue.sh join 127.0.0.1:7777
```

These commands have not been executed against Unreal in this cloud machine.
Future tasks should use the existing checkout; no additional Git worktree is needed.

## Checks executed here

```bash
./scripts/test_rules.sh
python3 -m unittest discover -s backend -p 'test_*.py' -v
git diff --check
```

The native C++17 test runs **66 assertions** covering all 16 PvP consent/boundary
combinations, earned upgrade costs, a complete nine-clear progression, invalid
values, reward overflow, and ability costs. It compiles the same `EmberRules.h`
used by Unreal gameplay. This validates those rules, **not Unreal integration**.
The **55 backend/client tests** also pass authentication, durable progression,
party/guild, privacy, rate-limit, and concurrent-idempotency checks. An Unreal compile, cook, runtime
combat test, and real multiplayer session remain unrun. The next engineering gate is the checklist in `docs/VALIDATION.md`.
