"""Standard-library client for the local development realm service.

Passwords are read from a terminal; account sessions are saved privately on disk.
Only ``ticket`` deliberately prints a short-lived credential for Unreal admission.
"""

from __future__ import annotations

import argparse
import getpass
import http.client
import ipaddress
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
from urllib import error, parse, request
import warnings


MAX_RESPONSE_BYTES = 65_536
MAX_SESSION_BYTES = 8_192


class ClientError(Exception):
    """A safe user-facing error that contains no remote response or credential."""


class NoRedirect(request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # Never forward an Authorization header or a password to another location.
        return None


def validate_url(value: str) -> str:
    try:
        url = parse.urlsplit(value)
        port = url.port
    except ValueError:
        raise ClientError("Invalid realm URL.") from None
    if (
        url.scheme not in ("http", "https") or not url.hostname
        or url.username is not None or url.password is not None
        or url.path not in ("", "/") or url.query or url.fragment
        or any(ord(char) < 33 for char in value)
        or (port is not None and not 1 <= port <= 65535)
    ):
        raise ClientError("Use a root http(s) realm URL without credentials, query, or fragment.")
    if url.scheme == "http" and not is_loopback(url.hostname):
        raise ClientError("Remote realm connections require HTTPS; HTTP is only allowed on loopback.")
    return parse.urlunsplit((url.scheme, url.netloc.lower(), "", "", ""))


def is_loopback(host: str) -> bool:
    if host.lower() == "localhost":
        return True
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return False


class RealmClient:
    def __init__(self, url: str, session_file: str | os.PathLike[str]):
        self.url = validate_url(url)
        self.session_file = Path(session_file)
        handlers: list = [NoRedirect()]
        if is_loopback(parse.urlsplit(self.url).hostname or ""):
            # Loopback belongs to this machine, not to an outbound HTTP proxy.
            handlers.append(request.ProxyHandler({}))
        self.opener = request.build_opener(*handlers)

    def session_token(self) -> str:
        try:
            if self.session_file.is_symlink():
                raise ClientError("The session file must be a regular private file, not a symlink.")
            with self.session_file.open("rb") as stream:
                info = os.fstat(stream.fileno())
                if not stat.S_ISREG(info.st_mode):
                    raise ClientError("The session file must be a regular private file.")
                if os.name == "posix" and info.st_mode & 0o077:
                    raise ClientError("The session file is not private; run chmod 600 on it or log in again.")
                raw = stream.read(MAX_SESSION_BYTES + 1)
            if len(raw) > MAX_SESSION_BYTES:
                raise ValueError
            session = json.loads(raw)
            if not isinstance(session, dict) or session.get("url") != self.url:
                raise ClientError("This session belongs to a different realm URL; log in for this realm.")
            token = session.get("session_token")
            if not isinstance(token, str) or not token or len(token) > 512 or any(ord(c) < 33 for c in token):
                raise ValueError
            return token
        except FileNotFoundError:
            raise ClientError("No saved session. Run login first.") from None
        except (OSError, ValueError, UnicodeError):
            raise ClientError("Cannot read a valid session file. Run login again.") from None

    def save_session(self, token: str) -> None:
        if not isinstance(token, str) or not token or len(token) > 512 or any(ord(c) < 33 for c in token):
            raise ClientError("The realm returned an invalid session credential.")
        temporary: str | None = None
        try:
            self.session_file.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
            fd, temporary = tempfile.mkstemp(prefix=".realm-session-", dir=self.session_file.parent)
            with os.fdopen(fd, "w", encoding="utf-8") as stream:
                # mkstemp creates mode 0600. Explicitly retain it on POSIX.
                if os.name == "posix":
                    os.fchmod(stream.fileno(), 0o600)
                json.dump({"url": self.url, "session_token": token}, stream)
                stream.write("\n")
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, self.session_file)
            temporary = None
        except OSError:
            raise ClientError("Could not save the private session file.") from None
        finally:
            if temporary is not None:
                try:
                    os.unlink(temporary)
                except OSError:
                    pass

    def call(self, method: str, path: str, body: dict | None = None, *, authenticated: bool = True) -> dict:
        headers = {"Accept": "application/json"}
        if authenticated:
            headers["Authorization"] = "Bearer " + self.session_token()
        data = None
        if body is not None:
            headers["Content-Type"] = "application/json"
            data = json.dumps(body).encode("utf-8")
        req = request.Request(self.url + path, data=data, headers=headers, method=method)
        try:
            with self.opener.open(req, timeout=10) as response:
                raw = response.read(MAX_RESPONSE_BYTES + 1)
        except error.HTTPError as failure:
            # A service or intermediary may echo request secrets in its body.
            # Report only trusted local descriptions of the HTTP status.
            failure.close()
            descriptions = {
                400: "Invalid request; check account, name, or invitation details.",
                401: "Authentication failed or expired; check your credentials and log in again.",
                403: "This action is not permitted for your account or group role.",
                404: "The requested account, invitation, group, or endpoint was not found.",
                409: "The request conflicts with existing account or group state.",
                429: "Too many requests; wait before retrying.",
                503: "The realm service is unavailable.",
            }
            description = descriptions.get(failure.code, "The realm rejected the request.")
            raise ClientError(f"HTTP {failure.code}: {description}") from None
        except (error.URLError, OSError, ValueError, http.client.HTTPException):
            raise ClientError("Could not reach the realm securely. Check its URL and service status.") from None
        if len(raw) > MAX_RESPONSE_BYTES:
            raise ClientError("The realm returned an oversized response.")
        try:
            result = json.loads(raw)
        except (ValueError, UnicodeError):
            raise ClientError("The realm returned invalid JSON.") from None
        if not isinstance(result, dict):
            raise ClientError("The realm returned an invalid response.")
        return result


def password_prompt(confirm: bool = False) -> str:
    try:
        with warnings.catch_warnings():
            # getpass's fallback can echo input; refuse that mode.
            warnings.simplefilter("error", getpass.GetPassWarning)
            password = getpass.getpass("Password: ")
            if confirm and password != getpass.getpass("Confirm password: "):
                raise ClientError("Passwords did not match.")
    except (EOFError, getpass.GetPassWarning):
        raise ClientError("Private password input is unavailable. Run this command in an interactive terminal.") from None
    return password


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description="Emberfall development realm account client")
    root.add_argument("--url", default="http://127.0.0.1:8081", help="Realm origin; remote connections require HTTPS")
    root.add_argument("--session-file", default="Saved/Realm/client-session.json", help="Private account session path")
    commands = root.add_subparsers(dest="command", required=True)
    for command in ("register", "login"):
        commands.add_parser(command).add_argument("username")
    commands.add_parser("ticket", help="Print a one-use, short-lived Unreal join ticket")
    commands.add_parser("character", help="Read your server-stored character")
    for group in ("party", "guild"):
        subcommands = commands.add_parser(group).add_subparsers(dest="action", required=True)
        create = subcommands.add_parser("create")
        if group == "guild":
            create.add_argument("name")
        subcommands.add_parser("invite").add_argument("username")
        subcommands.add_parser("accept").add_argument("invite_id")
        subcommands.add_parser("leave")
        subcommands.add_parser("show")
    return root


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        client = RealmClient(args.url, args.session_file)
        if args.command in ("register", "login"):
            result = client.call(
                "POST", "/v1/" + args.command,
                {"username": args.username, "password": password_prompt(confirm=args.command == "register")},
                authenticated=False,
            )
            if args.command == "register":
                print("Account created. Run login to save an account session.")
            else:
                client.save_session(result.get("session_token"))
                print("Logged in. Account session saved privately; use ticket to join Unreal.")
        elif args.command == "ticket":
            result = client.call("POST", "/v1/tickets", {})
            ticket = result.get("ticket")
            if not isinstance(ticket, str) or not ticket or len(ticket) > 512 or any(ord(c) < 33 for c in ticket):
                raise ClientError("The realm returned an invalid join ticket.")
            print(ticket)
        elif args.command == "character":
            result = client.call("GET", "/v1/character")
            character = result.get("character")
            if not isinstance(character, dict):
                raise ClientError("The realm returned an invalid character record.")
            fields = ("character_id", "username", "gear_tier", "sigils", "victories",
                      "dungeon_clears", "emberstone", "chapter_started", "revision")
            print(json.dumps({"character": {key: character[key] for key in fields if key in character}}, indent=2))
        else:
            path = "/v1/" + args.command
            body: dict = {}
            if args.action == "show":
                result = client.call("GET", path)
            else:
                if args.action == "create" and args.command == "guild":
                    body["name"] = args.name
                elif args.action == "invite":
                    body["username"] = args.username
                elif args.action == "accept":
                    body["invite_id"] = args.invite_id
                result = client.call("POST", path + "/" + args.action, body)
            # Only the documented social fields are suitable for terminal output.
            fields = ("party_id", "guild_id", "name", "members", "leader_id", "invite_id")
            print(json.dumps({key: result[key] for key in fields if key in result}, indent=2))
        return 0
    except ClientError as failure:
        print(str(failure), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Cancelled.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
