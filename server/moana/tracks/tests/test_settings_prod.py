"""`moana.settings_prod` has to at least import — and until now nothing could.

whitenoise and gunicorn lived only in the image's dependency list
(`moana/requirements.txt`), so the venv and CI installed a set that could not
load the production settings module at all: the middleware and staticfiles
backend it names were simply absent. The only thing on earth that exercised
this file was `docker build`, which is how the missing collectstatic secret in
`f958393` reached `main` (issue #156).

Lives with the tracks tests because that is where the other project-level
wiring test is (`test_dev_admin.py`).
"""

import importlib

import pytest

REQUIRED_ENV = {
    "DJANGO_SECRET_KEY": "test-secret",
    "REMOTE_USER_PROXY_SECRET": "test-proxy-secret",
    "DJANGO_ALLOWED_HOSTS": "moana.example,www.moana.example",
}


def _load(monkeypatch, env):
    """Import moana.settings_prod under exactly `env`, freshly evaluated."""
    for key in REQUIRED_ENV:
        monkeypatch.delenv(key, raising=False)
    for key, value in env.items():
        monkeypatch.setenv(key, value)
    return importlib.reload(importlib.import_module("moana.settings_prod"))


class TestSettingsProd:
    def test_imports_with_the_required_secrets(self, monkeypatch):
        prod = _load(monkeypatch, REQUIRED_ENV)

        assert prod.DEBUG is False
        assert prod.SECRET_KEY == "test-secret"
        assert prod.ALLOWED_HOSTS == ["moana.example", "www.moana.example"]

    def test_the_production_only_packages_are_installed(self, monkeypatch):
        # The point of the exercise: these are what the two dependency lists
        # disagreed about, and settings_prod names both by string, so an
        # absent one only surfaces when Django resolves it.
        prod = _load(monkeypatch, REQUIRED_ENV)

        assert (
            prod.STORAGES["staticfiles"]["BACKEND"]
            == "whitenoise.storage.CompressedManifestStaticFilesStorage"
        )
        assert "whitenoise.middleware.WhiteNoiseMiddleware" in prod.MIDDLEWARE
        importlib.import_module("whitenoise.storage")
        # entrypoint.sh execs this; nothing else in the suite would notice it
        # going missing.
        importlib.import_module("gunicorn")

    @pytest.mark.parametrize(
        "missing", ["DJANGO_SECRET_KEY", "REMOTE_USER_PROXY_SECRET"]
    )
    def test_refuses_to_load_without_a_secret(self, monkeypatch, missing):
        # No defaults on purpose: inheriting the base settings' dev
        # placeholder for REMOTE_USER_PROXY_SECRET would let anyone reaching
        # the published port forge `Remote-User` and become staff.
        env = {k: v for k, v in REQUIRED_ENV.items() if k != missing}

        with pytest.raises(KeyError, match=missing):
            _load(monkeypatch, env)
