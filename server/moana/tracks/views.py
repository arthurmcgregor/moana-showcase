"""Simple views (non-viewset) for the tracks app."""

from django.http import HttpResponse
from django.views.decorators.csrf import ensure_csrf_cookie
from drf_spectacular.utils import extend_schema, inline_serializer
from rest_framework import serializers
from rest_framework.decorators import api_view, permission_classes
from rest_framework.permissions import AllowAny
from rest_framework.response import Response


def healthz(request):
    """Liveness probe. Returns 200 while the process is up.

    Deliberately a plain Django view (not DRF) so it sits outside auth,
    permission, and content-negotiation machinery: no login redirect, no
    DB or external calls. `SECURE_PROXY_SSL_HEADER` (set in settings_prod)
    means an `X-Forwarded-Proto: https` request is treated as secure, so
    there is no HTTPS redirect even if `SECURE_SSL_REDIRECT` is enabled.

    The gatus monitor probes this through Caddy, which handles `/healthz`
    ahead of `import tinyauth` — the one public path on the deployed site,
    and the reason this must stay free of anything worth reading. It used
    to probe the container port directly; that is bound to loopback now.
    """
    return HttpResponse("ok", content_type="text/plain")


@extend_schema(
    responses=inline_serializer(
        name="MeResponse",
        fields={
            "username": serializers.CharField(allow_null=True),
            "is_admin": serializers.BooleanField(),
        },
    ),
)
@ensure_csrf_cookie
@api_view(["GET"])
@permission_classes([AllowAny])
def me(request):
    """Return the current user's identity and role. Used by the Vue frontend.

    `@ensure_csrf_cookie` guarantees the csrftoken cookie is set on the
    response so subsequent unsafe requests (POST/PATCH/PUT/DELETE) from the
    SPA can include the matching X-CSRFToken header.
    """
    user = request.user
    if user.is_anonymous:
        return Response({"username": None, "is_admin": False})
    return Response({"username": user.username, "is_admin": user.is_staff})
