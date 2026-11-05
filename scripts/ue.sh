#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
action="${1:-editor}"
case "$action" in
    build-editor|build-game|build-server|editor|host|server|join) ;;
    *) printf 'Unknown action: %s\n' "$action" >&2; exit 2 ;;
esac

if [[ -z "${UE_ROOT:-}" || ! -d "$UE_ROOT/Engine" ]]; then
    printf '%s\n' 'Set UE_ROOT to a compatible installed Unreal Engine 5.5 root.' >&2
    exit 2
fi
project="$project_dir/Emberfall.uproject"
editor="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor"
build="$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh"

case "$action" in
    build-*)
        [[ -f "$build" ]] || { printf '%s\n' 'Linux Unreal Build.sh not found.' >&2; exit 2; }
        target=EmberfallEditor
        [[ "$action" != build-game ]] || target=Emberfall
        [[ "$action" != build-server ]] || target=EmberfallServer
        exec bash "$build" "$target" Linux Development "-Project=$project" -WaitMutex
        ;;
    *)
        [[ -x "$editor" ]] || { printf '%s\n' 'Linux UnrealEditor executable not found; build/install the engine first.' >&2; exit 2; }
        case "$action" in
            editor) exec "$editor" "$project" ;;
            host) exec "$editor" "$project" '/Engine/Maps/Entry?listen' -game -log -port=7777 ;;
            server) exec "$editor" "$project" /Engine/Maps/Entry -server -log -port=7777 ;;
            join) exec "$editor" "$project" "${2:-127.0.0.1:7777}" -game -log ;;
        esac
        ;;
esac
