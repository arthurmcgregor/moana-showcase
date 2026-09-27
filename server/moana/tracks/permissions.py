"""DRF permission classes for forward-auth role-based access."""

from rest_framework.permissions import SAFE_METHODS, BasePermission


class IsAdminOrReadOnly(BasePermission):
    """Allow anyone to read. Require staff (moana_admin) for writes.

    Reads are unrestricted here, anonymous included — the deployed site is
    behind Tinyauth, so by the time a request arrives it has already been
    authenticated at the edge and this class has nothing left to add. That
    also means it is Caddy, not this, that protects reads: a request reaching
    Django another way gets them for free. Writes are the part this owns, and
    `is_staff` only becomes true via a `Remote-User` header carrying the proxy
    secret (see `tracks.middleware.RemoteUserMiddleware`).
    """

    def has_permission(self, request, view):
        if request.method in SAFE_METHODS:
            return True
        return request.user and request.user.is_staff
