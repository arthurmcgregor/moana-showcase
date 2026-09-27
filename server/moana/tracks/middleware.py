"""Middleware to authenticate users via Remote-User header from Authelia/Caddy forward auth."""

import logging

from django.conf import settings
from django.contrib.auth import get_user_model
from django.contrib.auth.models import AnonymousUser
from django.utils.crypto import constant_time_compare

logger = logging.getLogger(__name__)

User = get_user_model()

# Caddy stamps this on every proxied request from the shared secret in its env;
# `Remote-User` is worth nothing without it. Django's own port is published on the
# LAN, so without this check anyone who can reach :8000 could name themselves staff
# and skip Tinyauth entirely.
PROXY_AUTH_HEADER = "HTTP_X_PROXY_AUTH"


def _from_the_trusted_proxy(request):
    """Did this request come through the proxy that owns the shared secret?

    Fails closed: an unset secret trusts nobody, so a deployment that forgets to
    wire it goes read-only rather than wide open. `settings_prod` reads the value
    with `os.environ[...]`, so in practice the container refuses to boot first.
    """
    expected = getattr(settings, "REMOTE_USER_PROXY_SECRET", "")
    if not expected:
        logger.error(
            "REMOTE_USER_PROXY_SECRET is empty — ignoring Remote-User headers. "
            "Nobody can be staff until it matches the value Caddy sends."
        )
        return False

    return constant_time_compare(request.META.get(PROXY_AUTH_HEADER, ""), expected)


class RemoteUserMiddleware:
    """Read Remote-User / Remote-Groups headers set by Authelia via Caddy and
    populate request.user with a matching Django User.

    Django converts HTTP headers to META keys: Remote-User -> HTTP_REMOTE_USER.

    Only honoured on requests carrying the proxy's shared secret — see
    `_from_the_trusted_proxy`.
    """

    def __init__(self, get_response):
        self.get_response = get_response

    def __call__(self, request):
        username = request.META.get("HTTP_REMOTE_USER")

        if username and not _from_the_trusted_proxy(request):
            logger.warning(
                "Ignoring Remote-User '%s' from %s: no valid proxy secret. Either "
                "the request bypassed Caddy or the secrets disagree.",
                username,
                request.META.get("REMOTE_ADDR", "?"),
            )
            username = None

        if username:
            username = username.strip()
            groups_header = request.META.get("HTTP_REMOTE_GROUPS", "")
            groups = [g.strip() for g in groups_header.split(",") if g.strip()]

            user, created = User.objects.get_or_create(
                username=username,
                defaults={"is_staff": "moana_admin" in groups},
            )

            if created:
                logger.info("Created user %s (staff=%s)", username, user.is_staff)
            else:
                # Sync admin status from group membership
                should_be_staff = "moana_admin" in groups
                if user.is_staff != should_be_staff:
                    user.is_staff = should_be_staff
                    user.save(update_fields=["is_staff"])
                    logger.info("Updated user %s staff=%s", username, should_be_staff)

            request.user = user
        else:
            # No header — should not happen behind Authelia, but be safe
            if not hasattr(request, "user") or request.user is None:
                request.user = AnonymousUser()

        return self.get_response(request)


class DevRemoteUserMiddleware:
    """Stand in for Authelia locally by setting the headers it would send.

    Deployed, identity arrives as Remote-User / Remote-Groups and
    `RemoteUserMiddleware` above turns it into a Django user. Locally there is no
    proxy, so every request would be anonymous, `/api/me/` would answer
    `is_admin: false`, and the Vue app would hide every editing control.

    Faking the headers rather than calling `auth.login()` keeps identity on one
    code path: `RemoteUserMiddleware` runs the same get-or-create and
    `moana_admin` → `is_staff` sync in dev as it does deployed, so that logic is
    exercised daily instead of only in production. It also writes no session
    rows for cookie-less clients, and leaves the csrftoken cookie to
    `@ensure_csrf_cookie` on `/api/me/`, exactly as deployed.

    Locally this middleware *is* the trusted proxy, so it also stamps the shared
    secret `RemoteUserMiddleware` demands — meaning a request that already names a
    Remote-User is honoured without knowing the secret. That is how
    `curl -H 'Remote-User: crew'` acts as somebody who is not an admin.

    Three separate things keep this out of a deployment: it is only added to
    MIDDLEWARE when `DEBUG` is on, `settings_prod` removes it again, and the
    check below is per-request. `DEV_AUTO_LOGIN=0` in `server/.env` turns it off
    when you want to see the site as an anonymous visitor.
    """

    def __init__(self, get_response):
        self.get_response = get_response

    def __call__(self, request):
        if settings.DEBUG and getattr(settings, "DEV_AUTO_LOGIN", False):
            # Always: standing in for the proxy means vouching for the request,
            # whoever it claims to be.
            request.META[PROXY_AUTH_HEADER] = settings.REMOTE_USER_PROXY_SECRET

            # The identity pair moves together: a caller who names a user supplies
            # that user's groups too, rather than inheriting moana_admin.
            if "HTTP_REMOTE_USER" not in request.META:
                request.META["HTTP_REMOTE_USER"] = settings.DEV_ADMIN_USERNAME
                request.META["HTTP_REMOTE_GROUPS"] = "moana_admin"

        return self.get_response(request)
