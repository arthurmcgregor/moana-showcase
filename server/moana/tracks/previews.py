"""
Journey and track preview generation and caching.

Renders static map PNG thumbnails using matplotlib + contextily. Previews
are cached to disk at MEDIA_ROOT/previews/.
"""

from __future__ import annotations

import io
import logging
from pathlib import Path

from django.conf import settings
from django.db import models

logger = logging.getLogger(__name__)

ZOOM_LEVELS = ("region", "detail")


def _cache_dir() -> Path:
    return Path(settings.MEDIA_ROOT) / "previews"


def _cache_path(journey_id: int, zoom: str) -> Path:
    return _cache_dir() / f"{journey_id}_{zoom}.png"


def _track_cache_path(track_id: int, zoom: str) -> Path:
    return _cache_dir() / f"track_{track_id}_{zoom}.png"


def _render_coords(coords: list[tuple[float, float]], zoom: str) -> bytes:
    import contextily as cx
    import geopandas as gpd
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from shapely.geometry import LineString
    from shapely.geometry import Point as ShapelyPoint

    if len(coords) >= 2:
        geometry = [LineString(coords)]
    else:
        geometry = [ShapelyPoint(coords[0])]

    gdf = gpd.GeoDataFrame(geometry=geometry, crs="EPSG:4326")
    gdf = gdf.to_crs(epsg=3857)

    bounds = gdf.total_bounds
    dx = bounds[2] - bounds[0]
    dy = bounds[3] - bounds[1]

    if zoom == "detail":
        pad_x = dx * 0.3
        pad_y = dy * 0.3
    else:
        pad_x = dx * 0.5
        pad_y = dy * 0.5
        min_extent = 300_000
        if dx + 2 * pad_x < min_extent:
            pad_x = (min_extent - dx) / 2
        if dy + 2 * pad_y < min_extent:
            pad_y = (min_extent - dy) / 2

    extent_x = dx + 2 * pad_x
    extent_y = dy + 2 * pad_y
    if extent_x > extent_y:
        pad_y += (extent_x - extent_y) / 2
    else:
        pad_x += (extent_y - extent_x) / 2

    fig = plt.figure(figsize=(6, 6), dpi=200)
    ax = fig.add_axes([0, 0, 1, 1])
    gdf.plot(ax=ax, linewidth=2, color="#e74c3c", zorder=2)

    ax.set_xlim(bounds[0] - pad_x, bounds[2] + pad_x)
    ax.set_ylim(bounds[1] - pad_y, bounds[3] + pad_y)
    ax.set_aspect("equal")

    cx.add_basemap(ax, source=cx.providers.CartoDB.Voyager, attribution=False)
    ax.set_axis_off()

    buf = io.BytesIO()
    fig.savefig(buf, format="png", pad_inches=0, dpi=200)
    plt.close(fig)

    return buf.getvalue()


def render_preview(journey, zoom: str = "region") -> bytes | None:
    """Render a journey preview image. Returns PNG bytes or None if no track data."""
    from .models import TrackPoint

    points_qs = TrackPoint.objects.filter(
        timestamp__gte=journey.started_at
    ).order_by("timestamp")
    if journey.ended_at:
        points_qs = points_qs.filter(timestamp__lte=journey.ended_at)

    tracks = journey.get_tracks().prefetch_related(
        models.Prefetch("points", queryset=points_qs, to_attr="journey_points")
    )
    coords = []
    for track in tracks:
        coords.extend([(p.location.x, p.location.y) for p in track.journey_points])

    if not coords and journey.manual_route is not None:
        coords = [(x, y) for x, y in journey.manual_route.coords]

    if not coords:
        return None

    return _render_coords(coords, zoom)


def render_track_preview(track, zoom: str = "region") -> bytes | None:
    """Render a single track's preview image. Returns PNG bytes or None if empty."""
    coords = [
        (p.location.x, p.location.y)
        for p in track.points.all().order_by("timestamp")
    ]
    if not coords:
        return None
    return _render_coords(coords, zoom)


def get_or_generate_preview(journey, zoom: str = "region") -> Path | None:
    """Return path to cached journey preview PNG, generating it if missing."""
    if zoom not in ZOOM_LEVELS:
        zoom = "region"

    cache_path = _cache_path(journey.pk, zoom)

    if cache_path.exists():
        return cache_path

    image_bytes = render_preview(journey, zoom)
    if image_bytes is None:
        return None

    cache_path.parent.mkdir(parents=True, exist_ok=True)
    cache_path.write_bytes(image_bytes)
    logger.info("Generated preview %s for journey %s", zoom, journey.pk)
    return cache_path


def get_or_generate_track_preview(track, zoom: str = "region") -> Path | None:
    """Return path to cached track preview PNG, generating it if missing."""
    if zoom not in ZOOM_LEVELS:
        zoom = "region"

    cache_path = _track_cache_path(track.pk, zoom)

    if cache_path.exists():
        return cache_path

    image_bytes = render_track_preview(track, zoom)
    if image_bytes is None:
        return None

    cache_path.parent.mkdir(parents=True, exist_ok=True)
    cache_path.write_bytes(image_bytes)
    logger.info("Generated preview %s for track %s", zoom, track.pk)
    return cache_path


def invalidate_previews(journey_id: int) -> None:
    """Delete all cached journey preview images."""
    for zoom in ZOOM_LEVELS:
        _cache_path(journey_id, zoom).unlink(missing_ok=True)


def invalidate_track_previews(track_id: int) -> None:
    """Delete all cached track preview images."""
    for zoom in ZOOM_LEVELS:
        _track_cache_path(track_id, zoom).unlink(missing_ok=True)


def generate_all_previews(journey) -> None:
    """Generate and cache journey previews at all zoom levels."""
    invalidate_previews(journey.pk)
    for zoom in ZOOM_LEVELS:
        get_or_generate_preview(journey, zoom)
