# Shared helpers for the ci/ scripts. Source it:  . "$(dirname "$0")/lib.sh"
# Not meant to be executed directly.
set -euo pipefail

log()  { printf '\033[1;34m[ci]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[ci] error:\033[0m %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

repo_root() { git rev-parse --show-toplevel 2>/dev/null || pwd; }

# Print the given value into $GITHUB_ENV when running under GitHub Actions.
export_env() { # export_env NAME VALUE
    export "$1=$2"
    [ -n "${GITHUB_ENV:-}" ] && printf '%s=%s\n' "$1" "$2" >>"$GITHUB_ENV" || true
}
