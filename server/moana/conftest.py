import re
from datetime import datetime, timezone

import pytest
from django.contrib.gis.geos import Point
from rest_framework.test import APIClient
from syrupy.extensions.json import JSONSnapshotExtension

from tracks.models import (
    Aboard,
    Annotation,
    Journey,
    Leg,
    Mode,
    Person,
    Track,
    TrackPoint,
    Vessel,
)

# Fields whose values are auto-generated and change between test runs
AUTO_TIMESTAMP_FIELDS = {"created_at", "imported_at"}

# Fields that contain database PKs (auto-increment)
PK_FIELDS = {"id"}

# Fields that are FK references (contain integer PKs of related objects)
FK_FIELDS = {
    "vessel",
    "person",
    "track",
    "journey",
    "leg",
    "station",
    "track_id",
    "vessel_id",
    "journey_id",
    "person_id",
    "leg_id",
    "station_id",
}

# Float fields that may have cross-platform precision differences
FLOAT_ROUND_FIELDS = {
    "total_distance_nm",
    "avg_speed_knots",
    "max_speed_knots",
    "distance_nm",
}


def sanitize_api_response(data):
    """Replace volatile fields in API response data with deterministic values.

    Auto-increment PKs and FKs are replaced with a fixed placeholder.
    Auto-generated timestamps get replaced with a fixed value. Float
    statistics get rounded for cross-platform stability.
    """
    def walk(obj):
        if isinstance(obj, dict):
            result = {}
            for k, v in obj.items():
                if k in AUTO_TIMESTAMP_FIELDS and v is not None:
                    result[k] = "2026-01-01T00:00:00Z"
                elif k in PK_FIELDS and isinstance(v, int):
                    result[k] = "<pk>"
                elif k in FK_FIELDS and isinstance(v, int):
                    result[k] = "<fk>"
                elif k in ("next", "previous") and isinstance(v, str):
                    result[k] = re.sub(r"^https?://[^/]+", "", v)
                elif k in FLOAT_ROUND_FIELDS and isinstance(v, (int, float)):
                    result[k] = round(v, 2)
                else:
                    result[k] = walk(v)
            return result
        elif isinstance(obj, list):
            return [walk(item) for item in obj]
        return obj

    return walk(data)


@pytest.fixture
def sanitize():
    return sanitize_api_response


@pytest.fixture
def snapshot(snapshot):
    return snapshot.use_extension(JSONSnapshotExtension)


@pytest.fixture
def api_client():
    return APIClient()


@pytest.fixture
def vessel(db):
    return Vessel.objects.create(
        name="Moana", vessel_type="yacht", description="A 26ft trailer yacht"
    )


@pytest.fixture
def person(db):
    return Person.objects.create(
        first_name="Alice", last_name="Smith", email="alice@example.com"
    )


@pytest.fixture
def journey_with_full_data(vessel, person):
    """A journey with legs, modes, annotations, aboard, track, and track points."""
    journey = Journey.objects.create(
        vessel=vessel,
        name="Saturday Sail",
        started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
        ended_at=datetime(2026, 1, 4, 16, 0, tzinfo=timezone.utc),
    )
    Leg.objects.create(
        journey=journey,
        name="Out to Diamond Harbour",
        started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        ended_at=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
    )
    Mode.objects.create(
        vessel=vessel,
        mode="MOTOR",
        started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
    )
    Mode.objects.create(
        vessel=vessel,
        mode="SAIL",
        started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
    )
    Mode.objects.create(
        vessel=vessel,
        mode="ANCHOR",
        started_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
    )
    Annotation.objects.create(
        vessel=vessel,
        type="TACK",
        timestamp=datetime(2026, 1, 4, 10, 30, tzinfo=timezone.utc),
    )
    Annotation.objects.create(
        vessel=vessel,
        type="GYBE",
        timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
    )
    Aboard.objects.create(
        vessel=vessel,
        person=person,
        started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
    )
    track = Track.objects.create(
        vessel=vessel,
        name="GPS Track",
        started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
    )
    TrackPoint.objects.create(
        track=track,
        location=Point(172.72, -43.60, srid=4326),
        speed=5.0,
        timestamp=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
    )
    TrackPoint.objects.create(
        track=track,
        location=Point(172.68, -43.62, srid=4326),
        speed=6.0,
        timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
    )
    TrackPoint.objects.create(
        track=track,
        location=Point(172.63, -43.63, srid=4326),
        speed=4.5,
        timestamp=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
    )
    journey.track = track
    return journey
