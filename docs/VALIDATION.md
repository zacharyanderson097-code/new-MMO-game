# First playable acceptance checklist

## Recorded evidence

| Check | Result in this cloud workspace |
| --- | --- |
| Native gameplay-rule tests | PASS: 66 assertions, C++17 with warnings as errors |
| Realm backend HTTP integration | PASS: 46 tests, including persistence, permissions, and concurrency |
| Realm CLI/privacy tests | PASS: 9 tests, including real HTTP workflows |
| Patch whitespace | PASS: `git diff --check` |
| Unreal Editor / game / server compile | UNRUN: Unreal Engine/toolchain not installed |
| Entry-map startup, render, collision, input | UNRUN |
| Unreal cooking / packaged client / packaged server | UNRUN |
| Two-client replication / damage / personal loot | UNRUN |
| Unreal admission / durable rewards / party dungeon integration | SOURCE ADDED; UNRUN in engine |
| MMO load / cross-process handoffs / production operations | NOT IMPLEMENTED |

Source review corrected a missing encounter-replay interaction, late-join local
camera initialization, player body blocking, and recovery from the test-region
edge. Further review added re-authentication identity fencing, clearing dungeon and
pending action state on lease loss, and visible dungeon/resource markers.
These fixes still need Unreal runtime verification. The native tests do not
exercise replication, input, physics, rendering, or Unreal's code generation.

## Required engine checks

Record the exact UE version, platform, build configuration, command, exit status,
and log path for each result. Run Editor, game, and server builds, then cook and
stage Development packages. Resolve compiler/UHT errors before playtesting. Do
not substitute native-rule tests for an Unreal build.

### Standalone first loop

- Start `/Engine/Maps/Entry` with `EmberGameMode`; verify a floor, lighting, character,
  HUD, sanctuary, beacon, boss and visible PvP markers exist.
- Verify the camera, WASD, mouse release/recapture, guard press/release and attack
  facing. No attack reaches through scenery or beyond melee range.
- Before lighting the beacon, attacking the boss cannot advance it. E nearby
  starts the player's story; it is not consumed for other players.
- Cross health thresholds 65% and 30%; check faster impacts and the third-attack
  Cinder Halo. The outer ring is hazardous; inside 230 units is safe. Rendered
  outlines and HUD instructions must agree with actual server damage.
- Guard spends stamina and reduces damage; dodge grants a short window; mend
  respects cooldown and stamina. Repeated inputs cannot bypass cooldowns.
- Die; observe five-second recovery and no gear loss. Leave the blockout edge;
  observe recovery rather than falling indefinitely. Clear an abandoned encounter;
  it resets after no eligible player remains nearby for four seconds.
- Clear the boss, gain exactly one sigil, E near its remains to reset, and clear
  nine times. Three clears fund Tempered; six more fund Dawnbound. Insufficient
  currency and maximum-tier forging never deduct currency.
- Restart standalone; gear, sigils, and victories restore. Delete only the local
  `Saved/SaveGames/Emberfall_Standalone_v1.sav` to begin a fresh local progression.

### Shared-world and PvP checks

- Start the backend and configure only the trusted server with its credential.
  Register two accounts and obtain separate tickets per `REALM_SERVICE.md`.
  Before `Login <ticket>`, movement, damage, gathering, and forging must be disabled.
  Check invalid/expired/consumed tickets, duplicate active admission, and lease loss.
- Join a server with two **independent clients**, then join a third after the fight
  starts. Each sees the same boss health, phase, and attack location. The late
  joiner has working camera/input/HUD. Client scene collision matches authority.
- Both clients activate their own beacon and damage the boss. On defeat each gets
  one personal sigil; the final blow does not own the reward. An idle non-contributor
  gets none. Reward handling cannot run twice for the same completed encounter.
- One player dies while another remains. The boss continues; a wipe resets only
  once no eligible living player remains within the leash. Defeated participants
  near the event can still receive their earned reward if already contributors.
- Players cannot push/body-block one another, reset a live fight, attack other
  players in PvE, or submit item/currency/damage values through an action RPC.
- In the Red March, test all four consent pairs; only both-on permits damage. Move
  either player across the boundary and verify damage stops. Test PvP with different
  gear tiers and confirm normalized damage. Guard/dodge still apply.
- Damage and attack cooldowns remain server-controlled with latency simulation.
  Changing consent during the ten-second combat restriction fails. Check late
  joins, disconnects, server shutdown, and restart without duplicate grants.
- Earn a sigil, gather Emberstone, forge gear, disconnect, then obtain a fresh ticket.
  Rejoin with the same account and verify values survive both game-server and
  backend restarts. Local standalone save files must never overwrite realm data.
- Simulate a lost reward response and disconnect during the request. Retries use
  the same request ID and must not double-grant; normal release must wait for
  pending writes. Abrupt server process loss has no durable outgoing reward queue
  yet: test and document that recovery gap before an external economy is enabled.
- Stop the backend or expire the lease. Character actions pause before the lease's
  hard deadline; re-admission refreshes state. Changing accounts on the same pawn
  must be rejected: reconnect to create a new pawn and prevent inherited contribution.

### Party dungeons and social integration

- Use the CLI to create a party, invite a second account, and accept as that
  recipient. Nonleaders and other recipients cannot hijack invitations. Check
  guild membership independently; both survive service restart.
- Gather all online, chapter-ready party members at the marked southern portal
  `(0,-1500)`. Only the leader can enter for the roster. An offline, distant, dead,
  already-enrolled, or in-combat member prevents entry for the whole group.
- Enter with two different parties. Each has its own room and Keeper Serael;
  damage, targeting, and rewards cannot cross instance IDs. The HUD identifies
  the correct dungeon boss; floors/walls/exit labels replicate to late clients.
- Defeat the dungeon boss. Each enrolled damage contributor gets one durable
  dungeon reward; the completed boss cannot reset for another reward in that room.
  E at the lobby exits only the interacting player; all empty rooms clean up
  after 30 seconds and release their slots. Four occupied rooms deny a fifth.
- Die, disconnect, or lose admission in a dungeon. Check room respawn and cleanup,
  return to the sanctuary on lease loss, and no inherited room/contribution when
  changing identity. Dungeon reconnect/rejoin is not implemented as a full feature.
- Clear the public Cinderwarden. Observe the shared 30-second respawn countdown;
  automatic/manual resets issue a new encounter ID and one reward per eligible
  character for that encounter. No PvP flag or guild ownership restricts the event.

## MMO gates after this source foundation

Validate the new authenticated persistence and party dungeon flow in Unreal first.
Then expand the room blockout into a fully authored five-player dungeon and add
support-role contribution, reconnect/rejoin, durable crash outboxes, polished social
UI, cross-process transfers, overflow and realm operations. Measure load before
choosing advertised capacity. Backend tests do not establish game-server capacity.
The current 16-player setting is a development limit, not evidence of MMO scale.
