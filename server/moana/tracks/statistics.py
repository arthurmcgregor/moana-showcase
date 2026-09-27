"""
Journey statistics service.

Single entry point for all vessel statistics computation using PostGIS.
Replaces the scattered stats logic that was previously split across
utils.py, models.py, and serializers.py.
"""

from __future__ import annotations

from dataclasses import dataclass
from datetime import timedelta

from django.db import connection
from django.utils import timezone

_NM_IN_METERS = 1852.0

ON_WATER_MODE_DISPLAYS = frozenset(
    {"Sailing", "Motoring", "Motor Sailing", "Drifting/Unpowered"}
)


@dataclass(frozen=True)
class Stats:
    """Statistics for a time range (journey, leg, or mode)."""

    distance_nm: float
    duration: timedelta | None
    max_speed_knots: float
    avg_speed_knots: float
    by_mode: dict[str, Stats]

    def to_dict(self, *, _top_level: bool = True) -> dict:
        prefix = "total_" if _top_level else ""
        result = {
            f"{prefix}distance_nm": self.distance_nm,
            f"{prefix}duration_seconds": (
                int(self.duration.total_seconds()) if self.duration else 0
            ),
            "max_speed_knots": self.max_speed_knots,
            "avg_speed_knots": self.avg_speed_knots,
        }
        if _top_level:
            result["by_mode"] = {
                name: mode_stats.to_dict(_top_level=False)
                for name, mode_stats in self.by_mode.items()
            }
        return result


@dataclass(frozen=True)
class JourneyStats:
    """Statistics for a journey and all its legs."""

    journey: Stats
    legs: dict[int, Stats]

    def to_dict(self) -> dict:
        return {
            "journey": self.journey.to_dict(),
            "legs": {lid: s.to_dict() for lid, s in self.legs.items()},
        }


def _postgis_stats(vessel_id, start_time, end_time):
    """Compute distance and speed stats for a time range using PostGIS."""
    with connection.cursor() as cursor:
        cursor.execute(
            """
            SELECT
                ST_Length(
                    ST_MakeLine(tp.location::geometry ORDER BY tp.timestamp)::geography
                ),
                MAX(tp.speed),
                AVG(tp.speed)
            FROM tracks_trackpoint tp
            INNER JOIN tracks_track t ON tp.track_id = t.id
            WHERE t.vessel_id = %s
              AND tp.timestamp >= %s
              AND tp.timestamp <= %s
            """,
            [vessel_id, start_time, end_time],
        )
        row = cursor.fetchone()
    if row and row[0] is not None:
        return row[0], row[1] or 0, float(row[2] or 0)
    return 0, 0, 0


def _postgis_mode_stats(vessel_id, modes_list, end_time):
    """Compute per-mode distance and speed stats using a single PostGIS query."""
    if not modes_list:
        return {}

    mode_ranges = []
    for i, mode in enumerate(modes_list):
        mode_end = (
            modes_list[i + 1].started_at
            if i + 1 < len(modes_list)
            else (end_time or timezone.now())
        )
        mode_ranges.append((mode.get_mode_display(), mode.started_at, mode_end))

    placeholders = ", ".join(
        ["(%s, %s::timestamptz, %s::timestamptz)"] * len(mode_ranges)
    )
    params = []
    for mode_key, start, end in mode_ranges:
        params.extend([mode_key, start, end])

    with connection.cursor() as cursor:
        cursor.execute(
            f"""
            WITH mode_ranges(mode_key, start_ts, end_ts) AS (
                VALUES {placeholders}
            )
            SELECT
                mr.mode_key,
                ST_Length(
                    ST_MakeLine(tp.location::geometry ORDER BY tp.timestamp)::geography
                ),
                MAX(tp.speed),
                AVG(tp.speed),
                EXTRACT(EPOCH FROM (mr.end_ts - mr.start_ts))
            FROM mode_ranges mr
            LEFT JOIN tracks_trackpoint tp
                ON tp.timestamp >= mr.start_ts AND tp.timestamp <= mr.end_ts
            LEFT JOIN tracks_track t
                ON tp.track_id = t.id AND t.vessel_id = %s
            WHERE tp.id IS NOT NULL
            GROUP BY mr.mode_key, mr.start_ts, mr.end_ts
            ORDER BY mr.start_ts
            """,
            params + [vessel_id],
        )
        rows = cursor.fetchall()

    agg: dict[str, dict] = {}
    for mode_key, distance_m, max_speed, avg_speed, duration_s in rows:
        d_m = float(distance_m or 0)
        dur_s = int(duration_s or 0)
        bucket = agg.setdefault(
            mode_key,
            {"distance_m": 0.0, "duration_s": 0, "max_speed": 0.0, "avg_weighted": 0.0},
        )
        bucket["distance_m"] += d_m
        bucket["duration_s"] += dur_s
        bucket["max_speed"] = max(bucket["max_speed"], float(max_speed or 0))
        bucket["avg_weighted"] += float(avg_speed or 0) * dur_s

    by_mode = {}
    for mode_key, b in agg.items():
        avg = b["avg_weighted"] / b["duration_s"] if b["duration_s"] else 0.0
        by_mode[mode_key] = Stats(
            distance_nm=b["distance_m"] / _NM_IN_METERS,
            duration=timedelta(seconds=b["duration_s"]),
            max_speed_knots=b["max_speed"],
            avg_speed_knots=avg,
            by_mode={},
        )
    return by_mode


def _build_stats(vessel_id, start_time, end_time, modes_list):
    """Build Stats for a time range (journey or leg)."""
    effective_end = end_time or timezone.now()

    distance_m, max_speed, avg_speed = _postgis_stats(
        vessel_id, start_time, effective_end
    )
    duration = (end_time - start_time) if end_time else None
    by_mode = _postgis_mode_stats(vessel_id, modes_list, end_time)

    return Stats(
        distance_nm=distance_m / _NM_IN_METERS,
        duration=duration,
        max_speed_knots=max_speed,
        avg_speed_knots=avg_speed,
        by_mode=by_mode,
    )


class JourneyStatisticsService:
    """Computes statistics for journeys, legs, and arbitrary time ranges."""

    def for_journey(self, journey) -> JourneyStats:
        """Compute journey-level and per-leg statistics in one batch."""
        legs = list(journey.legs.all())
        modes_list = list(journey.get_modes().order_by("started_at"))

        journey_stats = _build_stats(
            journey.vessel_id, journey.started_at, journey.ended_at, modes_list
        )

        leg_stats = {}
        for leg in legs:
            leg_modes = [
                m
                for m in modes_list
                if m.started_at >= leg.started_at
                and (leg.ended_at is None or m.started_at <= leg.ended_at)
            ]
            leg_stats[leg.id] = _build_stats(
                journey.vessel_id, leg.started_at, leg.ended_at, leg_modes
            )

        return JourneyStats(journey=journey_stats, legs=leg_stats)

    def for_leg(self, leg) -> Stats:
        """Compute statistics for a single leg."""
        modes_list = list(leg.get_modes().order_by("started_at"))
        return _build_stats(
            leg.journey.vessel_id, leg.started_at, leg.ended_at, modes_list
        )

    def on_water_distance_nm(self, journey) -> float:
        """On-water distance in nautical miles for a journey.

        Sums distance from on-water modes (sailing, motoring, motor-sailing,
        drifting). Falls back to total distance when the journey has no modes
        or no on-water modes.
        """
        modes_list = list(journey.get_modes().order_by("started_at"))

        on_water_total = 0.0
        if modes_list:
            by_mode = _postgis_mode_stats(
                journey.vessel_id, modes_list, journey.ended_at
            )
            on_water_total = sum(
                stats.distance_nm
                for mode_name, stats in by_mode.items()
                if mode_name in ON_WATER_MODE_DISPLAYS
            )

        if on_water_total > 0.0:
            return on_water_total

        # Fallback: no modes or no on-water modes — use total distance
        effective_end = journey.ended_at or timezone.now()
        distance_m, _, _ = _postgis_stats(
            journey.vessel_id, journey.started_at, effective_end
        )
        return distance_m / _NM_IN_METERS

    def for_time_range(self, vessel_id, start, end, modes=None) -> Stats:
        """Compute statistics for an arbitrary time range."""
        modes_list = list(modes.order_by("started_at")) if modes is not None else []
        return _build_stats(vessel_id, start, end, modes_list)

    def estimated_distance_nm(self, journey) -> float | None:
        """Nautical miles of a journey's manual_route, or None when unset."""
        if journey.manual_route is None:
            return None
        # Prefer the batch-annotated length (avoids N+1 on list views).
        precomputed = getattr(journey, "_manual_route_length_m", None)
        if precomputed is not None:
            return precomputed.m / _NM_IN_METERS

        from django.contrib.gis.db.models.functions import Length

        from .models import Journey

        row = (
            Journey.objects.filter(pk=journey.pk)
            .annotate(_route_length=Length("manual_route"))
            .values_list("_route_length", flat=True)
            .first()
        )
        if row is None:
            return None
        return row.m / _NM_IN_METERS


journey_stats = JourneyStatisticsService()
