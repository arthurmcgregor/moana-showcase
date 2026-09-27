#!/usr/bin/env bash
#
# Run the local development stack: PostGIS + Django + Vite, in one terminal.
#
# Usage:
#   scripts/dev.sh            # db + Django (:8000) + Vite (:5173); Ctrl-C stops all
#   scripts/dev.sh db         # start the dev database only, leave it running
#   scripts/dev.sh server     # Django only (starts the db first)
#   scripts/dev.sh frontend   # Vite only
#   scripts/dev.sh admin      # create/refresh the local admin account, then exit
#   scripts/dev.sh down       # stop the dev database
#
# The database is server/docker-compose.yml — the *dev* stack, on port 5436 with
# a persistent volume. The repo-root docker-compose.yml is the production-shaped
# stack (it also builds the Django and nginx images from prod.env) and is not
# what you want locally. The test suite uses its own throwaway DB on 5434; see
# server/scripts/run-tests.sh.
#
# Django and Vite run as background jobs in their own process groups (set -m), so
# a single Ctrl-C tears down the whole tree rather than orphaning node or python.
# The database is deliberately left running — `scripts/dev.sh down` stops it.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVER_DIR="$ROOT/server"
FRONTEND_DIR="$ROOT/frontend"
VENV_PYTHON="$SERVER_DIR/.venv/bin/python"
COMPOSE=(docker compose -f "$SERVER_DIR/docker-compose.yml")

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; BLUE=$'\033[34m'; CYAN=$'\033[36m'; RESET=$'\033[0m'

say()  { printf '%s==>%s %s\n' "$GREEN" "$RESET" "$1"; }
die()  { printf '%sError:%s %s\n' "$RED" "$RESET" "$1" >&2; exit 1; }

# --- preflight ---------------------------------------------------------------

require_setup() {
  [[ -f "$SERVER_DIR/.env" ]] || die "server/.env is missing. Run: mise run dev:setup"
  [[ -x "$VENV_PYTHON" ]]     || die "server/.venv is missing. Run: mise run dev:setup"
  docker info >/dev/null 2>&1 || die "Docker is not running. Start Docker Desktop and retry."
}

require_frontend() {
  [[ -d "$FRONTEND_DIR/node_modules" ]] || die "frontend/node_modules is missing. Run: mise run dev:setup"
  if [[ ! -f "$FRONTEND_DIR/.env.local" ]]; then
    printf '%sWarning:%s frontend/.env.local is missing — the map will not render without VITE_MAPBOX_ACCESS_TOKEN.\n' \
      "$YELLOW" "$RESET" >&2
  fi
}

# Load server/.env so Django sees POSTGRES_* etc. when run outside a mise shell.
# Compose reads the same file, so the container and host-side Django agree.
load_server_env() {
  set -a
  # shellcheck disable=SC1091
  source "$SERVER_DIR/.env"
  set +a
}

# The repo-root stack publishes 8000 and 5432 too. Catch the clash here with a usable
# message instead of letting runserver die on "Address already in use".
require_free_port() {
  local port="$1" what="$2"
  if lsof -nP -iTCP:"$port" -sTCP:LISTEN >/dev/null 2>&1; then
    local owner
    owner="$(docker ps --filter "publish=$port" --format '{{.Names}}' 2>/dev/null | head -1)"
    if [[ -n "$owner" ]]; then
      die "port $port ($what) is held by container '$owner'.
       If that is the repo-root production-shaped stack, stop it:
         docker compose -f docker-compose.yml down"
    fi
    die "port $port ($what) is already in use. Free it, or change the port in server/.env
       (POSTGRES_PORT for the database, WEB_PORT for Django)."
  fi
}

# --- database ----------------------------------------------------------------

# Not port-preflighted: leaving the database up between sessions is the point, so a
# listener on POSTGRES_PORT is usually our own container. Compose is idempotent here.
start_db() {
  say "Starting the dev database (PostGIS on port ${POSTGRES_PORT:-5436})..."
  if ! "${COMPOSE[@]}" up -d --wait 2>&1; then
    die "the dev database did not come up. If port ${POSTGRES_PORT:-5436} is taken by
       something else, change POSTGRES_PORT in server/.env — compose and Django both
       read it, so one edit moves the pair."
  fi
}

stop_db() {
  say "Stopping the dev database (the volume is kept)..."
  "${COMPOSE[@]}" stop
}

migrate() {
  say "Applying migrations..."
  (cd "$SERVER_DIR/moana" && "$VENV_PYTHON" manage.py migrate --no-input)
}

# DevRemoteUserMiddleware already creates this account on the first request; what
# the command adds is a known password and superuser, so /admin/ works with
# DEV_AUTO_LOGIN=0. Idempotent — it resets an existing account to that state.
ensure_admin() {
  say "Ensuring the dev admin account..."
  (cd "$SERVER_DIR/moana" && "$VENV_PYTHON" manage.py devadmin)
}

# --- process supervision ------------------------------------------------------
#
# Deliberately no `set -m`: job control in a non-interactive bash makes the shell
# ignore SIGINT, and an ignored signal cannot be trapped — Ctrl-C would then leave
# python and node running. Without it the jobs stay in this script's process group,
# so a terminal Ctrl-C reaches them directly and the trap below only has to reap.

PIDS=()

# start <label> <colour> <workdir> <command...>
#
# `exec` replaces the subshell with the command, so $! is the real python/node pid
# rather than a wrapper's. Output goes through awk for the [label] prefix; fflush
# keeps it line-by-line instead of block-buffered.
start() {
  local label="$1" colour="$2" workdir="$3"; shift 3
  ( cd "$workdir"; exec "$@" ) \
    > >(awk -v tag="${colour}[${label}]${RESET} " '{ print tag $0; fflush() }') 2>&1 &
  PIDS+=("$!")
}

# Signal a process and everything below it. npm spawns vite as a child and Django's
# autoreloader forks a worker, so signalling the top pid alone strands one of each.
kill_tree() {
  local pid="$1" child
  for child in $(pgrep -P "$pid" 2>/dev/null); do
    kill_tree "$child"
  done
  kill -TERM "$pid" 2>/dev/null || true
}

cleanup() {
  trap - INT TERM EXIT
  printf '\n'
  say "Shutting down..."
  local pid
  for pid in ${PIDS[@]+"${PIDS[@]}"}; do
    kill_tree "$pid"
  done
  wait 2>/dev/null || true
  say "Stopped. The database is still up — 'mise run dev:down' stops it."
}

# Ctrl-C is how this script is meant to end, so exit 0 — otherwise every session
# closes with `mise run dev` reporting a failed task.
on_signal() {
  cleanup
  exit 0
}

# Block until any supervised job exits, then tear the rest down.
supervise() {
  trap on_signal INT TERM
  trap cleanup EXIT
  local pid
  while :; do
    for pid in "${PIDS[@]}"; do
      kill -0 "$pid" 2>/dev/null || return 0
    done
    sleep 1
  done
}

start_server_job() {
  require_free_port "${WEB_PORT:-8000}" "Django"
  start "server" "$BLUE" "$SERVER_DIR/moana" \
    "$VENV_PYTHON" manage.py runserver "0.0.0.0:${WEB_PORT:-8000}"
}

start_frontend_job() {
  # Vite would quietly fall back to 5174 if 5173 were taken, which then fails CORS
  # against CORS_ALLOWED_ORIGINS. Better to stop with an explanation.
  require_free_port 5173 "Vite"
  # Through mise so frontend/mise.toml applies — the node pin and VITE_APP_VERSION,
  # neither of which this script inherits when invoked from the repo root.
  start "frontend" "$CYAN" "$FRONTEND_DIR" mise exec -- npm run dev
}

# --- entrypoints ---------------------------------------------------------------

case "${1:-up}" in
  down)
    stop_db
    ;;

  db)
    require_setup
    load_server_env
    start_db
    say "Database ready on localhost:${POSTGRES_PORT:-5436} (db '${POSTGRES_DB:-moana}')."
    ;;

  admin)
    require_setup
    load_server_env
    start_db
    migrate
    ensure_admin
    ;;

  server)
    require_setup
    load_server_env
    start_db
    migrate
    ensure_admin
    start_server_job
    say "Django on http://localhost:${WEB_PORT:-8000}"
    supervise
    ;;

  frontend)
    require_frontend
    start_frontend_job
    say "Vite on http://localhost:5173"
    supervise
    ;;

  up)
    require_setup
    require_frontend
    load_server_env
    start_db
    migrate
    ensure_admin
    start_server_job
    start_frontend_job
    printf '\n%sMoana dev stack%s\n' "$GREEN" "$RESET"
    printf '  frontend  http://localhost:5173\n'
    printf '  api       http://localhost:%s/api/tracks/\n' "${WEB_PORT:-8000}"
    printf '  admin     http://localhost:%s/admin/\n' "${WEB_PORT:-8000}"
    printf '  database  localhost:%s/%s\n' "${POSTGRES_PORT:-5436}" "${POSTGRES_DB:-moana}"
    printf '  you are      %s%s%s (faked Remote-User; DEV_AUTO_LOGIN=%s in server/.env)\n\n' \
      "$YELLOW" "${DEV_ADMIN_USERNAME:-admin}" "$RESET" "${DEV_AUTO_LOGIN:-1}"
    supervise
    ;;

  *)
    die "unknown command '$1'. Use: up | db | server | frontend | admin | down"
    ;;
esac
