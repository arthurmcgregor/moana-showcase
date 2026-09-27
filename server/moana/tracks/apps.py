import logging

from django.apps import AppConfig
from django.conf import settings

log = logging.getLogger(__name__)


class TracksConfig(AppConfig):
    name = "tracks"

    def ready(self):
        from . import signals  # noqa: F401

        if settings.IMMICH_URL and settings.IMMICH_API_KEY:
            log.info(
                "Immich integration enabled: url=%s public_url=%s",
                settings.IMMICH_URL,
                settings.IMMICH_PUBLIC_URL or "(unset)",
            )
        else:
            missing = [
                name
                for name in ("IMMICH_URL", "IMMICH_API_KEY")
                if not getattr(settings, name)
            ]
            log.info("Immich integration disabled — missing %s", ", ".join(missing))
