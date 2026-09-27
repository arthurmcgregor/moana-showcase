#!/usr/bin/env bash
#
# Run the server test suite against an isolated, ephemeral PostGIS database.
#
# Usage:
#   scripts/run-tests.sh                 # start test DB (if needed) and run all tests
#   scripts/run-tests.sh tracks/tests/test_models.py   # pass args straight to pytest
#   scripts/run-tests.sh --down          # stop and remove the test DB, then exit
#
# The test DB runs via docker-compose.test.yml on host port 5434 (clear of the
# dev DB on 5436) and is left running between invocations for fast
# reruns. Tests connect through the POSTGRES_* env vars exported below, which
# override whatever mise loaded from server/.env.
set -euo pipefail

# Resolve paths relative to this script so it works from any CWD.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPOSE=(docker compose -f "$SERVER_DIR/docker-compose.test.yml")

if [[ "${1:-}" == "--down" ]]; then
  "${COMPOSE[@]}" down -v
  exit 0
fi

echo "==> Starting ephemeral test database (port 5434)..."
"${COMPOSE[@]}" up -d --wait

# Point Django at the test database. Explicit exports win over server/.env.
export POSTGRES_HOST=localhost
export POSTGRES_PORT=5434
export POSTGRES_DB=moana
export POSTGRES_USER=moana_user
export POSTGRES_PASSWORD=moana_test

echo "==> Running pytest..."
cd "$SERVER_DIR/moana"
# Use the venv interpreter directly so the run does not depend on which Python
# the mise shim currently resolves to.
exec "$SERVER_DIR/.venv/bin/python" -m pytest "$@"
