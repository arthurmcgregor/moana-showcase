"""The local-dev admin account and the shim that hands Django its identity.

Both only exist because dev has no Authelia in front of Django. The point of
these tests is the guard: neither may do anything with DEBUG off, and the shim
must not survive into `settings_prod`.
"""

import importlib
import os
import re
from pathlib import Path
from unittest import mock

from django.conf import settings
from django.contrib.auth import get_user_model
from django.core.management import call_command
from django.core.management.base import CommandError
from django.test import Client, SimpleTestCase, TestCase, override_settings

User = get_user_model()

SHIM = "tracks.middleware.DevRemoteUserMiddleware"
REAL = "tracks.middleware.RemoteUserMiddleware"

# settings.py only inserts the shim when DEBUG and DEV_AUTO_LOGIN are on at import
# time, and the test run may have either off. Pin the chain here so the tests
# exercise the middleware itself rather than that import-time decision.
MIDDLEWARE_WITH_SHIM = [m for m in settings.MIDDLEWARE if m != SHIM]
MIDDLEWARE_WITH_SHIM.insert(MIDDLEWARE_WITH_SHIM.index(REAL), SHIM)


@override_settings(DEBUG=True, DEV_ADMIN_USERNAME="admin", DEV_ADMIN_PASSWORD="admin")
class DevAdminCommandTestCase(TestCase):
    def test_creates_a_superuser(self):
        call_command("devadmin")

        user = User.objects.get(username="admin")
        self.assertTrue(user.is_staff)
        self.assertTrue(user.is_superuser)
        self.assertTrue(user.check_password("admin"))

    def test_promotes_and_resets_an_existing_user(self):
        User.objects.create_user(username="admin", password="forgotten")

        call_command("devadmin")

        user = User.objects.get(username="admin")
        self.assertTrue(user.is_staff)
        self.assertTrue(user.check_password("admin"))
        self.assertEqual(User.objects.filter(username="admin").count(), 1)

    def test_honours_explicit_arguments(self):
        call_command("devadmin", "--username", "skipper", "--password", "s3cret")

        user = User.objects.get(username="skipper")
        self.assertTrue(user.is_staff)
        self.assertTrue(user.check_password("s3cret"))

    @override_settings(DEBUG=False)
    def test_refuses_with_debug_off(self):
        with self.assertRaises(CommandError):
            call_command("devadmin")
        self.assertFalse(User.objects.filter(username="admin").exists())


@override_settings(
    DEBUG=True,
    DEV_AUTO_LOGIN=True,
    DEV_ADMIN_USERNAME="admin",
    DEV_ADMIN_PASSWORD="admin",
    MIDDLEWARE=MIDDLEWARE_WITH_SHIM,
)
class DevRemoteUserTestCase(TestCase):
    """`/api/me/` is the endpoint the Vue app asks for its role, so it is the
    honest end-to-end check that the faked header reaches DRF as a staff user."""

    url = "/api/me/"

    def setUp(self):
        # A fresh Client per test: the middleware chain is built on first request.
        self.client = Client()

    def test_anonymous_request_becomes_the_dev_admin(self):
        response = self.client.get(self.url)

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json(), {"username": "admin", "is_admin": True})

    def test_creates_the_account_when_it_does_not_exist(self):
        """No `devadmin` run first — RemoteUserMiddleware makes the user, the same
        get-or-create that runs behind Authelia."""
        self.assertFalse(User.objects.filter(username="admin").exists())

        self.client.get(self.url)

        self.assertTrue(User.objects.get(username="admin").is_staff)

    def test_an_explicit_header_wins_over_the_shim(self):
        """Only the identity pair is defaulted, so curl can act as somebody else —
        and somebody outside moana_admin is not staff. No X-Proxy-Auth needed: the
        shim vouches for the request, as the proxy it stands in for would."""
        response = self.client.get(self.url, headers={"remote-user": "crew"})

        self.assertEqual(response.json(), {"username": "crew", "is_admin": False})

    @override_settings(DEV_AUTO_LOGIN=False)
    def test_stays_anonymous_when_switched_off(self):
        call_command("devadmin")

        response = self.client.get(self.url)

        self.assertEqual(response.json(), {"username": None, "is_admin": False})

    @override_settings(DEBUG=False)
    def test_stays_anonymous_with_debug_off(self):
        with override_settings(DEBUG=True):
            call_command("devadmin")

        response = self.client.get(self.url)

        self.assertEqual(response.json(), {"username": None, "is_admin": False})


class ProdSettingsTestCase(SimpleTestCase):
    """settings.py builds MIDDLEWARE while DEBUG is still True, so the shim really
    is in the list settings_prod inherits — this strip is load-bearing, not a
    formality."""

    def test_prod_drops_the_shim_and_keeps_the_real_middleware(self):
        with mock.patch.dict(
            os.environ,
            {"DJANGO_SECRET_KEY": "test-only", "REMOTE_USER_PROXY_SECRET": "test-only"},
        ):
            settings_prod = importlib.reload(importlib.import_module("moana.settings_prod"))

        self.assertNotIn(SHIM, settings_prod.MIDDLEWARE)
        self.assertIn(REAL, settings_prod.MIDDLEWARE)
        self.assertFalse(settings_prod.DEBUG)
        self.assertFalse(settings_prod.DEV_AUTO_LOGIN)

    def test_every_required_secret_has_a_build_time_placeholder(self):
        """`docker build` runs collectstatic under settings_prod, where an
        `os.environ[...]` with no real value to give raises and fails the build.
        CI never builds the image, so nothing else catches this — it costs a
        round trip through the registry to find out."""
        settings_src = (Path(settings.BASE_DIR) / "moana" / "settings_prod.py").read_text()
        dockerfile = (Path(settings.BASE_DIR).parent / "Dockerfile").read_text()

        required = set(re.findall(r'os\.environ\["([A-Z0-9_]+)"\]', settings_src))
        self.assertTrue(required, "expected settings_prod to require something")

        for name in sorted(required):
            self.assertIn(
                f"{name}=", dockerfile, f"{name} has no placeholder in the Dockerfile"
            )

    def test_importing_prod_settings_does_not_mutate_the_live_ones(self):
        """`from .settings import *` binds the base module's own list and dict, so
        settings_prod must rebind rather than edit them in place."""
        middleware_before = list(settings.MIDDLEWARE)
        permissions_before = settings.REST_FRAMEWORK.get("DEFAULT_PERMISSION_CLASSES")

        with mock.patch.dict(
            os.environ,
            {"DJANGO_SECRET_KEY": "test-only", "REMOTE_USER_PROXY_SECRET": "test-only"},
        ):
            importlib.reload(importlib.import_module("moana.settings_prod"))

        self.assertEqual(list(settings.MIDDLEWARE), middleware_before)
        self.assertEqual(
            settings.REST_FRAMEWORK.get("DEFAULT_PERMISSION_CLASSES"), permissions_before
        )
