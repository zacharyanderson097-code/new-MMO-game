"""HTTP/SQLite integration tests for the local development realm service.

These tests exercise the backend, not Unreal networking or a production MMO.
All credentials are disposable fixture values; no environment secrets are read.
"""

from concurrent.futures import ThreadPoolExecutor
import http.client
import json
from pathlib import Path
import secrets
import sqlite3
import tempfile
import threading
import time
import unittest

from backend.realm import MAX_BODY_BYTES, create_server


class RealmIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="emberfall-realm-test-")
        self.addCleanup(self.directory.cleanup)
        self.db_path = Path(self.directory.name) / "realm.sqlite"
        self.server_token = secrets.token_urlsafe(40)
        self.start_server()
        self.addCleanup(self.stop_server)

    def start_server(self):
        self.server = create_server(self.db_path, self.server_token, port=0)
        self.port = self.server.server_port
        self.thread = threading.Thread(
            target=self.server.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True
        )
        self.thread.start()

    def stop_server(self):
        if self.server is not None:
            self.server.shutdown()
            self.server.server_close()
            self.thread.join(timeout=5)
            self.assertFalse(self.thread.is_alive(), "HTTP test server did not stop")
            self.server = None

    def request(self, method, path, body=None, token=None, *, raw=None, headers=None):
        request_headers = {"Content-Type": "application/json"}
        if token is not None:
            request_headers["Authorization"] = "Bearer " + token
        if headers:
            request_headers.update(headers)
        payload = raw if raw is not None else (json.dumps(body).encode() if body is not None else None)
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=15)
        try:
            connection.request(method, path, payload, request_headers)
            response = connection.getresponse()
            status = response.status
            response_headers = dict(response.getheaders())
            data = json.loads(response.read())
            return status, data, response_headers
        finally:
            connection.close()

    def post(self, path, body, token=None, expected=200):
        status, data, _ = self.request("POST", path, body, token)
        self.assertEqual(status, expected, f"Unexpected status from {path}: {data}")
        return data

    def get(self, path, token=None, expected=200):
        status, data, _ = self.request("GET", path, token=token)
        self.assertEqual(status, expected, f"Unexpected status from {path}: {data}")
        return data

    def account(self, username="Wayfarer"):
        password = "disposable-fixture-password"
        registration = self.post("/v1/register", {"username": username, "password": password}, expected=201)
        login = self.post("/v1/login", {"username": username, "password": password})
        self.assertEqual(login["character"]["character_id"], registration["character_id"])
        return {"id": registration["character_id"], "token": login["session_token"], "username": username}

    def character(self, account):
        return self.get("/v1/character", account["token"])["character"]

    def admit(self, account):
        ticket = self.post("/v1/tickets", {}, account["token"])["ticket"]
        admission = self.post("/v1/server/admit", {"ticket": ticket}, self.server_token)
        self.assertEqual(admission["character"]["character_id"], account["id"])
        self.assertTrue(admission["lease"])
        return admission["lease"]

    def mutate(self, account, lease, operation, request_id=None, *, expected=200, **extra):
        body = {"character_id": account["id"], "lease": lease, "operation": operation,
                "request_id": request_id or secrets.token_hex(16)}
        body.update(extra)
        return self.post("/v1/server/mutate", body, self.server_token, expected)

    def earn(self, account, lease, count):
        for _ in range(count):
            self.mutate(account, lease, "boss_reward")

    def expire(self, table):
        # Expiration is deterministic; tests do not sleep through production TTLs.
        self.assertIn(table, ("sessions", "join_tickets", "leases"))
        with sqlite3.connect(self.db_path) as db:
            db.execute(f"UPDATE {table} SET expires_at=?", (int(time.time()) - 1,))

    def create_group(self, account, kind="party", name="Wayfire Wardens"):
        body = {"name": name} if kind == "guild" else {}
        return self.post(f"/v1/{kind}/create", body, account["token"])

    def invite(self, leader, recipient, kind="party"):
        return self.post(f"/v1/{kind}/invite", {"username": recipient["username"]}, leader["token"])["invite_id"]

    def accept(self, recipient, invite_id, kind="party", expected=200):
        return self.post(f"/v1/{kind}/accept", {"invite_id": invite_id}, recipient["token"], expected)

    def test_health_checks_storage_and_disables_caching(self):
        status, data, headers = self.request("GET", "/health")
        self.assertEqual(status, 200)
        self.assertEqual(data, {"status": "ok"})
        self.assertEqual(headers["Cache-Control"], "no-store")
        self.assertEqual(headers["X-Content-Type-Options"], "nosniff")

    def test_registration_login_and_initial_character(self):
        player = self.account()
        character = self.character(player)
        self.assertEqual(character["username"], player["username"])
        self.assertEqual(character["gear_tier"], 0)
        self.assertEqual(character["sigils"], 0)
        self.assertEqual(character["victories"], 0)
        self.assertEqual(character["dungeon_clears"], 0)
        self.assertFalse(character["chapter_started"])

    def test_case_insensitive_duplicate_registration(self):
        self.account("Wayfarer")
        self.post("/v1/register", {"username": "wayfarer", "password": "another-test-password"}, expected=409)

    def test_registration_rejects_invalid_username_and_password(self):
        for username, password in (("ab", "long-enough-password"), ("7invalid", "long-enough-password"),
                                   ("Invalid Name", "long-enough-password"), ("A" * 25, "long-enough-password"),
                                   ("ValidName", "short"), ("ValidName", "A" * 129),
                                   ([], "long-enough-password"), ("ValidName", None)):
            with self.subTest(username_type=type(username).__name__, password_length=len(password) if isinstance(password, str) else None):
                self.post("/v1/register", {"username": username, "password": password}, expected=400)

    def test_login_rejects_wrong_password_and_unknown_user(self):
        self.account()
        for username, password in (("Wayfarer", "incorrect-password"), ("MissingUser", "disposable-fixture-password")):
            self.post("/v1/login", {"username": username, "password": password}, expected=401)

    def test_character_requires_valid_account_session(self):
        self.get("/v1/character", expected=401)
        self.get("/v1/character", "invalid-session", expected=401)
        self.get("/v1/character", self.server_token, expected=401)

    def test_expired_account_session_is_denied(self):
        player = self.account()
        self.expire("sessions")
        self.get("/v1/character", player["token"], expected=401)
        self.post("/v1/tickets", {}, player["token"], expected=401)

    def test_ticket_is_short_lived_and_one_use(self):
        player = self.account()
        ticket = self.post("/v1/tickets", {}, player["token"])
        self.assertEqual(ticket["expires_in"], 90)
        admission = self.post("/v1/server/admit", {"ticket": ticket["ticket"]}, self.server_token)
        self.assertEqual(admission["character"]["character_id"], player["id"])
        status, _, _ = self.request("POST", "/v1/server/admit", {"ticket": ticket["ticket"]}, self.server_token)
        self.assertEqual(status, 401)

    def test_expired_ticket_cannot_admit_character(self):
        player = self.account()
        ticket = self.post("/v1/tickets", {}, player["token"])["ticket"]
        self.expire("join_tickets")
        self.post("/v1/server/admit", {"ticket": ticket}, self.server_token, expected=401)

    def test_only_server_can_admit_or_mutate(self):
        player = self.account()
        ticket = self.post("/v1/tickets", {}, player["token"])["ticket"]
        for token in (None, "wrong-server-token", player["token"]):
            self.post("/v1/server/admit", {"ticket": ticket}, token, expected=401)
        lease = self.admit(player)
        for endpoint, body in (("mutate", {"character_id": player["id"], "lease": lease, "operation": "boss_reward", "request_id": "forged-client-reward"}),
                               ("heartbeat", {"character_id": player["id"], "lease": lease}),
                               ("release", {"character_id": player["id"], "lease": lease}),
                               ("party", {"character_id": player["id"], "lease": lease})):
            self.post(f"/v1/server/{endpoint}", body, player["token"], expected=401)
        self.assertEqual(self.character(player)["sigils"], 0)

    def test_second_active_admission_is_rejected(self):
        player = self.account()
        self.admit(player)
        ticket = self.post("/v1/tickets", {}, player["token"])["ticket"]
        self.post("/v1/server/admit", {"ticket": ticket}, self.server_token, expected=409)

    def test_heartbeat_renews_current_lease(self):
        player = self.account()
        lease = self.admit(player)
        with sqlite3.connect(self.db_path) as db:
            db.execute("UPDATE leases SET expires_at=?", (int(time.time()) + 2,))
        self.post("/v1/server/heartbeat", {"character_id": player["id"], "lease": lease}, self.server_token)
        with sqlite3.connect(self.db_path) as db:
            expiration = db.execute("SELECT expires_at FROM leases WHERE character_id=?", (player["id"],)).fetchone()[0]
        self.assertGreaterEqual(expiration, int(time.time()) + 50)

    def test_expired_lease_cannot_mutate_or_heartbeat(self):
        player = self.account()
        lease = self.admit(player)
        self.expire("leases")
        self.mutate(player, lease, "boss_reward", expected=403)
        self.post("/v1/server/heartbeat", {"character_id": player["id"], "lease": lease}, self.server_token, expected=403)
        self.assertEqual(self.character(player)["sigils"], 0)

    def test_release_and_readmit_fences_old_lease(self):
        player = self.account()
        old_lease = self.admit(player)
        self.post("/v1/server/release", {"character_id": player["id"], "lease": old_lease}, self.server_token)
        new_lease = self.admit(player)
        self.assertNotEqual(old_lease, new_lease)
        self.mutate(player, old_lease, "boss_reward", expected=403)
        self.post("/v1/server/release", {"character_id": player["id"], "lease": old_lease}, self.server_token, expected=403)
        self.mutate(player, new_lease, "chapter")

    def test_wrong_character_lease_cannot_grant_rewards(self):
        first, second = self.account("FirstPlayer"), self.account("SecondPlayer")
        lease = self.admit(first)
        self.mutate(second, lease, "boss_reward", expected=403)
        self.assertEqual(self.character(second)["sigils"], 0)

    def test_chapter_progress_is_durable_and_idempotent(self):
        player = self.account()
        lease = self.admit(player)
        result = self.mutate(player, lease, "chapter", "chapter-request")
        self.assertTrue(result["character"]["chapter_started"])
        self.assertFalse(result["duplicate"])
        repeated = self.mutate(player, lease, "chapter", "chapter-request")
        self.assertTrue(repeated["duplicate"])
        self.assertEqual(result["character"]["revision"], repeated["character"]["revision"])

    def test_boss_reward_granted_once_per_request(self):
        player = self.account()
        lease = self.admit(player)
        self.mutate(player, lease, "chapter")
        result = self.mutate(player, lease, "boss_reward", "encounter-1")
        self.assertFalse(result["duplicate"])
        self.assertEqual(result["character"]["sigils"], 1)
        self.assertEqual(result["character"]["victories"], 1)
        repeated = self.mutate(player, lease, "boss_reward", "encounter-1")
        self.assertTrue(repeated["duplicate"])
        self.assertEqual(repeated["character"]["sigils"], 1)

    def test_concurrent_duplicate_reward_requests_are_atomic(self):
        player = self.account()
        lease = self.admit(player)
        self.mutate(player, lease, "chapter")
        body = {"character_id": player["id"], "lease": lease, "operation": "boss_reward", "request_id": "same-encounter"}
        with ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(lambda _: self.request("POST", "/v1/server/mutate", body, self.server_token), range(12)))
        self.assertTrue(all(status == 200 for status, _, _ in results))
        self.assertEqual(sum(not data["duplicate"] for _, data, _ in results), 1)
        character = self.character(player)
        self.assertEqual(character["sigils"], 1)
        self.assertEqual(character["victories"], 1)

    def test_request_id_cannot_be_reused_for_different_operation(self):
        player = self.account()
        lease = self.admit(player)
        self.mutate(player, lease, "chapter", "fixed-operation")
        self.mutate(player, lease, "boss_reward", "fixed-operation", expected=409)
        self.assertEqual(self.character(player)["sigils"], 0)

    def test_dungeon_clear_records_currency_victory_and_clear(self):
        player = self.account()
        lease = self.admit(player)
        self.mutate(player, lease, "chapter")
        result = self.mutate(player, lease, "dungeon_reward")["character"]
        self.assertEqual((result["sigils"], result["victories"], result["dungeon_clears"]), (1, 1, 1))

    def test_forge_requires_exact_earned_costs_and_stops_at_maximum(self):
        player = self.account()
        lease = self.admit(player)
        self.mutate(player, lease, "chapter")
        initial = self.character(player)
        self.mutate(player, lease, "forge", expected=409)
        self.assertEqual(self.character(player), initial)
        self.earn(player, lease, 2)
        self.mutate(player, lease, "forge", expected=409)
        self.earn(player, lease, 1)
        tempered = self.mutate(player, lease, "forge")["character"]
        self.assertEqual((tempered["gear_tier"], tempered["sigils"]), (1, 0))
        self.earn(player, lease, 5)
        self.mutate(player, lease, "forge", expected=409)
        self.earn(player, lease, 1)
        dawbound = self.mutate(player, lease, "forge")["character"]
        self.assertEqual((dawbound["gear_tier"], dawbound["sigils"], dawbound["victories"]), (2, 0, 9))
        self.mutate(player, lease, "forge", expected=409)
        self.assertEqual(self.character(player), dawbound)

    def test_unknown_mutation_cannot_set_client_selected_currency(self):
        player = self.account()
        lease = self.admit(player)
        self.mutate(player, lease, "set_currency", expected=400, sigils=999999)
        self.assertEqual(self.character(player)["sigils"], 0)

    def test_gather_grants_one_resource_and_duplicate_request_grants_nothing(self):
        player = self.account()
        lease = self.admit(player)
        self.assertEqual(self.character(player)["emberstone"], 0)
        gathered = self.mutate(player, lease, "gather", "resource-node-1", amount=999999)
        self.assertFalse(gathered["duplicate"])
        self.assertEqual(gathered["character"]["emberstone"], 1)
        duplicate = self.mutate(player, lease, "gather", "resource-node-1")
        self.assertTrue(duplicate["duplicate"])
        self.assertEqual(duplicate["character"], gathered["character"])

    def test_gather_resource_cap_never_overflows(self):
        player = self.account()
        lease = self.admit(player)
        with sqlite3.connect(self.db_path) as db:
            db.execute("UPDATE characters SET emberstone=999999 WHERE account_id=?", (player["id"],))
        self.assertEqual(self.mutate(player, lease, "gather")["character"]["emberstone"], 1000000)
        self.assertEqual(self.mutate(player, lease, "gather")["character"]["emberstone"], 1000000)

    def test_rewards_and_revision_saturate_at_decoder_limits(self):
        player = self.account()
        lease = self.admit(player)
        with sqlite3.connect(self.db_path) as db:
            db.execute("""UPDATE characters SET sigils=999999,victories=999999,
                          dungeon_clears=999999,revision=2147483646 WHERE account_id=?""", (player["id"],))
        reached = self.mutate(player, lease, "dungeon_reward")["character"]
        self.assertEqual((reached["sigils"], reached["victories"], reached["dungeon_clears"]),
                         (1000000, 1000000, 1000000))
        self.assertEqual(reached["revision"], 2147483647)
        saturated = self.mutate(player, lease, "boss_reward", "saturated-reward")["character"]
        self.assertEqual(saturated, reached)
        duplicate = self.mutate(player, lease, "boss_reward", "saturated-reward")
        self.assertTrue(duplicate["duplicate"])
        self.assertEqual(duplicate["character"], saturated)

    def test_revision_saturation_preserves_lease_fencing_and_deduplication(self):
        player = self.account()
        old_lease = self.admit(player)
        with sqlite3.connect(self.db_path) as db:
            db.execute("UPDATE characters SET revision=2147483647 WHERE account_id=?", (player["id"],))
        first = self.mutate(player, old_lease, "boss_reward", "reward-at-max-revision")
        self.assertEqual(first["character"]["sigils"], 1)
        self.post("/v1/server/release", {"character_id": player["id"], "lease": old_lease}, self.server_token)
        new_lease = self.admit(player)
        # A recorded request still cannot bypass lease authorization on retry.
        self.mutate(player, old_lease, "boss_reward", "reward-at-max-revision", expected=403)
        duplicate = self.mutate(player, new_lease, "boss_reward", "reward-at-max-revision")
        self.assertTrue(duplicate["duplicate"])
        self.assertEqual(duplicate["character"]["sigils"], 1)
        subsequent = self.mutate(player, new_lease, "boss_reward", "distinct-reward-at-max-revision")
        self.assertFalse(subsequent["duplicate"])
        self.assertEqual(subsequent["character"]["sigils"], 2)
        self.assertEqual(subsequent["character"]["revision"], 2147483647)

    def test_all_mutation_types_respect_saturated_revision(self):
        player = self.account()
        lease = self.admit(player)
        with sqlite3.connect(self.db_path) as db:
            db.execute("UPDATE characters SET revision=2147483647,sigils=3 WHERE account_id=?", (player["id"],))
        for operation in ("chapter", "gather", "forge", "boss_reward", "dungeon_reward"):
            with self.subTest(operation=operation):
                result = self.mutate(player, lease, operation)
                self.assertFalse(result["duplicate"])
                self.assertEqual(result["character"]["revision"], 2147483647)
        character = self.character(player)
        self.assertTrue(character["chapter_started"])
        self.assertEqual((character["gear_tier"], character["emberstone"], character["sigils"]), (1, 1, 2))

    def test_registration_and_login_share_authentication_rate_budget(self):
        player = self.account()
        with self.server.rate_lock:
            self.server.login_attempts["127.0.0.1"] = (int(time.time()), 59)
        self.post("/v1/login", {"username": player["username"], "password": "disposable-fixture-password"})
        rejected = self.post("/v1/register", {"username": "BlockedPlayer", "password": "disposable-fixture-password"}, expected=429)
        self.assertEqual(rejected["error"]["code"], "rate_limited")
        with sqlite3.connect(self.db_path) as db:
            self.assertEqual(db.execute("SELECT COUNT(*) FROM accounts WHERE username='BlockedPlayer'").fetchone()[0], 0)
        with self.server.rate_lock:
            self.server.login_attempts["127.0.0.1"] = (int(time.time()), 59)
        self.post("/v1/register", {"username": "LastAllowed", "password": "disposable-fixture-password"}, expected=201)
        rejected_login = self.post("/v1/login", {"username": player["username"], "password": "disposable-fixture-password"}, expected=429)
        self.assertEqual(rejected_login["error"]["code"], "rate_limited")

    def test_authentication_rate_budget_resets_after_its_window(self):
        player = self.account()
        with self.server.rate_lock:
            self.server.login_attempts["127.0.0.1"] = (int(time.time()) - 61, 60)
        self.post("/v1/login", {"username": player["username"], "password": "disposable-fixture-password"})
        with self.server.rate_lock:
            self.assertEqual(self.server.login_attempts["127.0.0.1"][1], 1)

    def test_worker_limit_returns_busy_then_recovers(self):
        acquired = 0
        try:
            for _ in range(16):
                self.assertTrue(self.server.worker_slots.acquire(blocking=False))
                acquired += 1
            status, body, headers = self.request("GET", "/health")
            self.assertEqual(status, 503)
            self.assertEqual(body["error"]["code"], "server_busy")
            self.assertEqual(headers["Cache-Control"], "no-store")
        finally:
            for _ in range(acquired):
                self.server.worker_slots.release()
        self.assertEqual(self.get("/health"), {"status": "ok"})

    def test_restart_preserves_character_and_idempotency_ledger(self):
        player = self.account()
        lease = self.admit(player)
        self.mutate(player, lease, "chapter")
        before = self.mutate(player, lease, "boss_reward", "persistent-reward")["character"]
        self.stop_server()
        self.start_server()
        self.assertEqual(self.character(player), before)
        repeated = self.mutate(player, lease, "boss_reward", "persistent-reward")
        self.assertTrue(repeated["duplicate"])
        self.assertEqual(repeated["character"], before)

    def test_reward_idempotency_survives_a_new_lease(self):
        player = self.account()
        lease = self.admit(player)
        self.mutate(player, lease, "chapter")
        self.mutate(player, lease, "boss_reward", "lease-independent-reward")
        self.post("/v1/server/release", {"character_id": player["id"], "lease": lease}, self.server_token)
        new_lease = self.admit(player)
        repeated = self.mutate(player, new_lease, "boss_reward", "lease-independent-reward")
        self.assertTrue(repeated["duplicate"])
        self.assertEqual(repeated["character"]["sigils"], 1)

    def test_empty_party_then_create_and_invite_recipient(self):
        leader, member = self.account("PartyLeader"), self.account("PartyMember")
        self.assertEqual(self.get("/v1/party", leader["token"])["members"], [])
        created = self.create_group(leader)
        self.assertEqual(created["leader_id"], leader["id"])
        joined = self.accept(member, self.invite(leader, member))
        self.assertEqual(len(joined["members"]), 2)
        self.assertEqual(self.get("/v1/party", member["token"])["party_id"], created["party_id"])

    def test_only_party_leader_can_invite(self):
        leader, member, outsider = self.account("PartyLeader"), self.account("PartyMember"), self.account("Outsider")
        self.create_group(leader)
        self.accept(member, self.invite(leader, member))
        self.post("/v1/party/invite", {"username": outsider["username"]}, member["token"], expected=403)

    def test_only_named_invitation_recipient_can_accept(self):
        leader, recipient, intruder = self.account("PartyLeader"), self.account("Recipient"), self.account("Intruder")
        self.create_group(leader)
        invite_id = self.invite(leader, recipient)
        self.accept(intruder, invite_id, expected=403)
        self.assertEqual(len(self.accept(recipient, invite_id)["members"]), 2)

    def test_party_capacity_is_enforced_when_invitation_is_accepted(self):
        leader = self.account("PartyLeader")
        members = [self.account(f"Member{index}") for index in range(5)]
        self.create_group(leader)
        # Issue all invitations before filling the party: stale invitations must not bypass the cap.
        invitations = [self.invite(leader, member) for member in members]
        for member, invite_id in zip(members[:4], invitations[:4]):
            self.accept(member, invite_id)
        self.accept(members[4], invitations[4], expected=409)
        self.assertEqual(len(self.get("/v1/party", leader["token"])["members"]), 5)

    def test_accepting_party_invite_cannot_replace_an_existing_group(self):
        first, second = self.account("FirstLeader"), self.account("SecondLeader")
        self.create_group(first)
        invitation = self.invite(first, second)
        second_party = self.create_group(second)
        self.accept(second, invitation, expected=409)
        self.assertEqual(self.get("/v1/party", second["token"])["party_id"], second_party["party_id"])

    def test_departing_party_leader_promotes_remaining_member(self):
        leader, member = self.account("PartyLeader"), self.account("PartyMember")
        self.create_group(leader)
        self.accept(member, self.invite(leader, member))
        self.post("/v1/party/leave", {}, leader["token"])
        party = self.get("/v1/party", member["token"])
        self.assertEqual(party["leader_id"], member["id"])
        self.assertEqual(len(party["members"]), 1)
        self.post("/v1/party/leave", {}, member["token"])
        self.assertEqual(self.get("/v1/party", member["token"])["members"], [])

    def test_server_party_lookup_requires_current_character_lease(self):
        leader, member = self.account("PartyLeader"), self.account("PartyMember")
        party = self.create_group(leader)
        self.accept(member, self.invite(leader, member))
        lease = self.admit(member)
        result = self.post("/v1/server/party", {"character_id": member["id"], "lease": lease}, self.server_token)
        self.assertEqual(result["party_id"], party["party_id"])
        self.assertEqual(len(result["members"]), 2)
        self.expire("leases")
        self.post("/v1/server/party", {"character_id": member["id"], "lease": lease}, self.server_token, expected=403)

    def test_guild_membership_persists_across_service_restart(self):
        leader, member = self.account("GuildLeader"), self.account("GuildMember")
        guild = self.create_group(leader, "guild", "Keepers of the Wayfire")
        self.accept(member, self.invite(leader, member, "guild"), "guild")
        before = self.get("/v1/guild", member["token"])
        self.assertEqual(before["guild_id"], guild["guild_id"])
        self.assertEqual(before["name"], "Keepers of the Wayfire")
        self.stop_server()
        self.start_server()
        self.assertEqual(self.get("/v1/guild", member["token"]), before)

    def test_guild_invites_require_leader_and_named_recipient(self):
        leader, member, intruder = self.account("GuildLeader"), self.account("GuildMember"), self.account("Intruder")
        self.create_group(leader, "guild")
        invite_id = self.invite(leader, member, "guild")
        self.accept(intruder, invite_id, "guild", expected=403)
        self.accept(member, invite_id, "guild")
        self.post("/v1/guild/invite", {"username": intruder["username"]}, member["token"], expected=403)

    def test_guild_and_party_membership_are_independent(self):
        player = self.account()
        party = self.create_group(player)
        guild = self.create_group(player, "guild")
        self.assertEqual(self.get("/v1/party", player["token"])["party_id"], party["party_id"])
        self.assertEqual(self.get("/v1/guild", player["token"])["guild_id"], guild["guild_id"])

    def test_malformed_json_is_rejected_without_breaking_next_request(self):
        status, data, _ = self.request("POST", "/v1/register", raw=b"{broken")
        self.assertEqual(status, 400)
        self.assertEqual(data["error"]["code"], "invalid_json")
        self.assertEqual(self.get("/health"), {"status": "ok"})

    def test_oversized_request_is_rejected(self):
        status, _, _ = self.request("POST", "/v1/register", raw=b" " * (MAX_BODY_BYTES + 1))
        self.assertEqual(status, 400)

    def test_json_array_and_wrong_content_type_are_rejected(self):
        status, _, _ = self.request("POST", "/v1/register", raw=b"[]")
        self.assertEqual(status, 400)
        status, _, _ = self.request("POST", "/v1/register", raw=b"{}", headers={"Content-Type": "text/plain"})
        self.assertEqual(status, 415)

    def test_unknown_route_does_not_expose_a_currency_endpoint(self):
        player = self.account()
        self.post("/v1/currency", {"sigils": 999999}, player["token"], expected=404)
        self.assertEqual(self.character(player)["sigils"], 0)


if __name__ == "__main__":
    unittest.main()
