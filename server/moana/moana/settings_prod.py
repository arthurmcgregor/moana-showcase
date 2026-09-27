"""
Production settings for moana project.

Imports all base settings and overrides for production deployment.
All secrets and host-specific values come from environment variables.
"""

import os

from .settings import *  # noqa: F401, F403

# --- Core ---
DEBUG = False

SECRET_KEY = os.environ["DJANGO_SECRET_KEY"]

# Required, not defaulted: the base settings ship a dev placeholder, and inheriting
# that deployed would let anyone who reaches Django's published port forge
# `Remote-User` and become staff. Must match the value Caddy sends as
# `X-Proxy-Auth` on the moana site — missing here, the container refuses to boot,
# which beats a site that quietly trusts nobody (or worse, everybody).
REMOTE_USER_PROXY_SECRET = os.environ["REMOTE_USER_PROXY_SECRET"]

ALLOWED_HOSTS = [
    h.strip() for h in os.environ.get("DJANGO_ALLOWED_HOSTS", "").split(",") if h.strip()
]

# --- Database ---
DATABASES = {
    "default": {
        "ENGINE": "django.contrib.gis.db.backends.postgis",
        "NAME": os.environ.get("POSTGRES_DB", "moana"),
        "USER": os.environ.get("POSTGRES_USER", "moana_user"),
        "PASSWORD": os.environ.get("POSTGRES_PASSWORD", ""),
        "HOST": os.environ.get("POSTGRES_HOST", "db"),
        "PORT": os.environ.get("POSTGRES_PORT", "5432"),
        "CONN_MAX_AGE": 600,
    }
}

# --- Static files ---
STATIC_ROOT = BASE_DIR / "staticfiles"

STORAGES = {
    "staticfiles": {
        "BACKEND": "whitenoise.storage.CompressedManifestStaticFilesStorage",
    },
}

# --- Middleware ---
# The comprehension is also the copy: `import *` binds the base module's own list,
# so insert() below would otherwise mutate it in place.
#
# RemoteUserMiddleware itself stays where base settings put it — the strip is only
# the dev shim that feeds it a fake header. settings.py builds MIDDLEWARE while
# DEBUG is still True, so the shim is in the list by the time this file runs. It is
# also inert with DEBUG off, but identity middleware is not something to leave to a
# single guard. `tracks/tests/test_dev_admin.py` asserts this strip.
MIDDLEWARE = [  # noqa: F405
    m for m in MIDDLEWARE if m != "tracks.middleware.DevRemoteUserMiddleware"  # noqa: F405
]

# WhiteNoise right after SecurityMiddleware
MIDDLEWARE.insert(
    MIDDLEWARE.index("django.middleware.security.SecurityMiddleware") + 1,
    "whitenoise.middleware.WhiteNoiseMiddleware",
)

DEV_AUTO_LOGIN = False

# --- CORS / CSRF ---
CORS_ALLOW_ALL_ORIGINS = False

CORS_ALLOWED_ORIGINS = [
    o.strip()
    for o in os.environ.get("CORS_ALLOWED_ORIGINS", "").split(",")
    if o.strip()
]

CSRF_TRUSTED_ORIGINS = [
    o.strip()
    for o in os.environ.get("CSRF_TRUSTED_ORIGINS", "").split(",")
    if o.strip()
]

# --- DRF ---
# Rebound, not mutated in place: the dict came from `import *`, so assigning into
# it would edit the base settings module's copy too.
REST_FRAMEWORK = {  # noqa: F405
    **REST_FRAMEWORK,  # noqa: F405
    "DEFAULT_PERMISSION_CLASSES": ["tracks.permissions.IsAdminOrReadOnly"],
}

# --- Security headers ---
SECURE_PROXY_SSL_HEADER = ("HTTP_X_FORWARDED_PROTO", "https")
SESSION_COOKIE_SECURE = True
CSRF_COOKIE_SECURE = True

# --- Logging ---
LOGGING = {
    "version": 1,
    "disable_existing_loggers": False,
    "formatters": {
        "verbose": {
            "format": "[{levelname}] {asctime} {name} {message}",
            "style": "{",
        },
    },
    "handlers": {
        "console": {
            "class": "logging.StreamHandler",
            "formatter": "verbose",
        },
    },
    "loggers": {
        "tracks": {
            "handlers": ["console"],
            "level": "INFO",
            "propagate": False,
        },
        "django": {
            "handlers": ["console"],
            "level": "WARNING",
            "propagate": False,
        },
    },
}
