"""RemoteUserMiddleware: identity from the proxy, and only from the proxy.

Deployed, Caddy authenticates against Tinyauth and rewrites Remote-User itself, so
a client cannot forge it *through* Caddy. Django's own port is published on the LAN
though, so the header alone is not proof of anything — X-Proxy-Auth is. These tests
are that boundary.
"""

from django.contrib.auth import get_user_model
from django.test import Client, TestCase, override_settings

User = get_user_model()

SECRET = "test-proxy-secret"


@override_settings(REMOTE_USER_PROXY_SECRET=SECRET)
class RemoteUserMiddlewareTestCase(TestCase):
    url = "/api/me/"

    def setUp(self):
        self.client = Client()

    def get(self, **headers):
        return self.client.get(self.url, headers=headers)

    def test_trusted_admin_header_creates_a_staff_user(self):
        response = self.get(
            **{"remote-user": "alice", "remote-groups": "moana_admin", "x-proxy-auth": SECRET}
        )

        self.assertEqual(response.json(), {"username": "alice", "is_admin": True})
        self.assertTrue(User.objects.get(username="alice").is_staff)

    def test_trusted_header_without_the_group_is_not_staff(self):
        response = self.get(
            **{"remote-user": "crew", "remote-groups": "family", "x-proxy-auth": SECRET}
        )

        self.assertEqual(response.json(), {"username": "crew", "is_admin": False})

    def test_losing_the_group_demotes_an_existing_user(self):
        User.objects.create_user(username="alice", is_staff=True)

        response = self.get(**{"remote-user": "alice", "x-proxy-auth": SECRET})

        self.assertEqual(response.json(), {"username": "alice", "is_admin": False})
        self.assertFalse(User.objects.get(username="alice").is_staff)

    def test_a_wrong_secret_is_ignored(self):
        response = self.get(
            **{"remote-user": "attacker", "remote-groups": "moana_admin", "x-proxy-auth": "nope"}
        )

        self.assertEqual(response.json(), {"username": None, "is_admin": False})
        self.assertFalse(User.objects.filter(username="attacker").exists())

    def test_no_secret_at_all_is_ignored(self):
        """The bypass this guards: a request straight to Django's published port."""
        response = self.get(**{"remote-user": "attacker", "remote-groups": "moana_admin"})

        self.assertEqual(response.json(), {"username": None, "is_admin": False})
        self.assertFalse(User.objects.filter(username="attacker").exists())

    @override_settings(REMOTE_USER_PROXY_SECRET="")
    def test_an_unconfigured_secret_trusts_nobody(self):
        """Fails closed: a deployment that loses the secret goes read-only, not open.
        An empty header must not match an empty setting."""
        with self.assertLogs("tracks.middleware", level="ERROR"):
            response = self.get(**{"remote-user": "alice", "x-proxy-auth": ""})

        self.assertEqual(response.json(), {"username": None, "is_admin": False})

    def test_no_headers_is_anonymous(self):
        self.assertEqual(self.get().json(), {"username": None, "is_admin": False})
