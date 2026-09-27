"""Journey-photo logic: time-window computation and Immich asset serialization."""

from __future__ import annotations

from datetime import timedelta
from typing import Optional

from django.conf import settings
from django.urls import reverse
from django.utils import timezone

WINDOW_PADDING = timedelta(hours=2)


def journey_photo_window(journey) -> Optional[tuple]:
    """Return the UTC (start, end) to query Immich with, padded ±2h.

    Ongoing tracks or ongoing journeys fall back to `now()`.
    """
    tracks = list(journey.get_tracks())
    if tracks:
        raw_start = min(t.started_at for t in tracks)
        ends = [t.ended_at for t in tracks if t.ended_at is not None]
        raw_end = max(ends) if ends else (journey.ended_at or timezone.now())
    else:
        if not journey.started_at:
            return None
        raw_start = journey.started_at
        raw_end = journey.ended_at or timezone.now()
    return raw_start - WINDOW_PADDING, raw_end + WINDOW_PADDING


def serialize_immich_asset(asset: dict, journey_id: int, request) -> dict:
    asset_id = asset["id"]
    thumb_path = reverse(
        "tracks:journey-photo-thumb",
        kwargs={"pk": journey_id, "asset_id": asset_id},
    )
    preview_path = reverse(
        "tracks:journey-photo-preview",
        kwargs={"pk": journey_id, "asset_id": asset_id},
    )
    immich_public_url = (settings.IMMICH_PUBLIC_URL or "").rstrip("/")
    out = {
        "id": asset_id,
        "takenAt": asset.get("fileCreatedAt") or asset.get("exifInfo", {}).get("dateTimeOriginal"),
        "type": asset.get("type", "IMAGE"),
        "thumbUrl": request.build_absolute_uri(thumb_path),
        "previewUrl": request.build_absolute_uri(preview_path),
        "immichUrl": f"{immich_public_url}/photos/{asset_id}" if immich_public_url else None,
    }
    exif = asset.get("exifInfo") or {}
    # Omit lat/lng entirely (not null) when GPS EXIF is absent, so the
    # frontend can use `"lat" in photo` as a single truthy check.
    lat = exif.get("latitude")
    lng = exif.get("longitude")
    if lat is not None and lng is not None:
        out["lat"] = lat
        out["lng"] = lng
    return out
