# Local realm accounts, characters, parties, and guilds

The `backend` package is a Python standard-library development service and CLI.
It provides account sessions, short-lived join tickets, SQLite-backed character
records, and party/guild membership. It is a local integration foundation, not a
production account platform or a tested MMO fleet. Unreal integration still needs
a compatible engine build and real client/server playtests; see the README for
the current validation record.

## Start the development service

Run from the repository root with Python 3.10 or newer. No package installation is
required. The service binds to loopback; remote use needs a separately operated
HTTPS endpoint and additional security and operational work.

```bash
cd /path/to/new-MMO-game
export EMBER_SERVER_TOKEN="$(python3 -c 'import secrets; print(secrets.token_urlsafe(48))')"
export EMBER_BACKEND_URL=http://127.0.0.1:8081
python3 -m backend.realm --db Saved/Realm/realm.sqlite --port 8081
```

The generated token stays in this shell's environment. Do not print it, commit it,
put it in launch arguments, or give it to a player client. `EMBER_SERVER_TOKEN`
must contain at least 32 UTF-8 bytes. Only the realm process and trusted Unreal
server receive it. Keep the same value in those trusted processes; if the service
is restarted with a new value, update the trusted server's configuration too.
Use the README's Unreal server launch instructions after configuring its service
connection. The player CLI does not use or read this server credential.
The trusted Unreal server reads `EMBER_BACKEND_URL` (default
`http://127.0.0.1:8081`) and `EMBER_SERVER_TOKEN` from its environment. Start it
from an appropriately configured private shell, never by copying these values
into a distributed player configuration. Multiplayer admission requires the
backend; it does not fall back to an anonymous character. Standalone play retains
its separate local-save convenience mode.

The database, journal files, and CLI sessions are placed under ignored `Saved/`.
Use separate session files for different players. Keep database backups private:
they contain account password hashes and character and social records.

From another terminal, check the local service:

```bash
python3 -c 'import urllib.request; print(urllib.request.urlopen("http://127.0.0.1:8081/health").read().decode())'
```

An `ok` health response only confirms the service can answer and open its database;
it does not validate authentication, the game connection, or player capacity.

## Register, sign in, and join Unreal

All following commands run from the repository root. Passwords are entered only
at a private `getpass` prompt. The client refuses an echoing fallback, so use an
interactive terminal. There is no password command-line flag or environment option.
Registration creates the account; sign-in is a separate explicit action.
Usernames are 3–24 ASCII characters, starting with a letter, followed by letters,
numbers, underscores, or hyphens. Passwords must be 10–128 characters.

```bash
python3 -m backend.client register WayfarerOne
python3 -m backend.client login WayfarerOne
python3 -m backend.client character
python3 -m backend.client ticket
```

`login` saves the account bearer session to `Saved/Realm/client-session.json` using
an atomic replace and mode `0600` on POSIX. It does not print the session token.
The file is bound to the service URL to avoid accidentally sending it to another
realm. Treat it as a credential: do not share or commit it. `character` reads the
account's server-side character; the CLI has no arbitrary progression-write command.

`ticket` deliberately prints a **one-use join credential**, valid for 90 seconds.
Do not share it, place it in a public log, or reuse it. In the Unreal client's
console, enter:

```text
Login <paste-the-ticket-here>
```

The trusted game server consumes the ticket through the realm service. A player
cannot substitute an account ID for authenticated admission. Request a fresh
ticket if it expires or was already consumed. Keep account sessions out of the
Unreal console: only the short-lived ticket belongs there.

The default service origin is `http://127.0.0.1:8081`. To select a service or a
different player's session, put global options before the command:

```bash
python3 -m backend.client --url http://127.0.0.1:8081 --session-file Saved/Realm/player-two.json register WayfarerTwo
python3 -m backend.client --session-file Saved/Realm/player-two.json login WayfarerTwo
```

Remote origins require `https://`. Plain HTTP is accepted only for literal
loopback IP addresses and `localhost`; URL credentials, paths, queries, and
fragments are rejected. TLS certificate verification remains enabled and the
client refuses redirects. These checks protect the development client; they do
not turn the development HTTP service into a production identity provider.

## Parties

Parties currently hold up to five characters. A leader creates the party and
invites an existing account by username. The CLI returns an invitation ID; convey
that ID to the intended invitee through your existing private coordination channel.
There is currently no invitation-list endpoint. Only the invited account can accept.

```bash
python3 -m backend.client party create
python3 -m backend.client party invite WayfarerTwo
python3 -m backend.client party show
python3 -m backend.client --session-file Saved/Realm/player-two.json party accept <invitation-id>
python3 -m backend.client --session-file Saved/Realm/player-two.json party show
python3 -m backend.client --session-file Saved/Realm/player-two.json party leave
```

Replace `<invitation-id>` with the returned ID; angle brackets are placeholders,
not literal shell input. Membership is account-backed, not a local client list.
Only the current leader can invite. Existing invitations stop being usable when
leadership changes. Membership does not supply matchmaking, shared quest credit,
voice chat, or a complete social UI.

The Unreal source includes a party dungeon entry: the leader uses `Dungeon` or the
context action near the entrance at `(0, -1500)`, with party members within 350
Unreal units. `ExitDungeon` or the context action near the dungeon lobby returns
the interacting player, with a 300-unit proximity check. These are prototype interactions that
still need engine compilation and multiplayer testing; party database records
alone do not verify a functioning dungeon trip.

## Guilds

Guilds use the same invitation workflow, with up to 50 members and a name supplied
at creation. Names containing spaces must be quoted.

```bash
python3 -m backend.client guild create "Dawnward Company"
python3 -m backend.client guild invite WayfarerTwo
python3 -m backend.client guild show
python3 -m backend.client --session-file Saved/Realm/player-two.json guild accept <invitation-id>
python3 -m backend.client --session-file Saved/Realm/player-two.json guild leave
```

Party and guild membership are independent. These records do not implement a
guild bank, guild permissions beyond leader invitations, guild combat rewards,
cross-region chat, or social moderation tools.

## Trust boundaries and remaining work

- Public account sessions can read their own character and manage permitted social
  actions. They cannot call trusted game-server progression endpoints.
- Server credentials must remain outside player builds and session files. A leaked
  server credential compromises the development authority boundary; rotate it and
  investigate before accepting further character writes.
- The CLI reports bounded local descriptions of HTTP failures and never echoes
  an authentication error body. A 401 means sign in again or correct credentials;
  a 403 means the account lacks permission; a 409 indicates a state conflict.
- The account service and SQLite file are a single-machine development setup.
  Production needs HTTPS deployment, abuse and rate controls, recovery and session
  management, monitoring, backups and restore tests, and measured concurrent load.
- Unreal compilation, replicated play, service failure/reconnect handling, and
  durable reward behavior must be validated end to end. Python checks alone do
  not establish these outcomes or MMO readiness.
