# Emberfall: production architecture and migration path

## What exists today

The repository contains a native Unreal Engine 5 C++ PC foundation and a local
Python/SQLite realm service. The Unreal source includes a replicated overworld,
opening beacon, combat, phased cooperative boss, earned upgrades, gathering, and
opt-in PvP rules. Its development connection cap is 16; this is a configuration
limit, not a measured supported population. The world-boss source automatically
reschedules the encounter after 30 seconds and grants eligible contributors
personal rewards. There is no overflow service or production event scheduler.

The realm service implements account registration and sessions, one-use join
tickets, server-held character leases, durable progression, deduplicated mutation
requests, and party/guild membership. The standard-library CLI manages accounts,
tickets, and social actions. Backend tests have passed; see `VALIDATION.md` for
the current test record. Standalone play retains local `USaveGame` convenience
saves, separate from server-backed multiplayer characters.

A Drowned Archive blockout exists in Unreal source: up to five party members enter
one of four spatially separated rooms in the same game-server process. Each room
uses Keeper Serael, a renamed reuse of the existing boss pattern. This is not a
complete authored dungeon, a separately hosted instance, or a raid. Guild records
do not yet provide guild gameplay, chat, banks, or a complete in-game social UI.

Compilation and playtesting require an installed compatible Unreal toolchain;
consult the README for actual validation results. Source replication declarations,
server build targets, and implementation intent alone do not demonstrate a
successful build, functioning multiplayer, or MMO capacity. Unreal integration,
including backend admission, saves, reconnects, dungeon entry, and replication,
has not been compiled or exercised in-engine. Python service tests verify the
backend boundary; they do not establish those engine outcomes.
Local saves are editable and cannot become authoritative production characters.
The sections below distinguish the local implementation from the proposed
production architecture and its acceptance criteria.

## Engine and hosting are separate choices

The proposed PC game uses Unreal Engine 5 clients and dedicated Unreal server
processes operated by the developer. Those processes can run on rented cloud
machines, managed game hosting, or owned hardware. Using Unreal does not require
another company to own the game's accounts, inventory, or economy.

Start with rented capacity and measure demand. Owning hardware may become useful
at stable utilization, but adds procurement, networking, failures, deployment,
security, and on-call work. No hosting option guarantees higher profit. Compare
total operating cost against latency, reliability, staffing, and player density.
Review current engine licensing and hosting terms before commercial commitments.

## Target authoritative game simulation

Current source routes combat and progression decisions through server-owned
gameplay and backend requests. The following is the production direction; GAS,
prediction, Data Assets, and large-scale interest management are not implemented
merely by having these contracts documented.

- UE5 dedicated servers own movement validation, damage, healing, cooldowns,
  resource costs, boss phases, contribution, loot eligibility, and PvP rules.
- Clients submit intentions such as move, cast, interact, and equip. They never
  submit accepted damage totals, item grants, currencies, or encounter completion.
- Use C++ and Gameplay Ability System for reusable ability effects and tags.
  Predict responsive local actions; reconcile with server decisions. Never trust
  a predicted hit or cooldown completion as proof it happened.
- Keep identity, inventory, and encounter rules independent of presentation.
  Data Assets describe balance; version identifiers make changes traceable.
- Apply interest management and measured replication budgets. Benchmark candidate
  UE replication systems with the actual abilities and encounter density before
  selecting one; engine defaults do not establish MMO-scale capacity.

## Services and persistence

The local realm service stores accounts and character state in a transactional
SQLite database. Passwords use salted PBKDF2 hashes; the CLI saves account sessions
privately and requests 90-second one-use join tickets. A trusted Unreal server
consumes a ticket to obtain a character lease. Client sessions cannot call the
server-only mutation endpoints, and the public client has no database connection.

Leases prevent concurrent admissions for a character and expire after 60 seconds;
the Unreal source renews them every 20 seconds. Mutations serialize database writes
and deduplicate request IDs, including across service restart. Fixed operations
cover chapter progress, boss/dungeon rewards, earned forging, and gathering one
emberstone. Callers cannot provide an arbitrary currency award. Party and guild
records persist alongside character data; CLI invitations require the current
leader and acceptance by the named recipient.

These are tested local-service behaviors. The game integration still requires
real disconnect, retry, lease-expiry, server-crash, and reconnect tests. The
development service binds to loopback HTTP; remote deployment requires HTTPS and
additional operations and security work. See `REALM_SERVICE.md` for exact setup.

For production, extend the transactional store for full accounts, inventory,
currency, entitlements, and encounter rewards. A cache can accelerate
lookups but must not be the sole durable source for owned items or balances.
Keep secrets in managed configuration and grant each service limited access.

The current mutation ledger handles fixed progression operations, not a complete
tradable-item economy. Production item ownership changes and currency deductions
must commit atomically. Every reward needs a unique encounter/character/grant key
so retries cannot award it twice. Add an append-only economy audit trail, monitor
conservation rules, and test backup restoration. Persist checkpoints without
writing every simulation tick.
Define crash recovery and transfer ownership before enabling player trading.

## One shared open world, dungeons, and fair boss access

The production goal is a persistent open-world MMO: many people inhabit the same
logical realm, meet in its shared overworld, and form parties for dungeons and
bosses. A realm is the player's community and shared world state, not a promise
that every actor runs inside one operating-system process. The replicated
foundation still needs build, client/server, load, and end-to-end persistence
validation before it supports the intended persistent world.

Use regional game-server processes to own adjacent areas of the overworld and
coordinate boundary handoffs. Keep players on the same realm and preserve friends,
guilds, parties, event announcements, and world progression across regions. Avoid
silently placing friends in invisible copies of the same place. Seamless handoffs
are a substantial networking feature that must be measured and tested; they are
not supplied merely by selecting Unreal or setting a large connection limit.

Production party dungeons will run as explicit instances joined from the shared
world. A party stays together, retains realm identity, and returns to its overworld after the
instance. Shared world bosses remain cooperative public events. Cross-region and
dungeon transfers use a single ownership lease and a versioned character snapshot
so two servers cannot independently spend the same inventory. The current room
blockout teleports parties within one process; cross-process transfers and dungeon
fleets remain proposed work.

Cooperative world-boss zones enforce PvE rules server-side. No hostile player
damage, forced flagging, player collision blockade, or first-tag ownership.
Eligible players receive personal rewards under transparent contribution rules
that count useful healing and defensive play as well as damage. Add an accessible
recovery path for disconnected participants; the exact rules need playtesting.

Prefer the shared realm's public encounter while capacity is safe. If measured
limits require overflow, make the extra encounter explicit and preserve social
continuity: party and friend joining, visible placement, shared event timing,
equivalent eligibility and rewards, and a reliable return to the same overworld.
Do not split an existing party. Test this against the goal of people meeting in a
shared world rather than treating hidden copies as equivalent. Avoid putting
rewards behind a single guild-controlled spawn. Rate limiting rewards is a
progression decision, separate from allowing participation. No single-process
claim of thousands of combatants is supported before representative load tests.

PvP zones and scheduled battlegrounds have explicit opt-in admission. Their
server rules cannot spill into cooperative world events. Instanced competitive
events can normalize equipment; open PvP progression needs separate balance
testing. Reward PvP identity and progression without making it a mandatory route
to the strongest cooperative equipment.

## Economy and abuse controls

The proposed store entitlement service grants cosmetics only. It must have no path
to combat stats, upgrade materials, loot chances, paid shortcuts, or tradable cash tokens.
Top-tier progression binds to the earning character/account under documented
rules. Bindings alone do not prevent paid boosting or account sales.

Use server validation, request rate limits, replay protection, suspicious economy
alerts, and reviewable moderation tools. Anti-cheat software is one signal,
not authority. Restrict service credentials, validate every transfer, and test
duplicate requests, disconnects, rollback, concurrent trades, and failed writes.
If trading is introduced, design RMT detection and appeals alongside it.

## Interfaces to preserve during migration

| Boundary | Prototype | Production contract |
| --- | --- | --- |
| Player command | Unreal input and server RPCs | Sequenced, authenticated intent with server validation |
| Combat state | Replicated gameplay foundation | Measured snapshots/events; predicted client presentation |
| Encounter state | Replicated boss phases | Server encounter ID, phase, time, participant eligibility |
| Reward grant | SQLite fixed-operation mutations with request deduplication; uncompiled Unreal integration | Full item transaction and economy audit ID |
| Character save | Backend account record and lease; local `USaveGame` in standalone | Operationally verified persistence, authorization, and crash recovery |
| Social membership | SQLite parties/guilds and CLI invitations | In-game social UI, moderation, routing, and durable cross-region identity |
| Dungeon entry | Party-specific same-process spatial room blockout | Dedicated instances, transfer ownership, reconnect and fleet lifecycle |
| World event listing | One encounter with a 30-second source reset timer | Realm schedule, overflow, routing, and encounter status API |

Preserve useful gameplay components, terminology, encounter specifications, and
balance data formats while adding explicit server authority. Local gameplay code
and server build targets are not proof of a working networking implementation.
Production characters start fresh rather than trusting local save values. Backend
account persistence now exists and survives service restart in Python tests;
in-engine reconnect identity and resumed progression remain unverified. A
production economy additionally requires the operational and abuse controls above.

## Delivery stages and acceptance gates

1. **UE5 foundation.** Compile editor, game, and dedicated-server targets. Verify
   opening completion, readable boss attacks, recoverable defeat, earned upgrades,
   and replayable progression. Test two real clients against the same server and
   confirm shared boss state and PvP consent rules. Distinguish standalone save
   behavior from backend multiplayer records; record failures before expanding.
2. **Small UE5 network slice.** One zone, a small invited party, one three-phase
   boss, backend login/save, a party dungeon room, and a separate opt-in PvP
   encounter. Prove server damage authority, reconnects, duplicate-grant prevention, and independent personal
   loot. Instrument bandwidth, frame time, latency, and database operations.
3. **Capacity and operations trials.** Set a target population from measurements.
   Test representative combat load, overflow, zone transfers, degraded services,
   backups/restores, deploy/rollback, abuse cases, and operating cost per player.
   Test on multiple real clients; bots alone cannot validate player experience.
4. **Persistent external alpha.** Require monitoring, support/moderation,
   privacy and account recovery, incident procedures, and published progression
   rules. Increase population only when measured reliability supports it.

No stage establishes readiness for the next by having an open port or a running
process. Record functional results and failures at each gate before expanding.
