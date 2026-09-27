"""
GPS CSV parser.

Parses CSV files from the device's GPS recorder into validated points
ready for persistence. Handles GPS-specific date/time formats (DDMMYY,
HHMMSScc) and coordinate validation.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from io import StringIO

from django.contrib.gis.geos import Point


@dataclass
class ParsedPoint:
    """A single validated GPS point ready for TrackPoint creation."""

    location: Point
    timestamp: datetime
    speed: float | None = None
    satellites: int | None = None
    altitude: float | None = None
    hdop: float | None = None
    course: float | None = None
    heel: float | None = None
    trim: float | None = None
    heading: float | None = None


@dataclass
class ParseResult:
    """Output of parsing a GPS CSV file."""

    points: list[ParsedPoint]
    errors: list[str]
    started_at: datetime | None = None
    ended_at: datetime | None = None

    @property
    def is_valid(self) -> bool:
        return len(self.points) > 0


REQUIRED_COLUMNS = frozenset({"lat", "lng", "gps_date", "gps_time"})


def parse_gps_date(date_value: int) -> datetime:
    """Parse GPS date in DDMMYY format.

    Examples:
        040126 -> datetime(2026, 1, 4)
        71125  -> datetime(2025, 11, 7)  (left-padded to 071125)
    """
    date_str = str(date_value).zfill(6)
    day = int(date_str[0:2])
    month = int(date_str[2:4])
    year = int(date_str[4:6]) + 2000
    return datetime(year, month, day)


def parse_gps_time(time_value: int) -> tuple[int, int, int, int]:
    """Parse GPS time in HHMMSScc format (cc = centiseconds).

    Returns (hours, minutes, seconds, microseconds).
    Hours >= 24 are allowed — caller handles day rollover.
    """
    time_str = str(time_value).zfill(8)
    hours = int(time_str[0:2])
    minutes = int(time_str[2:4])
    seconds = int(time_str[4:6])
    centiseconds = int(time_str[6:8])
    return hours, minutes, seconds, centiseconds * 10000


def parse_gps_datetime(date_value: int, time_value: int) -> datetime:
    """Combine GPS date and time into a timezone-aware UTC datetime.

    Handles hours >= 24 by wrapping to the next day.
    """
    date = parse_gps_date(date_value)
    hours, minutes, seconds, microseconds = parse_gps_time(time_value)

    if hours >= 24:
        date = date + timedelta(days=hours // 24)
        hours = hours % 24

    return datetime(
        date.year, date.month, date.day,
        hours, minutes, seconds, microseconds,
        tzinfo=timezone.utc,
    )


def _parse_row(row: dict[str, str], row_num: int) -> ParsedPoint:
    """Parse a single CSV row into a ParsedPoint.

    Raises ValueError with a descriptive message for invalid rows.
    """
    lat = float(row["lat"])
    lng = float(row["lng"])

    if not (-90 <= lat <= 90) or not (-180 <= lng <= 180):
        raise ValueError(f"Invalid coordinates ({lat}, {lng})")

    timestamp = parse_gps_datetime(int(row["gps_date"]), int(row["gps_time"]))

    heel = float(row["heel"]) if row.get("heel") else None
    trim = float(row["trim"]) if row.get("trim") else None
    heading = float(row["heading"]) if row.get("heading") else None

    if heel is not None and not (-90 <= heel <= 90):
        raise ValueError(f"Invalid heel {heel} (expected -90..90)")
    if trim is not None and not (-90 <= trim <= 90):
        raise ValueError(f"Invalid trim {trim} (expected -90..90)")
    if heading is not None and not (0 <= heading < 360):
        raise ValueError(f"Invalid heading {heading} (expected 0..<360)")

    return ParsedPoint(
        location=Point(lng, lat, srid=4326),
        timestamp=timestamp,
        speed=float(row["speed"]) if row.get("speed") else None,
        satellites=int(row["satellites"]) if row.get("satellites") else None,
        altitude=float(row["altitude"]) if row.get("altitude") else None,
        hdop=float(row["hdop"]) if row.get("hdop") else None,
        course=float(row["course"]) if row.get("course") else None,
        heel=heel,
        trim=trim,
        heading=heading,
    )


def parse_gps_csv(csv_content: str) -> ParseResult:
    """Parse a GPS CSV string into validated points.

    Raises:
        ValueError: If required columns (lat, lng, gps_date, gps_time) are missing.
    """
    reader = csv.DictReader(StringIO(csv_content))

    if not REQUIRED_COLUMNS.issubset(reader.fieldnames or []):
        raise ValueError(
            f"Missing required columns. Required: {REQUIRED_COLUMNS}, "
            f"found: {reader.fieldnames}"
        )

    points: list[ParsedPoint] = []
    errors: list[str] = []

    for row_num, row in enumerate(reader, start=2):
        try:
            points.append(_parse_row(row, row_num))
        except (ValueError, KeyError) as e:
            errors.append(f"Row {row_num}: {e}")

    started_at = min(p.timestamp for p in points) if points else None
    ended_at = max(p.timestamp for p in points) if points else None

    return ParseResult(
        points=points, errors=errors,
        started_at=started_at, ended_at=ended_at,
    )
