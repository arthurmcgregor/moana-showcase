#!/bin/sh
set -e

POSTGRES_HOST="${POSTGRES_HOST:-db}"
POSTGRES_PORT="${POSTGRES_PORT:-5432}"

echo "Waiting for database at ${POSTGRES_HOST}:${POSTGRES_PORT}..."

while ! python -c "
import socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
try:
    s.connect(('${POSTGRES_HOST}', ${POSTGRES_PORT}))
    s.close()
    exit(0)
except Exception:
    exit(1)
" 2>/dev/null; do
    sleep 1
done

echo "Database is ready."

echo "Running migrations..."
python manage.py migrate --noinput

GUNICORN_WORKERS="${GUNICORN_WORKERS:-3}"
GUNICORN_TIMEOUT="${GUNICORN_TIMEOUT:-120}"

echo "Starting gunicorn with ${GUNICORN_WORKERS} workers..."
exec gunicorn moana.wsgi:application \
    --bind 0.0.0.0:8000 \
    --workers "${GUNICORN_WORKERS}" \
    --timeout "${GUNICORN_TIMEOUT}" \
    --access-logfile - \
    --error-logfile -
