# ---- Builder stage ----
FROM python:3.15-bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gdal-bin \
    libgdal-dev \
    libgeos-dev \
    libproj-dev \
    libpq-dev \
    && rm -rf /var/lib/apt/lists/*

RUN python -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"

# The same pinned file the venv and CI install, so the image and the tested
# environment are provably the same dependency set (issue #156).
COPY requirements.txt /tmp/requirements.txt
RUN pip install --no-cache-dir -r /tmp/requirements.txt

# ---- Runtime stage ----
FROM python:3.15-slim-bookworm

RUN apt-get update && apt-get install -y --no-install-recommends \
    gdal-bin \
    libgeos-c1v5 \
    libproj25 \
    libpq5 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /opt/venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"

WORKDIR /app

COPY moana/ /app/

ENV DJANGO_SETTINGS_MODULE=moana.settings_prod

# Collect static files at build time. settings_prod demands both secrets outright
# — no defaults, so a deployment cannot inherit a dev placeholder — and neither is
# available at build time. Placeholders satisfy the import; the real values arrive
# through env_file at run time, and the build never serves a request with these.
RUN DJANGO_SECRET_KEY=build-placeholder \
    REMOTE_USER_PROXY_SECRET=build-placeholder \
    DJANGO_ALLOWED_HOSTS=localhost \
    python manage.py collectstatic --noinput

COPY entrypoint.sh /app/entrypoint.sh

RUN useradd --create-home appuser \
    && mkdir -p /app/media \
    && chown -R appuser:appuser /app/media /app/entrypoint.sh

USER appuser

EXPOSE 8000

ENTRYPOINT ["/app/entrypoint.sh"]
