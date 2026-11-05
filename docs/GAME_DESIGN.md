# EMBERFALL — game direction

An original fantasy MMORPG about reclaiming a continent that remembers its dead.
Working title; clear trademarks before commercial release.

## Status and inspirations

This document proposes the full game. The first deliverable is a native Unreal
Engine 5 PC project with replicated overworld source, server-authoritative combat
and rewards, an opening quest, gathering, and a cooperative boss foundation. A
Python/SQLite service now provides durable account progression, one-use login
tickets, character leases, deduplicated mutations, and party/guild membership via
CLI. Its backend tests pass; standalone play retains separate local SaveGame
support. Unreal integration has no verified engine build or in-engine reconnect
test yet. The development cap is 16 connections, with no MMO-scale load evidence.
The README records features and validation limits. This is not a deployed MMO or
a persistent shared continent; a compatible Unreal installation is required to play.

The direction combines deliberate action combat and useful gathering with deep
group roles, readable encounters, and a world shaped by shared accomplishments.
The user's reference to “anion 2” is provisionally interpreted as **Aion 2**:
the goal is spectacular bosses with movement, mechanics, and coordinated roles.
These are broad design goals; all characters, places, art, and stories are original.

## Promises to players

- Strength comes from playing: difficult encounters, practiced combat, and crafting.
- No cash-purchased power, loot boxes, enhancement chances, boosts, or paid currency.
- Cooperative world bosses never flag players for PvP or reward killing participants.
- PvP is an explicit choice, with its own destinations and clearly announced rules.
- A failed run teaches something; repeated bad luck still advances a visible goal.

## The opening: The Bell Beneath the Ash

You are a Wayfarer, returning with the first convoy to the ruined coast of Veyra.
Seventy years ago, the continent's signal towers rang together and its cities sank
beneath glass ash. Your convoy carries a living ember capable of waking one tower.
The opening begins aboard a crowded river barge as a drowned bell sounds below it.

1. **The Crossing.** Speak with scout Ilyen and choose your starting discipline.
   An ash squall extinguishes the convoy lamps. Learn movement, attacks, and a dodge
   while protecting a small group of survivors on the shore.
2. **A Light for the Lost.** Gather driftwood and emberstone, repair a shelter beacon,
   and recover three lost convoy seals. Gathering feeds an immediate shared need.
3. **The First Answer.** Carry the living ember to the ruined signal tower. Its
   guardian, the Cinderwarden, wakes with the voices of the city's missing people.
   Fight a short introductory encounter that teaches telegraphs and recovery.
4. **The Bell Opens.** Victory reveals that the towers preserved living memories.
   Restoring the continent may erase the people trapped inside them. Ilyen asks
   whether the next tower should be awakened; the answer becomes a later quest.

In the full game, the community rebuilds the shore settlement through contribution
milestones. New vendors, expeditions, and story chapters open with visible changes.
Milestones persist through maintenance and have recovery plans after failed events.
Essential quests remain replayable; early arrivals cannot permanently deny content
to later players. Individual story decisions use personal phases where necessary.

## Combat and group identity

Use direct movement, aimed abilities, readable attack commitment, and a limited
stamina budget. Weapons establish a discipline; a small active skill bar leaves
room for encounter awareness. Build choices should support several viable styles.

| Starting discipline | Role and identity | Core actions |
| --- | --- | --- |
| Vanguard | Frontline protection and control | Cleave, shield guard, rally |
| Emberweaver | Ranged damage and disruption | Ember bolt, flame surge, cinder step |
| Thornkeeper | Healing and sustained support | Thorn strike, renewal, shelter |

Full group encounters support tank, healer, and damage roles with interrupt and
rescue responsibilities shared across the party. A solo tutorial provides its own
healing and forgiving recovery; it does not demonstrate multiplayer role balance.

## One shared realm, an open continent

The full-game target is a persistent open world where many players share a logical
realm: the same towns, guilds, markets, regional milestones, and calendar of events.
Friends on that realm can meet, form parties, explore, and return to their shared
settlement without choosing a different progression copy. Characters, earned gear,
and completed town milestones survive disconnects, server restarts, and maintenance.

“One server” describes this social experience, not a promise that one machine can
simulate an unlimited population. Regions and private dungeon instances may run on
separate authoritative processes while preserving realm identity and party travel.
Publish measured region and encounter capacities before public testing. If a region
needs overflow, keep parties together, make instance selection visible, and reunite
friends as capacity allows; do not silently split them or sell priority access.

## Dungeons and raids

The source contains a **Drowned Archive blockout**: one boss arena for a party of
up to five, with four available spatial rooms inside the same server process.
Keeper Serael currently reuses the existing boss attack pattern. Entry, return,
party checks, and personal reward paths exist in source but remain uncompiled and
untested in-engine. This is not the complete dungeon described below or a raid.

Proposed dungeons are repeatable, instanced adventures reached through entrances in
the shared world. The initial group size is **five players**, with frontline control,
healing or support, and damage responsibilities. Early encounters teach these roles
without requiring a named class or a specific build to open a door or counter an
attack. Multiple disciplines can supply interrupts, protection, and recovery.

| Dungeon | Encounters and mechanics | Access and earned rewards |
| --- | --- | --- |
| **The Drowned Archive** | Beneath the shore settlement, Keeper Serael rotates water channels; players move to dry shelves and interrupt a clearly marked flood cast. The final boss, the Inkbound Witness, creates memory doubles that repeat its previous attack, rewarding recognition and positioning. | A short restoration quest opens the entrance for the whole party; late players can complete it independently. Personal loot includes archive-themed armor traits and Archive Seals toward a chosen item. |
| **Kiln of the Sleeping Sun** | In a broken mountain foundry, the Cinder Twins alternate an active attacker and a shielded furnace keeper; players redirect cooling vents to expose the keeper. The final boss, Ardent Mother Vhal, marks safe forge plates before a heat wave and must be staggered during a visible overload. | Open after a regional story chapter that remains available to newcomers. Personal loot includes weapon patterns and bound Suncores used in owner-earned masterwork crafting. |

Normal difficulty is the learning route; later challenge versions add coordinated
mechanics and prestige rewards. Wipes restart from checkpoints, and disconnected
players receive a reconnect window. Support contributions count toward rewards.
No paid keys, exclusive guild bookings, or first-clear ownership restrict access.

**Ten-player raids** are a later target: longer multi-boss expeditions with shared
mechanical responsibility and earned prestige gear. The first proposed raid, the
Choir Below, follows the signal towers into the continent's buried memory network.
Raid scope follows the five-player and world-boss validation gates. The authored
dungeons described above and all raids remain planned; the arena blockout does not complete
their encounter, story, art, or accessibility work.

## Planned world boss: Avarra, the Bellbound Colossus

A fallen tower walks through the Cinder Vale carrying a bell in its ribcage.
The event is announced with a map marker, a generous travel warning, and visible
staging space. Large silhouettes, distinct shapes, sound cues, and adjustable
effects keep mechanics legible without relying on color alone.

| Phase | Telegraph and response | Team opportunity |
| --- | --- | --- |
| 100–70%: The March | A wide ground arc previews a committed sweep; move behind it. A marked circular impact rewards leaving the circle before detonation. | Tanks orient the boss; everyone can rescue fallen players. |
| 70–35%: Broken Chimes | Three resonators pulse in a visible sequence. Interrupt the active chime while avoiding slow expanding rings. | Split into small groups; successful interrupts create a damage window. |
| 35–0%: The Last Toll | The broken bell exposes its core. Alternating safe sectors precede the final toll; temporary shelters block its blast. | Supports protect the approach; damage players exploit the exposed core. |

The source's Cinderwarden uses simpler telegraphed attacks and health phases,
personal contributor rewards, and an automatic 30-second encounter reschedule.
Avarra's mechanics, the full event scheduler, and overflow encounters are proposed.
The complete version needs networked mechanics, accessibility testing, encounter
load tests, and tuning for late arrivals before these promises are considered met.

## Planned open access to cooperative events

The source establishes cooperative encounter rules and personal rewards. The
complete participation, capacity, and reconnect protections below need further
implementation and real multiplayer validation; overflow is not implemented.

- Boss areas are protected PvE spaces, including their travel and resurrection routes.
- No first-hit ownership, guild reservations, kill stealing, or player collision that
  lets a group physically block the objective. Each eligible participant receives loot.
- Participation recognizes healing, protection, mechanics, revives, and damage;
  damage races cannot be the only qualification. Publish eligibility feedback.
- New arrivals enter a staging period and can join the current fight safely. Players
  who miss qualification receive a clear next-event time and alternate progression.
- Capacity creates overflow encounters with comparable rewards and scheduled starts.
  Group transfers preserve parties; no premium priority or paid event tickets.
- Failed events reset after recovery. Repeat fights remain useful through materials,
  practice, and deterministic progress, without mandatory attendance at every spawn.

## Earned gear and the economy

Best combat gear is character-bound and earned through challenging PvE, ranked PvP
achievements, or masterwork crafting that requires the owner's earned reagents.
Do not put completed best-in-slot gear on the auction house. Tradable ordinary
materials and useful intermediate crafts keep gathering and trade meaningful.

The full-game proposal awards personal loot plus visible Warden Marks. After a
documented number of eligible clears, marks buy a chosen boss item: no endlessly
resetting pity counter. The prototype uses **Ember Sigils** for its earned upgrade
loop; this does not implement the full Warden Mark economy or its proposed limits.
A weekly mark bonus can limit runaway repetition, but the deterministic path must
remain available afterward at a slower stated rate. Publish rates and thresholds.
Different activities offer comparable power with distinct traits and appearances.
Avoid mandatory random enhancement destruction and repeated obsolete gear resets.

Monetization proposal: a subscription or purchase-plus-cosmetics model, subject to
cost testing. Cosmetics cannot carry stats, conceal PvP tells, or grant utility.
No paid inventory advantage, gathering speed, crafting completion, level skips,
resurrection perks, gold tokens, real-money item trading, or tradable store items.
Developer enforcement and fraud detection are still required; policy alone does
not prevent illicit gold selling. Publish enforcement and appeal procedures.

## PvP on its own terms

**The Red March** is an explicitly opt-in frontier with clear boundary warnings,
safe exits, and no PvE main-story objective requiring entry. Combat begins only
after a deliberate confirmation and transition; ambushes cannot force PvE flags.

**Bannerfall** is a scheduled team event built around capturing and delivering
standards. Provide multiple session times and matchmaking that monitors skill,
party size, queue health, and faction imbalance. Arena modes use normalized gear;
frontier gear advantages have a tested cap. PvP starter gear is earned quickly,
and competitive mastery rewards specialized sidegrades, titles, and cosmetics.
No exclusive PvE best-in-slot item forces players into PvP. PvP accomplishments
must remain valuable without making world-boss participation a gearing prerequisite.

## Deliverable gates

1. **Native UE5 foundation:** replicated overworld source, one Vanguard, opening
   quest, gathering, movement, stamina, attacks, telegraphed cooperative boss,
   server-owned rewards, backend accounts, and standalone saves. Source includes
   party rooms and backend integration; network authority is not runtime-validated.
   Compile and test standalone, two-client admission, defeat, victory, dungeon entry,
   reconnects, and earned progression in a compatible Unreal installation.
2. **PC combat slice:** build one polished zone, complete tutorial encounter,
   gathering, accessibility options, and target-hardware performance measurements.
3. **Persistent multiplayer services:** validate engine use of the implemented
   account/character backend; add durable realm-wide changes, reliable reconnects,
   operations and recovery controls, and representative concurrent load tests.
4. **Cooperative slice:** a shared persistent region, five-player Drowned Archive,
   and an overflow-enabled world boss; verify party continuity, restart persistence,
   participation fairness, and personal loot under real concurrent load. Add the
   second dungeon after these checks; prototype ten-player raids afterward.
5. **PvP and economy:** opt-in boundaries, matchmaking, gear rules, abuse handling,
   item auditing, and documented performance/cost limits before inviting more players.

Each gate requires playable evidence. World size and player counts follow measured
capacity, staffing, and operating cost; neither engine choice nor self-hosting alone
establishes MMO scale, profitability, or readiness for a public launch.
