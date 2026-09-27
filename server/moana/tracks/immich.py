"""Thin wrapper around the Immich HTTP API."""

from __future__ import annotations

import logging
from datetime import datetime, timezone as dt_timezone
from typing import Optional

import requests
from django.conf import settings

log = logging.getLogger(__name__)
TIMEOUT_SECONDS = 5


class ImmichError(Exception):
    pass


class ImmichClient:
    def __init__(self, base_url: str, api_key: str):
        self._base_url = base_url.rstrip("/")
        self._session = requests.Session()
        self._session.headers.update({
            "x-api-key": api_key,
            "Accept": "application/json",
        })

    @classmethod
    def from_settings(cls) -> Optional["ImmichClient"]:
        if not settings.IMMICH_URL or not settings.IMMICH_API_KEY:
            return None
        key = (settings.IMMICH_URL, settings.IMMICH_API_KEY)
        cached = getattr(cls, "_cache", None)
        if cached and cached[0] == key:
            return cached[1]
        client = cls(settings.IMMICH_URL, settings.IMMICH_API_KEY)
        cls._cache = (key, client)
        return client

    def search_assets(self, taken_after: datetime, taken_before: datetime) -> list[dict]:
        """Return assets whose `fileCreatedAt` falls in the window, paging through all results."""
        url = f"{self._base_url}/api/search/metadata"
        assets: list[dict] = []
        page = 1
        while True:
            payload = {
                "takenAfter": _to_iso(taken_after),
                "takenBefore": _to_iso(taken_before),
                "withExif": True,
                "page": page,
            }
            try:
                response = self._session.post(url, json=payload, timeout=TIMEOUT_SECONDS)
            except requests.RequestException as exc:
                raise ImmichError(f"Immich search failed: {exc}") from exc
            if not response.ok:
                raise ImmichError(
                    f"Immich search returned {response.status_code}: {response.text[:200]}"
                )
            block = response.json().get("assets", {})
            assets.extend(block.get("items", []))
            next_page = block.get("nextPage")
            if not next_page:
                break
            page = int(next_page)
        return assets

    def fetch_thumbnail(self, asset_id: str, size: str = "thumbnail") -> tuple[bytes, str]:
        """`size` is "thumbnail" (small) or "preview" (~1440px)."""
        url = f"{self._base_url}/api/assets/{asset_id}/thumbnail"
        try:
            response = self._session.get(url, params={"size": size}, timeout=TIMEOUT_SECONDS)
        except requests.RequestException as exc:
            raise ImmichError(f"Immich thumbnail fetch failed: {exc}") from exc
        if not response.ok:
            raise ImmichError(
                f"Immich thumbnail returned {response.status_code} for asset {asset_id}"
            )
        content_type = response.headers.get("Content-Type", "application/octet-stream")
        return response.content, content_type


def _to_iso(dt: datetime) -> str:
    return dt.astimezone(dt_timezone.utc).isoformat().replace("+00:00", "Z")
