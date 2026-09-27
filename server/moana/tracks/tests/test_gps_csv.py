"""Tests for the GPS CSV parser."""

from datetime import datetime, timezone

import pytest

from tracks.gps_csv import parse_gps_csv, parse_gps_date, parse_gps_datetime, parse_gps_time


class TestParseGpsDate:
    def test_standard_date(self):
        assert parse_gps_date(40126) == datetime(2026, 1, 4)

    def test_pads_short_values(self):
        # 71125 -> "071125" -> 07 Nov 2025
        assert parse_gps_date(71125) == datetime(2025, 11, 7)


class TestParseGpsTime:
    def test_standard_time(self):
        hours, minutes, seconds, microseconds = parse_gps_time(13000000)
        assert (hours, minutes, seconds) == (13, 0, 0)
        assert microseconds == 0

    def test_centiseconds_to_microseconds(self):
        _, _, _, microseconds = parse_gps_time(23023099)
        assert microseconds == 990000  # 99 centiseconds

    def test_hours_above_24_preserved(self):
        hours, _, _, _ = parse_gps_time(25300000)
        assert hours == 25  # Caller handles rollover


class TestParseGpsDatetime:
    def test_combines_date_and_time(self):
        result = parse_gps_datetime(40126, 13000000)
        assert result == datetime(2026, 1, 4, 13, 0, 0, tzinfo=timezone.utc)

    def test_hour_24_rolls_to_next_day(self):
        result = parse_gps_datetime(40126, 24580000)
        assert result == datetime(2026, 1, 5, 0, 58, 0, tzinfo=timezone.utc)

    def test_hour_25_rolls_to_next_day(self):
        result = parse_gps_datetime(40126, 25300000)
        assert result == datetime(2026, 1, 5, 1, 30, 0, tzinfo=timezone.utc)


VALID_CSV = """\
lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time
-43.530911,172.636739,0.28,9,45.06,1.10,0.00,040126,13000000
-43.530926,172.636737,0.21,9,45.06,1.10,0.00,040126,13000100
"""

CSV_WITH_BAD_ROW = """\
lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time
-43.530911,172.636739,0.28,9,45.06,1.10,0.00,040126,13000000
-95.0,172.636739,0.28,9,45.06,1.10,0.00,040126,13000100
-43.530926,172.636737,0.21,9,45.06,1.10,0.00,040126,13000200
"""


class TestParseGpsCsv:
    def test_parses_valid_csv(self):
        result = parse_gps_csv(VALID_CSV)
        assert result.is_valid
        assert len(result.points) == 2
        assert result.errors == []
        assert result.started_at == datetime(2026, 1, 4, 13, 0, 0, tzinfo=timezone.utc)
        assert result.ended_at == datetime(2026, 1, 4, 13, 0, 1, tzinfo=timezone.utc)

    def test_collects_invalid_rows_as_errors(self):
        result = parse_gps_csv(CSV_WITH_BAD_ROW)
        assert len(result.points) == 2
        assert len(result.errors) == 1
        assert "Row 3" in result.errors[0]

    def test_missing_columns_raises_valueerror(self):
        with pytest.raises(ValueError, match="Missing required columns"):
            parse_gps_csv("lat,lng,speed\n-43,172,0.0\n")


CSV_WITH_IMU = """\
lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time,heel,trim,heading
-43.530911,172.636739,0.28,9,45.06,1.10,0.00,040126,13000000,12.34,-2.50,274.80
-43.530926,172.636737,0.21,9,45.06,1.10,0.00,040126,13000100,,,
"""

CSV_WITH_BAD_IMU = """\
lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time,heel,trim,heading
-43.530911,172.636739,0.28,9,45.06,1.10,0.00,040126,13000000,180.0,0.0,90.0
-43.530926,172.636737,0.21,9,45.06,1.10,0.00,040126,13000100,10.0,0.0,360.0
-43.530932,172.636738,0.16,9,42.96,1.10,0.00,040126,13000200,10.0,0.0,90.0
"""


class TestImuColumns:
    def test_parses_imu_values(self):
        result = parse_gps_csv(CSV_WITH_IMU)
        assert result.is_valid
        assert result.errors == []
        assert result.points[0].heel == 12.34
        assert result.points[0].trim == -2.50
        assert result.points[0].heading == 274.80

    def test_empty_imu_cells_become_none(self):
        result = parse_gps_csv(CSV_WITH_IMU)
        assert result.points[1].heel is None
        assert result.points[1].trim is None
        assert result.points[1].heading is None

    def test_missing_imu_columns_is_ok(self):
        result = parse_gps_csv(VALID_CSV)
        assert result.is_valid
        assert all(p.heel is None and p.trim is None and p.heading is None for p in result.points)

    def test_out_of_range_imu_rejected(self):
        result = parse_gps_csv(CSV_WITH_BAD_IMU)
        assert len(result.points) == 1
        assert len(result.errors) == 2
        assert "heel" in result.errors[0]
        assert "heading" in result.errors[1]

    def test_empty_valid_points_is_not_valid(self):
        csv = "lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time\n"
        csv += "-95.0,172.0,0,0,0,0,0,040126,13000000\n"
        result = parse_gps_csv(csv)
        assert not result.is_valid
        assert result.started_at is None
