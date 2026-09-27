"""Tests for the journey statistics service."""

from dataclasses import FrozenInstanceError
from datetime import datetime, timedelta, timezone

import pytest

from tracks.models import Journey, Mode, Track, TrackPoint
from tracks.statistics import Stats, journey_stats


class TestStatsToDict:
    """Stats.to_dict() produces the correct key shapes for the API."""

    def test_top_level_keys_have_total_prefix(self):
        stats = Stats(
            distance_nm=4.5,
            duration=timedelta(hours=2),
            max_speed_knots=6.0,
            avg_speed_knots=4.0,
            by_mode={},
        )
        d = stats.to_dict()
        assert d["total_distance_nm"] == 4.5
        assert d["total_duration_seconds"] == 7200
        assert d["max_speed_knots"] == 6.0
        assert d["avg_speed_knots"] == 4.0
        assert d["by_mode"] == {}

    def test_mode_entries_have_no_total_prefix(self):
        mode_stats = Stats(
            distance_nm=3.0,
            duration=timedelta(hours=1),
            max_speed_knots=5.0,
            avg_speed_knots=3.5,
            by_mode={},
        )
        stats = Stats(
            distance_nm=3.0,
            duration=timedelta(hours=1),
            max_speed_knots=5.0,
            avg_speed_knots=3.5,
            by_mode={"Sailing": mode_stats},
        )
        d = stats.to_dict()
        sailing = d["by_mode"]["Sailing"]
        assert "distance_nm" in sailing
        assert "duration_seconds" in sailing
        assert "total_distance_nm" not in sailing
        assert "total_duration_seconds" not in sailing

    def test_none_duration_serializes_as_zero(self):
        stats = Stats(
            distance_nm=0.0,
            duration=None,
            max_speed_knots=0,
            avg_speed_knots=0,
            by_mode={},
        )
        assert stats.to_dict()["total_duration_seconds"] == 0


@pytest.mark.django_db
class TestForJourney:
    """for_journey returns journey-level and per-leg stats with mode breakdowns."""

    def test_returns_stats_for_journey_and_each_leg(self, journey_with_full_data):
        journey = journey_with_full_data
        result = journey_stats.for_journey(journey)

        assert result.journey.distance_nm > 0
        assert result.journey.duration == timedelta(hours=7)

        leg = journey.legs.first()
        assert leg.id in result.legs
        assert result.legs[leg.id].duration == timedelta(hours=2)

    def test_journey_includes_per_mode_breakdowns(self, journey_with_full_data):
        journey = journey_with_full_data
        result = journey_stats.for_journey(journey)

        assert "Sailing" in result.journey.by_mode
        assert "Motoring" in result.journey.by_mode
        sailing = result.journey.by_mode["Sailing"]
        assert sailing.distance_nm > 0
        assert sailing.max_speed_knots > 0


@pytest.mark.django_db
class TestForLeg:
    """for_leg returns the same stats as the corresponding leg in for_journey."""

    def test_matches_journey_leg_stats(self, journey_with_full_data):
        journey = journey_with_full_data
        leg = journey.legs.first()

        from_journey = journey_stats.for_journey(journey).legs[leg.id]
        from_leg = journey_stats.for_leg(leg)

        assert from_leg.distance_nm == from_journey.distance_nm
        assert from_leg.duration == from_journey.duration
        assert from_leg.max_speed_knots == from_journey.max_speed_knots


@pytest.mark.django_db
class TestForTimeRange:
    """for_time_range with no matching data returns zero stats."""

    def test_empty_range_returns_zeros(self, vessel):
        result = journey_stats.for_time_range(
            vessel_id=vessel.id,
            start=datetime(2099, 1, 1, tzinfo=timezone.utc),
            end=datetime(2099, 1, 2, tzinfo=timezone.utc),
        )
        assert result.distance_nm == 0.0
        assert result.max_speed_knots == 0
        assert result.avg_speed_knots == 0
        assert result.by_mode == {}
        assert result.duration == timedelta(days=1)


@pytest.mark.django_db
class TestOnWaterDistanceNm:
    """on_water_distance_nm sums on-water modes or falls back to total."""

    def test_sums_on_water_modes_only(self, journey_with_full_data):
        """MOTOR + SAIL are on-water; ANCHOR is excluded."""
        journey = journey_with_full_data
        result = journey_stats.on_water_distance_nm(journey)
        assert result > 0
        # Should be <= total distance (ANCHOR portion excluded)
        full = journey_stats.for_journey(journey)
        assert result <= full.journey.distance_nm + 0.001

    def test_no_modes_falls_back_to_total(self, journey_with_full_data):
        """Journey with no modes returns total distance as fallback."""
        journey = journey_with_full_data
        Mode.objects.filter(vessel=journey.vessel).delete()
        result = journey_stats.on_water_distance_nm(journey)
        assert result > 0

    def test_only_non_water_modes_falls_back(self, vessel):
        """Journey with only ANCHOR mode falls back to total distance."""
        from django.contrib.gis.geos import Point

        journey = Journey.objects.create(
            vessel=vessel,
            name="At anchor",
            started_at=datetime(2026, 2, 1, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 2, 1, 12, 0, tzinfo=timezone.utc),
        )
        Mode.objects.create(
            vessel=vessel,
            mode="ANCHOR",
            started_at=datetime(2026, 2, 1, 9, 0, tzinfo=timezone.utc),
        )
        track = Track.objects.create(
            vessel=vessel,
            started_at=datetime(2026, 2, 1, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 2, 1, 12, 0, tzinfo=timezone.utc),
        )
        TrackPoint.objects.create(
            track=track,
            location=Point(172.7, -43.6, srid=4326),
            speed=0,
            timestamp=datetime(2026, 2, 1, 9, 0, tzinfo=timezone.utc),
        )
        TrackPoint.objects.create(
            track=track,
            location=Point(172.71, -43.61, srid=4326),
            speed=0,
            timestamp=datetime(2026, 2, 1, 12, 0, tzinfo=timezone.utc),
        )
        result = journey_stats.on_water_distance_nm(journey)
        assert result > 0

    def test_empty_journey_returns_zero(self, vessel):
        """Journey with no track points returns 0."""
        journey = Journey.objects.create(
            vessel=vessel,
            name="Empty",
            started_at=datetime(2099, 1, 1, tzinfo=timezone.utc),
            ended_at=datetime(2099, 1, 2, tzinfo=timezone.utc),
        )
        result = journey_stats.on_water_distance_nm(journey)
        assert result == 0.0


@pytest.mark.django_db
class TestEstimatedDistanceNm:
    """estimated_distance_nm computes PostGIS length of manual_route."""

    def test_null_without_manual_route(self, vessel):
        journey = Journey.objects.create(
            vessel=vessel,
            name="No route",
            started_at=datetime(2026, 3, 1, tzinfo=timezone.utc),
        )
        assert journey_stats.estimated_distance_nm(journey) is None

    def test_positive_with_manual_route(self, vessel):
        from django.contrib.gis.geos import LineString

        # Roughly 1 nm of longitude at -41° latitude (~1.33 nm actually).
        journey = Journey.objects.create(
            vessel=vessel,
            name="Manual",
            started_at=datetime(2026, 3, 1, tzinfo=timezone.utc),
            manual_route=LineString(
                (174.78, -41.29), (174.80, -41.29), srid=4326
            ),
        )
        result = journey_stats.estimated_distance_nm(journey)
        assert result is not None
        assert 0.5 < result < 2.0


@pytest.mark.django_db
class TestRepeatedMode:
    """Regression: a mode that appears multiple times in a journey must
    accumulate distance/duration across all occurrences, not be overwritten
    by the last one (see statistics._postgis_mode_stats)."""

    def test_repeated_sail_sums_both_segments(self, vessel):
        from django.contrib.gis.geos import Point

        journey = Journey.objects.create(
            vessel=vessel,
            name="Sail-Motor-Sail",
            started_at=datetime(2026, 4, 1, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 4, 1, 12, 0, tzinfo=timezone.utc),
        )
        # SAIL → MOTOR → SAIL: "Sailing" mode appears twice with a Motoring
        # segment between. Bug was that the second SAIL row overwrote the first
        # in the by_mode dict, dropping the first segment's distance.
        Mode.objects.create(
            vessel=vessel,
            mode="SAIL",
            started_at=datetime(2026, 4, 1, 9, 0, tzinfo=timezone.utc),
        )
        Mode.objects.create(
            vessel=vessel,
            mode="MOTOR",
            started_at=datetime(2026, 4, 1, 10, 0, tzinfo=timezone.utc),
        )
        Mode.objects.create(
            vessel=vessel,
            mode="SAIL",
            started_at=datetime(2026, 4, 1, 11, 0, tzinfo=timezone.utc),
        )
        track = Track.objects.create(
            vessel=vessel,
            name="t",
            started_at=datetime(2026, 4, 1, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 4, 1, 12, 0, tzinfo=timezone.utc),
        )
        # ~1 nm of longitude per segment at lat -41°, separate locations so
        # neither segment's linestring is degenerate.
        TrackPoint.objects.create(
            track=track, location=Point(174.00, -41.00, srid=4326),
            speed=5.0, timestamp=datetime(2026, 4, 1, 9, 0, tzinfo=timezone.utc),
        )
        TrackPoint.objects.create(
            track=track, location=Point(174.02, -41.00, srid=4326),
            speed=5.0, timestamp=datetime(2026, 4, 1, 9, 30, tzinfo=timezone.utc),
        )  # first SAIL segment
        TrackPoint.objects.create(
            track=track, location=Point(174.04, -41.00, srid=4326),
            speed=6.0, timestamp=datetime(2026, 4, 1, 10, 30, tzinfo=timezone.utc),
        )  # MOTOR segment
        TrackPoint.objects.create(
            track=track, location=Point(174.06, -41.00, srid=4326),
            speed=4.0, timestamp=datetime(2026, 4, 1, 11, 30, tzinfo=timezone.utc),
        )  # second SAIL segment

        result = journey_stats.for_journey(journey)
        sailing = result.journey.by_mode["Sailing"]
        motoring = result.journey.by_mode["Motoring"]

        # Both SAIL segments contribute: combined duration is 2h (9–10 + 11–12),
        # not 1h (only second segment).
        assert sailing.duration == timedelta(hours=2)
        # And sailing distance must exceed any single segment's distance —
        # specifically, it should exceed the motoring segment's distance (which
        # was previously the "winner" when only the last sail row was kept).
        assert sailing.distance_nm > motoring.distance_nm


class TestStatsImmutability:
    """Stats dataclasses are frozen and reject mutation."""

    def test_stats_rejects_attribute_assignment(self):
        stats = Stats(
            distance_nm=1.0,
            duration=timedelta(hours=1),
            max_speed_knots=5.0,
            avg_speed_knots=3.0,
            by_mode={},
        )
        with pytest.raises(FrozenInstanceError):
            stats.distance_nm = 99.0
