"""Local development account and realm service; HTTPS termination is required remotely.

Use ``python -m backend.realm --db Saved/Realm/realm.sqlite --port 8081``.
EMBER_SERVER_TOKEN must contain at least 32 UTF-8 bytes. Request logs are disabled.
"""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import secrets
import sqlite3
import threading
import time
from typing import Iterator
import uuid


MAX_BODY_BYTES = 16_384
SESSION_TTL_SECONDS = 86_400
TICKET_TTL_SECONDS = 90
PASSWORD_ITERATIONS = 600_000
LEASE_TTL_SECONDS = 60
INVITE_TTL_SECONDS = 86_400


class APIError(Exception):
    def __init__(self, status: int, code: str, message: str):
        super().__init__(message)
        self.status = status
        self.code = code
        self.message = message


def token_hash(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def password_hash(password: str, salt: bytes) -> bytes:
    return hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, PASSWORD_ITERATIONS)


class RealmStore:
    def __init__(self, db_path: str | os.PathLike[str]):
        self.db_path = str(db_path)
        if self.db_path == ":memory:":
            raise ValueError("Use a file-backed database; requests have independent connections.")
        Path(self.db_path).parent.mkdir(parents=True, exist_ok=True)
        with self.connection() as db:
            db.execute("PRAGMA journal_mode=WAL")
            db.executescript("""
                CREATE TABLE IF NOT EXISTS accounts (
                    id TEXT PRIMARY KEY,
                    username TEXT NOT NULL UNIQUE COLLATE NOCASE,
                    password_salt BLOB NOT NULL,
                    password_hash BLOB NOT NULL,
                    created_at INTEGER NOT NULL
                );
                CREATE TABLE IF NOT EXISTS sessions (
                    token_hash TEXT PRIMARY KEY,
                    account_id TEXT NOT NULL REFERENCES accounts(id),
                    expires_at INTEGER NOT NULL
                );
                CREATE TABLE IF NOT EXISTS join_tickets (
                    token_hash TEXT PRIMARY KEY,
                    account_id TEXT NOT NULL REFERENCES accounts(id),
                    expires_at INTEGER NOT NULL,
                    consumed_at INTEGER
                );
                CREATE TABLE IF NOT EXISTS characters (
                    account_id TEXT PRIMARY KEY REFERENCES accounts(id),
                    gear_tier INTEGER NOT NULL DEFAULT 0 CHECK(gear_tier BETWEEN 0 AND 2),
                    sigils INTEGER NOT NULL DEFAULT 0 CHECK(sigils >= 0),
                    victories INTEGER NOT NULL DEFAULT 0 CHECK(victories >= 0),
                    dungeon_clears INTEGER NOT NULL DEFAULT 0 CHECK(dungeon_clears >= 0),
                    emberstone INTEGER NOT NULL DEFAULT 0 CHECK(emberstone BETWEEN 0 AND 1000000),
                    chapter_started INTEGER NOT NULL DEFAULT 0,
                    revision INTEGER NOT NULL DEFAULT 0
                );
                CREATE TABLE IF NOT EXISTS leases (
                    character_id TEXT PRIMARY KEY REFERENCES accounts(id),
                    token_hash TEXT NOT NULL,
                    expires_at INTEGER NOT NULL
                );
                CREATE TABLE IF NOT EXISTS mutations (
                    character_id TEXT NOT NULL REFERENCES accounts(id),
                    request_id TEXT NOT NULL,
                    operation TEXT NOT NULL,
                    created_at INTEGER NOT NULL,
                    PRIMARY KEY(character_id, request_id)
                );
                CREATE TABLE IF NOT EXISTS social_groups (
                    id TEXT PRIMARY KEY,
                    kind TEXT NOT NULL CHECK(kind IN ('party', 'guild')),
                    name TEXT NOT NULL DEFAULT '',
                    leader_id TEXT NOT NULL REFERENCES accounts(id)
                );
                CREATE UNIQUE INDEX IF NOT EXISTS guild_unique_name
                    ON social_groups(name COLLATE NOCASE) WHERE kind='guild';
                CREATE TABLE IF NOT EXISTS memberships (
                    kind TEXT NOT NULL,
                    account_id TEXT NOT NULL REFERENCES accounts(id),
                    group_id TEXT NOT NULL REFERENCES social_groups(id) ON DELETE CASCADE,
                    joined_at INTEGER NOT NULL,
                    PRIMARY KEY(kind, account_id)
                );
                CREATE TABLE IF NOT EXISTS invites (
                    id TEXT PRIMARY KEY,
                    kind TEXT NOT NULL,
                    group_id TEXT NOT NULL REFERENCES social_groups(id) ON DELETE CASCADE,
                    inviter_id TEXT NOT NULL REFERENCES accounts(id),
                    recipient_id TEXT NOT NULL REFERENCES accounts(id),
                    expires_at INTEGER NOT NULL
                );
                CREATE INDEX IF NOT EXISTS sessions_expiry ON sessions(expires_at);
                CREATE INDEX IF NOT EXISTS tickets_expiry ON join_tickets(expires_at);
                CREATE INDEX IF NOT EXISTS members_by_group ON memberships(group_id, joined_at, account_id);
                CREATE INDEX IF NOT EXISTS invites_by_recipient ON invites(group_id, recipient_id);
                CREATE INDEX IF NOT EXISTS invites_expiry ON invites(expires_at);
            """)
            # Additive migration for development databases created before gathering.
            columns = {row["name"] for row in db.execute("PRAGMA table_info(characters)")}
            if "emberstone" not in columns:
                db.execute("ALTER TABLE characters ADD COLUMN emberstone INTEGER NOT NULL DEFAULT 0 CHECK(emberstone BETWEEN 0 AND 1000000)")

    @contextmanager
    def connection(self, mutate: bool = False) -> Iterator[sqlite3.Connection]:
        db = sqlite3.connect(self.db_path, timeout=10, isolation_level=None)
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA foreign_keys=ON")
        db.execute("PRAGMA busy_timeout=10000")
        try:
            if mutate:
                db.execute("BEGIN IMMEDIATE")
            yield db
            if mutate:
                db.commit()
        except Exception:
            if mutate:
                db.rollback()
            raise
        finally:
            db.close()

    def authenticate_session(self, bearer: str, db: sqlite3.Connection) -> str:
        row = db.execute(
            "SELECT account_id FROM sessions WHERE token_hash=? AND expires_at>?",
            (token_hash(bearer), int(time.time())),
        ).fetchone()
        if row is None:
            raise APIError(401, "unauthorized", "A valid account session is required.")
        return row["account_id"]

    def character(self, account_id: str, db: sqlite3.Connection) -> dict:
        row = db.execute("""
            SELECT accounts.id AS character_id, username, gear_tier, sigils,
                   victories, dungeon_clears, emberstone, chapter_started, revision
              FROM accounts JOIN characters ON accounts.id=characters.account_id
             WHERE accounts.id=?
        """, (account_id,)).fetchone()
        if row is None:
            raise APIError(404, "not_found", "Character not found.")
        result = dict(row)
        result["chapter_started"] = bool(result["chapter_started"])
        return result

    def require_lease(self, body: dict, db: sqlite3.Connection) -> str:
        character_id = require_string(body, "character_id", 36, 36)
        lease = require_string(body, "lease", 16, 256)
        row = db.execute("SELECT token_hash, expires_at FROM leases WHERE character_id=?", (character_id,)).fetchone()
        if row is None or row["expires_at"] <= int(time.time()) or not hmac.compare_digest(row["token_hash"], token_hash(lease)):
            raise APIError(403, "invalid_lease", "An active character lease is required.")
        return character_id

    def social_state(self, kind: str, account_id: str, db: sqlite3.Connection) -> dict:
        group = db.execute("""
            SELECT social_groups.* FROM memberships
            JOIN social_groups ON social_groups.id=memberships.group_id
            WHERE memberships.kind=? AND memberships.account_id=?
        """, (kind, account_id)).fetchone()
        result = {f"{kind}_id": "", "members": [], "leader_id": ""}
        if kind == "guild":
            result["name"] = ""
        if group is None:
            return result
        result[f"{kind}_id"] = group["id"]
        result["leader_id"] = group["leader_id"]
        result["members"] = [row["account_id"] for row in db.execute(
            "SELECT account_id FROM memberships WHERE group_id=? ORDER BY joined_at, account_id", (group["id"],))]
        if kind == "guild":
            result["name"] = group["name"]
        return result


def require_string(body: dict, key: str, minimum: int = 1, maximum: int = 128) -> str:
    value = body.get(key)
    if not isinstance(value, str) or not minimum <= len(value) <= maximum:
        raise APIError(400, "invalid_request", f"Invalid {key}.")
    try:
        value.encode("utf-8")
    except UnicodeEncodeError:
        raise APIError(400, "invalid_request", f"Invalid {key} encoding.") from None
    return value


def credentials(body: dict) -> tuple[str, str]:
    username = valid_username(body)
    password = require_string(body, "password", 10, 128)
    return username, password


def valid_username(body: dict) -> str:
    username = require_string(body, "username", 3, 24)
    if re.fullmatch(r"[A-Za-z][A-Za-z0-9_-]{2,23}", username) is None:
        raise APIError(400, "invalid_request", "Username must start with a letter and use ASCII letters, numbers, underscore or hyphen.")
    return username


class RealmHTTPServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address: tuple[str, int], store: RealmStore, server_token: str):
        self.store = store
        self.server_token = server_token
        self.login_attempts: dict[str, tuple[int, int]] = {}
        self.rate_lock = threading.Lock()
        self.worker_slots = threading.BoundedSemaphore(16)
        super().__init__(address, RealmHandler)

    def process_request(self, request, client_address) -> None:
        # Bound expensive password work and idle HTTP connections in this dev server.
        if not self.worker_slots.acquire(blocking=False):
            payload = b'{"error":{"code":"server_busy","message":"Try again later."}}'
            response = (b"HTTP/1.0 503 Service Unavailable\r\nContent-Type: application/json\r\n"
                        b"Cache-Control: no-store\r\nConnection: close\r\nContent-Length: "
                        + str(len(payload)).encode("ascii") + b"\r\n\r\n" + payload)
            try:
                request.settimeout(1)
                request.sendall(response)
            except OSError:
                pass
            finally:
                self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except Exception:
            self.worker_slots.release()
            raise

    def process_request_thread(self, request, client_address) -> None:
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.worker_slots.release()

    def rate_limit_login(self, address: str) -> None:
        now = int(time.time())
        with self.rate_lock:
            if len(self.login_attempts) >= 1024:
                self.login_attempts = {ip: state for ip, state in self.login_attempts.items() if state[0] > now - 60}
                if len(self.login_attempts) >= 1024 and address not in self.login_attempts:
                    raise APIError(429, "rate_limited", "Try again later.")
            started, count = self.login_attempts.get(address, (now, 0))
            if now - started >= 60:
                started, count = now, 0
            if count >= 60:
                raise APIError(429, "rate_limited", "Try again later.")
            self.login_attempts[address] = (started, count + 1)


class RealmHandler(BaseHTTPRequestHandler):
    server: RealmHTTPServer
    server_version = "EmberfallRealm/0.1"
    sys_version = ""

    def setup(self) -> None:
        super().setup()
        self.connection.settimeout(10)

    def log_message(self, format: str, *args: object) -> None:
        # Never log paths, bodies, headers, passwords, bearer tokens, or tickets.
        pass

    def send_error(self, code: int, message: str | None = None, explain: str | None = None) -> None:
        # The base implementation can echo parts of a malformed request in HTML.
        # Keep protocol errors structured and never include client-supplied text.
        status = 405 if code == 501 else code
        self._json(status, {"error": {"code": "invalid_http_request", "message": "Unsupported or malformed HTTP request."}})

    def _json(self, status: int, payload: dict) -> None:
        data = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def _body(self) -> dict:
        if self.headers.get("Transfer-Encoding"):
            raise APIError(400, "invalid_request", "Transfer encoding is unsupported.")
        if len(self.headers.get_all("Content-Length", [])) != 1:
            raise APIError(400, "invalid_request", "Exactly one content length is required.")
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            raise APIError(400, "invalid_request", "Invalid content length.") from None
        if length <= 0 or length > MAX_BODY_BYTES:
            raise APIError(400, "invalid_request", "A bounded JSON request body is required.")
        if self.headers.get_content_type() != "application/json":
            raise APIError(415, "unsupported_media_type", "Use application/json.")
        try:
            body = json.loads(self.rfile.read(length))
        except (ValueError, UnicodeDecodeError, RecursionError):
            raise APIError(400, "invalid_json", "Request body must contain valid JSON.") from None
        if not isinstance(body, dict):
            raise APIError(400, "invalid_request", "Request body must be a JSON object.")
        return body

    def _bearer(self) -> str:
        value = self.headers.get("Authorization", "")
        prefix, _, token = value.partition(" ")
        if prefix.lower() != "bearer" or not token or len(token) > 512:
            raise APIError(401, "unauthorized", "Bearer authorization is required.")
        return token

    def _require_server(self) -> None:
        if not hmac.compare_digest(self._bearer().encode("utf-8"), self.server.server_token.encode("utf-8")):
            raise APIError(401, "unauthorized", "Valid server authorization is required.")

    def _register(self, body: dict) -> tuple[int, dict]:
        self.server.rate_limit_login(self.client_address[0])
        username, password = credentials(body)
        salt = secrets.token_bytes(32)
        digest = password_hash(password, salt)
        character_id = str(uuid.uuid4())
        with self.server.store.connection(mutate=True) as db:
            if db.execute("SELECT 1 FROM accounts WHERE username=?", (username,)).fetchone():
                raise APIError(409, "username_taken", "Username is unavailable.")
            db.execute("INSERT INTO accounts VALUES(?,?,?,?,?)", (character_id, username, salt, digest, int(time.time())))
            db.execute("INSERT INTO characters(account_id) VALUES(?)", (character_id,))
        return 201, {"character_id": character_id}

    def _login(self, body: dict) -> dict:
        self.server.rate_limit_login(self.client_address[0])
        username, password = credentials(body)
        with self.server.store.connection() as db:
            row = db.execute("SELECT * FROM accounts WHERE username=?", (username,)).fetchone()
        # Always perform the password KDF, including unknown users.
        digest = password_hash(password, row["password_salt"] if row else bytes(32))
        if row is None or not hmac.compare_digest(digest, row["password_hash"]):
            raise APIError(401, "unauthorized", "Invalid username or password.")
        token = secrets.token_urlsafe(32)
        with self.server.store.connection(mutate=True) as db:
            now = int(time.time())
            db.execute("DELETE FROM sessions WHERE expires_at<=?", (now,))
            db.execute("INSERT INTO sessions VALUES(?,?,?)", (token_hash(token), row["id"], now + SESSION_TTL_SECONDS))
            character = self.server.store.character(row["id"], db)
        return {"session_token": token, "character": character}

    def _client(self, method: str, path: str, body: dict) -> dict:
        store = self.server.store
        with store.connection(mutate=method == "POST") as db:
            account_id = store.authenticate_session(self._bearer(), db)
            if method == "GET" and path == "/v1/character":
                return {"character": store.character(account_id, db)}
            if method == "POST" and path == "/v1/tickets":
                ticket = secrets.token_urlsafe(32)
                now = int(time.time())
                db.execute("DELETE FROM join_tickets WHERE expires_at<=? OR consumed_at IS NOT NULL", (now,))
                db.execute("INSERT INTO join_tickets VALUES(?,?,?,NULL)",
                           (token_hash(ticket), account_id, now + TICKET_TTL_SECONDS))
                return {"ticket": ticket, "expires_in": TICKET_TTL_SECONDS}
            for kind in ("party", "guild"):
                prefix = f"/v1/{kind}"
                if path == prefix and method == "GET":
                    return store.social_state(kind, account_id, db)
                if path.startswith(prefix + "/") and method == "POST":
                    return self._social(kind, path[len(prefix) + 1:], account_id, body, db)
        raise APIError(404, "not_found", "Endpoint not found.")

    def _social(self, kind: str, action: str, account_id: str, body: dict, db: sqlite3.Connection) -> dict:
        store = self.server.store
        state = store.social_state(kind, account_id, db)
        group_id = state[f"{kind}_id"]
        if action == "create":
            if group_id:
                raise APIError(409, "already_member", f"Leave the current {kind} first.")
            name = require_string(body, "name", 3, 32).strip() if kind == "guild" else ""
            if kind == "guild" and (len(name) < 3 or re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9 _'\-]{2,31}", name) is None):
                raise APIError(400, "invalid_request", "Invalid guild name.")
            if kind == "guild" and db.execute("SELECT 1 FROM social_groups WHERE kind='guild' AND name=? COLLATE NOCASE", (name,)).fetchone():
                raise APIError(409, "name_taken", "Guild name is unavailable.")
            group_id = str(uuid.uuid4())
            db.execute("INSERT INTO social_groups VALUES(?,?,?,?)", (group_id, kind, name, account_id))
            db.execute("INSERT INTO memberships VALUES(?,?,?,?)", (kind, account_id, group_id, time.time_ns()))
        elif action == "invite":
            if not group_id or state["leader_id"] != account_id:
                raise APIError(403, "forbidden", f"Only the {kind} leader can invite.")
            username = valid_username(body)
            recipient = db.execute("SELECT id FROM accounts WHERE username=?", (username,)).fetchone()
            if recipient is None:
                raise APIError(404, "not_found", "Player not found.")
            if db.execute("SELECT 1 FROM memberships WHERE kind=? AND account_id=?", (kind, recipient["id"])).fetchone():
                raise APIError(409, "already_member", f"Player already belongs to a {kind}.")
            limit = 5 if kind == "party" else 50
            if len(state["members"]) >= limit:
                raise APIError(409, "group_full", f"The {kind} is full.")
            invite_id = str(uuid.uuid4())
            now = int(time.time())
            db.execute("DELETE FROM invites WHERE expires_at<=?", (now,))
            # Replace any prior invitation to the same group rather than accumulating duplicates.
            db.execute("DELETE FROM invites WHERE group_id=? AND recipient_id=?", (group_id, recipient["id"]))
            db.execute("INSERT INTO invites VALUES(?,?,?,?,?,?)", (invite_id, kind, group_id, account_id, recipient["id"], now + INVITE_TTL_SECONDS))
            return {"invite_id": invite_id}
        elif action == "accept":
            if group_id:
                raise APIError(409, "already_member", f"Leave the current {kind} first.")
            invite_id = require_string(body, "invite_id", 36, 36)
            invite = db.execute("""
                SELECT invites.*, social_groups.leader_id FROM invites
                JOIN social_groups ON social_groups.id=invites.group_id
                WHERE invites.id=? AND invites.kind=?
            """, (invite_id, kind)).fetchone()
            if invite is None or invite["expires_at"] <= int(time.time()):
                raise APIError(409, "invalid_invite", "Invitation is no longer valid.")
            if invite["recipient_id"] != account_id:
                raise APIError(403, "forbidden", "This invitation belongs to another player.")
            if invite["inviter_id"] != invite["leader_id"]:
                raise APIError(409, "invalid_invite", "Invitation is no longer valid.")
            size = db.execute("SELECT COUNT(*) FROM memberships WHERE group_id=?", (invite["group_id"],)).fetchone()[0]
            if size >= (5 if kind == "party" else 50):
                raise APIError(409, "group_full", f"The {kind} is full.")
            db.execute("INSERT INTO memberships VALUES(?,?,?,?)", (kind, account_id, invite["group_id"], time.time_ns()))
            db.execute("DELETE FROM invites WHERE id=?", (invite_id,))
        elif action == "leave":
            if group_id:
                db.execute("DELETE FROM memberships WHERE kind=? AND account_id=?", (kind, account_id))
                remaining = db.execute("SELECT account_id FROM memberships WHERE group_id=? ORDER BY joined_at, account_id LIMIT 1", (group_id,)).fetchone()
                if remaining is None:
                    db.execute("DELETE FROM social_groups WHERE id=?", (group_id,))
                elif state["leader_id"] == account_id:
                    db.execute("UPDATE social_groups SET leader_id=? WHERE id=?", (remaining["account_id"], group_id))
                    # Explicitly revoke former leader invitations even if leadership later returns.
                    db.execute("DELETE FROM invites WHERE group_id=?", (group_id,))
            return {}
        else:
            raise APIError(404, "not_found", "Endpoint not found.")
        return store.social_state(kind, account_id, db)

    def _server_request(self, path: str, body: dict) -> dict:
        self._require_server()
        store = self.server.store
        with store.connection(mutate=True) as db:
            now = int(time.time())
            if path == "/v1/server/admit":
                ticket = require_string(body, "ticket", 16, 256)
                row = db.execute("SELECT * FROM join_tickets WHERE token_hash=?", (token_hash(ticket),)).fetchone()
                if row is None or row["consumed_at"] is not None or row["expires_at"] <= now:
                    raise APIError(401, "invalid_ticket", "Join ticket is invalid or expired.")
                active = db.execute("SELECT expires_at FROM leases WHERE character_id=?", (row["account_id"],)).fetchone()
                if active and active["expires_at"] > now:
                    raise APIError(409, "already_connected", "Character already has an active connection.")
                lease = secrets.token_urlsafe(32)
                db.execute("UPDATE join_tickets SET consumed_at=? WHERE token_hash=?", (now, token_hash(ticket)))
                db.execute("INSERT INTO leases VALUES(?,?,?) ON CONFLICT(character_id) DO UPDATE SET token_hash=excluded.token_hash, expires_at=excluded.expires_at",
                           (row["account_id"], token_hash(lease), now + LEASE_TTL_SECONDS))
                return {"character": store.character(row["account_id"], db), "lease": lease}
            character_id = store.require_lease(body, db)
            if path == "/v1/server/heartbeat":
                db.execute("UPDATE leases SET expires_at=? WHERE character_id=?", (now + LEASE_TTL_SECONDS, character_id))
                return {}
            if path == "/v1/server/release":
                db.execute("DELETE FROM leases WHERE character_id=?", (character_id,))
                return {}
            if path == "/v1/server/party":
                return store.social_state("party", character_id, db)
            if path != "/v1/server/mutate":
                raise APIError(404, "not_found", "Endpoint not found.")
            operation = require_string(body, "operation", 1, 32)
            request_id = require_string(body, "request_id", 1, 128)
            if operation not in ("chapter", "boss_reward", "dungeon_reward", "forge", "gather") or re.fullmatch(r"[A-Za-z0-9_.:\-]{1,128}", request_id) is None:
                raise APIError(400, "invalid_request", "Invalid mutation operation or request ID.")
            prior = db.execute("SELECT operation FROM mutations WHERE character_id=? AND request_id=?", (character_id, request_id)).fetchone()
            if prior:
                if prior["operation"] != operation:
                    raise APIError(409, "idempotency_conflict", "Request ID was used for a different operation.")
                return {"character": store.character(character_id, db), "duplicate": True}
            if operation == "chapter":
                db.execute("UPDATE characters SET chapter_started=1,revision=MIN(revision+1,2147483647) WHERE account_id=?", (character_id,))
            elif operation == "gather":
                db.execute("UPDATE characters SET emberstone=MIN(emberstone+1,1000000),revision=MIN(revision+1,2147483647) WHERE account_id=?", (character_id,))
            elif operation in ("boss_reward", "dungeon_reward"):
                db.execute("UPDATE characters SET sigils=MIN(sigils+1,1000000),victories=MIN(victories+1,1000000),dungeon_clears=MIN(dungeon_clears+?,1000000),revision=MIN(revision+1,2147483647) WHERE account_id=?",
                           (int(operation == "dungeon_reward"), character_id))
            else:
                character = store.character(character_id, db)
                if character["gear_tier"] >= 2:
                    raise APIError(409, "max_tier", "Equipment is already at maximum tier.")
                cost = 3 if character["gear_tier"] == 0 else 6
                if character["sigils"] < cost:
                    raise APIError(409, "insufficient_sigils", "Earn more sigils before upgrading.")
                db.execute("UPDATE characters SET sigils=sigils-?,gear_tier=gear_tier+1,revision=MIN(revision+1,2147483647) WHERE account_id=?", (cost, character_id))
            db.execute("INSERT INTO mutations VALUES(?,?,?,?)", (character_id, request_id, operation, now))
            return {"character": store.character(character_id, db), "duplicate": False}

    def _dispatch(self, method: str) -> None:
        try:
            if method == "GET" and self.path == "/health":
                with self.server.store.connection() as db:
                    db.execute("SELECT 1").fetchone()
                self._json(200, {"status": "ok"})
                return
            if method == "POST":
                body = self._body()
                if self.path == "/v1/register":
                    status, result = self._register(body)
                    self._json(status, result)
                    return
                if self.path == "/v1/login":
                    self._json(200, self._login(body))
                    return
                if self.path.startswith("/v1/server/"):
                    self._json(200, self._server_request(self.path, body))
                    return
            else:
                body = {}
            self._json(200, self._client(method, self.path, body))
        except APIError as error:
            self._json(error.status, {"error": {"code": error.code, "message": error.message}})
        except sqlite3.Error:
            self._json(503, {"error": {"code": "storage_unavailable", "message": "Realm storage is unavailable."}})
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception:
            # Avoid traceback/logging request content or credentials on unexpected failures.
            self._json(500, {"error": {"code": "internal_error", "message": "Realm request could not be completed."}})

    def do_GET(self) -> None:
        self._dispatch("GET")

    def do_POST(self) -> None:
        self._dispatch("POST")


def create_server(
    db_path: str | os.PathLike[str], server_token: str,
    host: str = "127.0.0.1", port: int = 0,
) -> RealmHTTPServer:
    if not isinstance(server_token, str) or len(server_token.encode("utf-8")) < 32:
        raise ValueError("EMBER_SERVER_TOKEN must contain at least 32 UTF-8 bytes.")
    if host not in ("127.0.0.1", "localhost"):
        raise ValueError("The development HTTP service must bind to loopback.")
    return RealmHTTPServer((host, port), RealmStore(db_path), server_token)


def main() -> None:
    parser = argparse.ArgumentParser(description="Emberfall loopback development realm service")
    parser.add_argument("--db", default="Saved/Realm/realm.sqlite")
    parser.add_argument("--port", type=int, default=8081)
    args = parser.parse_args()
    try:
        server = create_server(args.db, os.environ.get("EMBER_SERVER_TOKEN", ""), port=args.port)
    except (ValueError, OSError, sqlite3.Error) as error:
        # Initialization messages contain no credential values.
        parser.error(str(error))
    try:
        print(f"Emberfall development realm listening on loopback port {server.server_port}.", flush=True)
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
