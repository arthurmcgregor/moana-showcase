"""Tests for the preview generation and caching module."""

from pathlib import Path

import pytest
from django.conf import settings

from tracks.previews import generate_all_previews, get_or_generate_preview, invalidate_previews


class FakeJourney:
    """Minimal stand-in for cache-path computation (only needs .pk)."""

    def __init__(self, pk):
        self.pk = pk


@pytest.fixture
def preview_dir(tmp_path, settings):
    """Point MEDIA_ROOT at a temp directory for cache tests."""
    settings.MEDIA_ROOT = str(tmp_path)
    cache_dir = tmp_path / "previews"
    cache_dir.mkdir()
    return cache_dir


class TestInvalidatePreviews:
    def test_removes_cached_files(self, preview_dir):
        region = preview_dir / "42_region.png"
        detail = preview_dir / "42_detail.png"
        region.write_bytes(b"fake png")
        detail.write_bytes(b"fake png")

        invalidate_previews(42)

        assert not region.exists()
        assert not detail.exists()

    def test_no_error_when_files_missing(self, preview_dir):
        invalidate_previews(999)  # no files to delete — should not raise


class TestGetOrGeneratePreview:
    def test_returns_cached_path_when_file_exists(self, preview_dir):
        cached = preview_dir / "7_region.png"
        cached.write_bytes(b"cached png data")

        path = get_or_generate_preview(FakeJourney(pk=7), "region")

        assert path == cached
        assert path.read_bytes() == b"cached png data"

    @pytest.mark.django_db
    def test_returns_none_when_no_track_data(self, preview_dir, vessel):
        from datetime import datetime, timezone

        from tracks.models import Journey

        journey = Journey.objects.create(
            vessel=vessel,
            name="Empty Journey",
            started_at=datetime(2026, 1, 1, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 2, tzinfo=timezone.utc),
        )

        path = get_or_generate_preview(journey, "region")

        assert path is None

    @pytest.mark.django_db
    def test_renders_from_manual_route_when_no_tracks(self, preview_dir, vessel):
        from datetime import datetime, timezone

        from django.contrib.gis.geos import LineString

        from tracks.models import Journey

        journey = Journey.objects.create(
            vessel=vessel,
            name="Manual Journey",
            started_at=datetime(2026, 1, 1, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 2, tzinfo=timezone.utc),
            manual_route=LineString(
                (174.78, -41.29), (174.82, -41.30), (174.85, -41.28), srid=4326
            ),
        )

        path = get_or_generate_preview(journey, "region")

        assert path is not None
        assert path.exists()
        assert len(path.read_bytes()) > 0

    def test_normalizes_invalid_zoom_to_region(self, preview_dir):
        cached = preview_dir / "7_region.png"
        cached.write_bytes(b"region png")

        path = get_or_generate_preview(FakeJourney(pk=7), "invalid_zoom")

        assert path == cached


class TestGenerateAllPreviews:
    @pytest.mark.django_db
    def test_clears_stale_cache_even_when_no_tracks(self, preview_dir, vessel):
        from datetime import datetime, timezone

        from tracks.models import Journey

        journey = Journey.objects.create(
            vessel=vessel,
            name="Empty",
            started_at=datetime(2026, 1, 1, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 2, tzinfo=timezone.utc),
        )

        # Pre-populate stale cache
        stale = preview_dir / f"{journey.pk}_region.png"
        stale.write_bytes(b"stale")

        generate_all_previews(journey)

        # Stale file should be cleared; no new file written (no tracks)
        assert not stale.exists()
