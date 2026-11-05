"""Repeatable privacy and HTTP workflow regressions for the realm CLI.

All credentials are disposable fixtures. These tests do not validate Unreal or a
production account deployment. Run with ``python3 -m unittest backend.test_client``.
"""

import contextlib
import getpass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
import os
from pathlib import Path
import secrets
import stat
import tempfile
import threading
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, build_opener, ProxyHandler
import warnings

from backend import client
from backend.realm import create_server


def start_http(server):
    thread = threading.Thread(
        target=server.serve_forever, kwargs={"poll_interval": 0.01}, daemon=True
    )
    thread.start()
    return thread


def stop_http(server, thread):
    server.shutdown()
    server.server_close()
    thread.join(timeout=5)
    if thread.is_alive():
        raise RuntimeError("The disposable HTTP test service did not stop.")


class ClientPrivacyTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="emberfall-client-test-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.session = self.root / "session.json"
        self.realm = client.RealmClient("http://127.0.0.1:8081", self.session)

    def test_only_loopback_http_and_clean_https_origins_are_accepted(self):
        for url in ("http://localhost:8081", "http://127.0.0.1:8081",
                    "http://[::1]:8081", "https://realm.example.org"):
            with self.subTest(url=url):
                self.assertTrue(client.validate_url(url))
        for url in ("http://realm.example.org", "http://127.0.0.1.evil.example",
                    "http://localhost:0", "https://realm.example.org/game",
                    "https://realm.example.org?token=example",
                    "https://user:example@realm.example.org", "http://localhost\n"):
            with self.subTest(url=url):
                with self.assertRaises(client.ClientError):
                    client.validate_url(url)

    def test_session_is_private_atomic_and_bound_to_its_realm(self):
        first, replacement = secrets.token_urlsafe(32), secrets.token_urlsafe(32)
        self.realm.save_session(first)
        self.assertTrue(self.realm.session_token() == first, "Saved session did not round-trip.")
        if os.name == "posix":
            self.assertEqual(stat.S_IMODE(self.session.stat().st_mode), 0o600)
        with self.assertRaises(client.ClientError):
            client.RealmClient("https://different.example.org", self.session).session_token()
        with patch("backend.client.os.replace", side_effect=OSError("disposable failure")):
            with self.assertRaises(client.ClientError):
                self.realm.save_session(replacement)
        self.assertTrue(self.realm.session_token() == first, "Failed replacement destroyed the prior session.")
        self.assertEqual(list(self.root.iterdir()), [self.session])

    def test_session_rejects_nonregular_symlink_and_public_files(self):
        self.realm.save_session(secrets.token_urlsafe(32))
        directory_client = client.RealmClient(self.realm.url, self.root)
        with self.assertRaises(client.ClientError):
            directory_client.session_token()
        if os.name == "posix":
            link = self.root / "session-link.json"
            link.symlink_to(self.session)
            with self.assertRaises(client.ClientError):
                client.RealmClient(self.realm.url, link).session_token()
            if Path("/dev/null").exists():
                with self.assertRaises(client.ClientError):
                    client.RealmClient(self.realm.url, "/dev/null").session_token()
            self.session.chmod(0o644)
            with self.assertRaises(client.ClientError):
                self.realm.session_token()

    def test_password_prompt_refuses_echo_fallback_and_mismatch(self):
        fallback_reached = []

        def unavailable_terminal(_prompt):
            warnings.warn("Private input unavailable", getpass.GetPassWarning)
            fallback_reached.append(True)
            return "must-not-be-read"

        with patch("backend.client.getpass.getpass", side_effect=unavailable_terminal):
            with self.assertRaises(client.ClientError):
                client.password_prompt()
        self.assertFalse(fallback_reached, "An echoing password fallback was reached.")
        with patch("backend.client.getpass.getpass", side_effect=["fixture-one", "fixture-two"]):
            with self.assertRaises(client.ClientError):
                client.password_prompt(confirm=True)
        with patch("backend.client.getpass.getpass", side_effect=EOFError):
            with self.assertRaises(client.ClientError):
                client.password_prompt()

    def test_http_authentication_errors_never_echo_untrusted_body_or_reason(self):
        token = secrets.token_urlsafe(32)
        self.realm.save_session(token)
        remote_secret = secrets.token_urlsafe(32)
        failure = HTTPError(
            self.realm.url, 401, remote_secret, {},
            io.BytesIO(json.dumps({"error": {"message": token}}).encode()),
        )
        with patch.object(self.realm.opener, "open", side_effect=failure):
            with self.assertRaises(client.ClientError) as caught:
                self.realm.call("GET", "/v1/character")
        message = str(caught.exception)
        self.assertIn("401", message)
        self.assertTrue(token not in message and remote_secret not in message,
                        "An untrusted credential appeared in the error description.")

    def test_redirects_do_not_forward_authorization_or_password(self):
        observed = []

        class RedirectHandler(BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass

            def respond(self):
                observed.append((self.path, bool(self.headers.get("Authorization"))))
                self.send_response(302)
                self.send_header("Location", "/must-not-be-requested")
                self.send_header("Content-Length", "0")
                self.end_headers()

            def do_GET(self):
                self.respond()

            def do_POST(self):
                self.rfile.read(int(self.headers.get("Content-Length", "0")))
                self.respond()

        server = ThreadingHTTPServer(("127.0.0.1", 0), RedirectHandler)
        thread = start_http(server)
        self.addCleanup(stop_http, server, thread)
        local = client.RealmClient(f"http://127.0.0.1:{server.server_port}", self.session)
        local.save_session(secrets.token_urlsafe(32))
        with self.assertRaises(client.ClientError):
            local.call("GET", "/account")
        with self.assertRaises(client.ClientError):
            local.call("POST", "/login", {"password": secrets.token_urlsafe(16)}, authenticated=False)
        self.assertEqual(observed, [("/account", True), ("/login", False)])


class ClientHTTPWorkflowTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="emberfall-cli-http-test-")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.server_secret = secrets.token_urlsafe(48)
        self.password = secrets.token_urlsafe(24)
        self.server = create_server(self.root / "realm.sqlite", self.server_secret)
        thread = start_http(self.server)
        self.addCleanup(stop_http, self.server, thread)
        self.url = f"http://127.0.0.1:{self.server.server_port}"

    def session_file(self, username):
        return self.root / (username + ".json")

    def run_cli(self, username, arguments, *, expected=0, password=None):
        stdout, stderr = io.StringIO(), io.StringIO()
        supplied_password = password or self.password
        with patch("backend.client.getpass.getpass", return_value=supplied_password), \
                contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = client.main([
                "--url", self.url, "--session-file", str(self.session_file(username)), *arguments
            ])
        self.assertEqual(code, expected, "Unexpected CLI exit status.")
        output = stdout.getvalue() + stderr.getvalue()
        known_secrets = [self.password, supplied_password, self.server_secret]
        for path in self.root.glob("*.json"):
            known_secrets.append(json.loads(path.read_text())["session_token"])
        self.assertTrue(all(value not in output for value in known_secrets),
                        "A password, account session, or server credential leaked to CLI output.")
        return stdout.getvalue(), stderr.getvalue()

    def account(self, username):
        self.run_cli(username, ["register", username])
        self.assertFalse(self.session_file(username).exists(), "Registration unexpectedly logged in.")
        self.run_cli(username, ["login", username])
        if os.name == "posix":
            self.assertEqual(stat.S_IMODE(self.session_file(username).stat().st_mode), 0o600)

    def test_register_login_character_and_intentionally_printed_one_use_ticket(self):
        self.account("WayfarerOne")
        self.run_cli("WayfarerOne", ["login", "WayfarerOne"], expected=1,
                     password=secrets.token_urlsafe(18))
        output, _ = self.run_cli("WayfarerOne", ["character"])
        character = json.loads(output)["character"]
        self.assertEqual(character["username"], "WayfarerOne")
        self.assertEqual(character["emberstone"], 0)
        ticket = self.run_cli("WayfarerOne", ["ticket"])[0].strip()
        self.assertTrue(16 <= len(ticket) <= 256, "CLI did not emit a join ticket.")
        payload = json.dumps({"ticket": ticket}).encode()
        opener = build_opener(ProxyHandler({}))
        for expected_status in (200, 401):
            req = Request(self.url + "/v1/server/admit", payload, {
                "Content-Type": "application/json", "Authorization": "Bearer " + self.server_secret,
            }, method="POST")
            try:
                with opener.open(req, timeout=10) as response:
                    status = response.status
                    response.read()
            except HTTPError as failure:
                status = failure.code
                failure.close()
            self.assertEqual(status, expected_status, "Printed join ticket did not have one-use semantics.")

    def group_lifecycle(self, kind):
        self.account("WayfarerOne")
        self.account("WayfarerTwo")
        create_args = [kind, "create"] + (["Dawnward Company"] if kind == "guild" else [])
        group = json.loads(self.run_cli("WayfarerOne", create_args)[0])
        invitation = json.loads(self.run_cli("WayfarerOne", [kind, "invite", "WayfarerTwo"])[0])
        joined = json.loads(self.run_cli("WayfarerTwo", [kind, "accept", invitation["invite_id"]])[0])
        self.assertEqual(joined[kind + "_id"], group[kind + "_id"])
        self.assertEqual(len(joined["members"]), 2)
        shown = json.loads(self.run_cli("WayfarerTwo", [kind, "show"])[0])
        self.assertEqual(shown, joined)
        self.run_cli("WayfarerTwo", [kind, "invite", "Nobody"], expected=1)
        self.run_cli("WayfarerTwo", [kind, "leave"])
        self.assertFalse(json.loads(self.run_cli("WayfarerTwo", [kind, "show"])[0])["members"])

    def test_party_lifecycle_through_cli_arguments(self):
        self.group_lifecycle("party")

    def test_guild_lifecycle_through_cli_arguments(self):
        self.group_lifecycle("guild")


if __name__ == "__main__":
    unittest.main()
