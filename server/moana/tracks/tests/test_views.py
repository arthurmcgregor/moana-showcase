import io
from datetime import datetime, timezone

from django.contrib.gis.geos import Point
from django.test import TestCase
from django.urls import reverse

from tracks.models import Track, TrackPoint


class TrackUploadTestCase(TestCase):
    def setUp(self):
        self.upload_url = reverse("tracks:track-upload-csv")

    def create_csv_file(self, content):
        """Helper to create an in-memory CSV file."""
        return io.BytesIO(content.encode("utf-8"))

    def test_upload_valid_csv(self):
        """Test uploading a valid CSV file."""
        csv_content = """lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time
-43.530911,172.636739,0.28,9,45.06,1.10,0.00,040126,13000000
-43.530926,172.636737,0.21,9,45.06,1.10,0.00,040126,13000100
-43.530932,172.636738,0.16,9,42.96,1.10,0.00,040126,13000200"""

        csv_file = self.create_csv_file(csv_content)
        csv_file.name = "test_track.csv"

        response = self.client.post(
            self.upload_url,
            {"file": csv_file, "name": "Test Track", "description": "Test description"},
        )

        self.assertEqual(response.status_code, 201)
        data = response.json()
        self.assertTrue(data["success"])
        self.assertEqual(data["points_created"], 3)
        self.assertEqual(data["track_name"], "Test Track")

        # Verify Track was created
        track = Track.objects.get(id=data["track_id"])
        self.assertEqual(track.name, "Test Track")
        self.assertEqual(track.description, "Test description")
        self.assertEqual(track.points.count(), 3)

    def test_upload_without_optional_fields(self):
        """Test uploading CSV with minimal fields."""
        csv_content = """lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time
-43.530911,172.636739,,,,,0.00,040126,13000000
-43.530926,172.636737,,,,,0.00,040126,13000100"""

        csv_file = self.create_csv_file(csv_content)
        csv_file.name = "minimal_track.csv"

        response = self.client.post(self.upload_url, {"file": csv_file})

        self.assertEqual(response.status_code, 201)
        data = response.json()
        self.assertTrue(data["success"])
        self.assertEqual(data["points_created"], 2)

        # Verify TrackPoints have None for optional fields
        track = Track.objects.get(id=data["track_id"])
        point = track.points.first()
        self.assertIsNone(point.speed)
        self.assertIsNone(point.satellites)
        self.assertIsNone(point.altitude)

    def test_upload_no_file(self):
        """Test uploading without a file."""
        response = self.client.post(self.upload_url, {})
        self.assertEqual(response.status_code, 400)
        self.assertIn("error", response.json())

    def test_upload_missing_required_columns(self):
        """Test uploading CSV with missing required columns."""
        csv_content = """lat,lng,speed
-43.530911,172.636739,0.28"""

        csv_file = self.create_csv_file(csv_content)
        csv_file.name = "invalid_track.csv"

        response = self.client.post(self.upload_url, {"file": csv_file})
        self.assertEqual(response.status_code, 400)
        data = response.json()
        self.assertIn("Missing required columns", data["error"])

    def test_upload_invalid_coordinates(self):
        """Test uploading CSV with invalid coordinates."""
        csv_content = """lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time
-95.0,172.636739,0.28,9,45.06,1.10,0.00,040126,13000000"""

        csv_file = self.create_csv_file(csv_content)
        csv_file.name = "invalid_coords.csv"

        response = self.client.post(self.upload_url, {"file": csv_file})
        self.assertEqual(response.status_code, 400)
        self.assertIn("No valid track points", response.json()["error"])

    def test_upload_invalid_date_format(self):
        """Test uploading CSV with invalid date format."""
        csv_content = """lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time
-43.530911,172.636739,0.28,9,45.06,1.10,0.00,999999,13000000"""

        csv_file = self.create_csv_file(csv_content)
        csv_file.name = "invalid_date.csv"

        response = self.client.post(self.upload_url, {"file": csv_file})
        # Should return 400 with no valid points
        self.assertEqual(response.status_code, 400)

    def test_track_timestamps_calculated_correctly(self):
        """Test that Track started_at and ended_at are calculated from points."""
        csv_content = """lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time
-43.530911,172.636739,0.28,9,45.06,1.10,0.00,040126,10000000
-43.530926,172.636737,0.21,9,45.06,1.10,0.00,040126,12000000
-43.530932,172.636738,0.16,9,42.96,1.10,0.00,040126,14000000"""

        csv_file = self.create_csv_file(csv_content)
        csv_file.name = "time_test.csv"

        response = self.client.post(self.upload_url, {"file": csv_file})
        data = response.json()

        track = Track.objects.get(id=data["track_id"])
        expected_start = datetime(2026, 1, 4, 10, 0, 0, tzinfo=timezone.utc)
        expected_end = datetime(2026, 1, 4, 14, 0, 0, tzinfo=timezone.utc)

        self.assertEqual(track.started_at, expected_start)
        self.assertEqual(track.ended_at, expected_end)

    def test_point_geometry_stored_correctly(self):
        """Test that Point geometry is stored with correct lng/lat order."""
        csv_content = """lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time
-43.530911,172.636739,0.28,9,45.06,1.10,0.00,040126,13000000"""

        csv_file = self.create_csv_file(csv_content)
        csv_file.name = "geometry_test.csv"

        response = self.client.post(self.upload_url, {"file": csv_file})
        data = response.json()

        track = Track.objects.get(id=data["track_id"])
        point = track.points.first()

        # PostGIS Point stores as (lng, lat)
        expected_point = Point(172.636739, -43.530911, srid=4326)
        self.assertEqual(point.location, expected_point)

    def test_upload_with_imu_columns(self):
        """IMU columns from current firmware persist to TrackPoint and the GeoJSON endpoint."""
        csv_content = """lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time,heel,trim,heading
-43.530911,172.636739,0.28,9,45.06,1.10,0.00,040126,13000000,12.34,-2.50,274.80
-43.530926,172.636737,0.21,9,45.06,1.10,0.00,040126,13000100,,,"""

        csv_file = self.create_csv_file(csv_content)
        csv_file.name = "imu_track.csv"

        response = self.client.post(self.upload_url, {"file": csv_file})
        self.assertEqual(response.status_code, 201)
        data = response.json()

        track = Track.objects.get(id=data["track_id"])
        points = list(track.points.order_by("timestamp"))
        self.assertAlmostEqual(points[0].heel, 12.34)
        self.assertAlmostEqual(points[0].trim, -2.50)
        self.assertAlmostEqual(points[0].heading, 274.80)
        self.assertIsNone(points[1].heel)
        self.assertIsNone(points[1].trim)
        self.assertIsNone(points[1].heading)

        geojson_url = reverse("tracks:track-geojson", kwargs={"pk": track.id})
        gj = self.client.get(geojson_url).json()
        props = gj["features"][0]["properties"]
        self.assertAlmostEqual(props["heel"], 12.34)
        self.assertAlmostEqual(props["trim"], -2.50)
        self.assertAlmostEqual(props["heading"], 274.80)
        self.assertIsNone(gj["features"][1]["properties"]["heel"])

    def test_upload_with_warnings(self):
        """Test that valid points are saved even if some rows fail."""
        csv_content = """lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time
-43.530911,172.636739,0.28,9,45.06,1.10,0.00,040126,13000000
-95.0,172.636739,0.28,9,45.06,1.10,0.00,040126,13000100
-43.530932,172.636738,0.16,9,42.96,1.10,0.00,040126,13000200"""

        csv_file = self.create_csv_file(csv_content)
        csv_file.name = "partial_valid.csv"

        response = self.client.post(self.upload_url, {"file": csv_file})
        self.assertEqual(response.status_code, 201)
        data = response.json()

        # Should have 2 valid points
        self.assertEqual(data["points_created"], 2)
        # Should have warnings about the invalid row
        self.assertIn("warnings", data)
        self.assertEqual(len(data["warnings"]), 1)


class TrackUploadEventsSidecarTestCase(TestCase):
    """Events sidecar -> Mode rows on the CSV upload endpoint (issue #139)."""

    CSV_CONTENT = """lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time
-43.530911,172.636739,0.28,9,45.06,1.10,0.00,040126,13000000
-43.530926,172.636737,0.21,9,45.06,1.10,0.00,040126,13000100"""

    def setUp(self):
        from tracks.models import Vessel

        self.upload_url = reverse("tracks:track-upload-csv")
        self.vessel = Vessel.objects.create(name="Moana")

    def make_files(self, events_content):
        csv_file = io.BytesIO(self.CSV_CONTENT.encode("utf-8"))
        csv_file.name = "gps_20260104_130000.csv"
        events_file = io.BytesIO(events_content.encode("utf-8"))
        events_file.name = "gps_20260104_130000.events.jsonl"
        return csv_file, events_file

    def upload(self, events_content, **extra):
        csv_file, events_file = self.make_files(events_content)
        data = {"file": csv_file, "events": events_file, "vessel": self.vessel.id}
        data.update(extra)
        if data.get("vessel") is None:
            del data["vessel"]
        return self.client.post(self.upload_url, data)

    def test_sidecar_creates_mode_rows_including_other(self):
        from tracks.models import Mode

        # 30 s apart: clear of MODE_DEBOUNCE_SECONDS so all three are
        # deliberate changes, and inside the recording so none of them trips
        # the outside-the-track-window warning.
        response = self.upload(
            '{"kind":"SAIL","utc":1767531600}\n'
            '{"kind":"MOTOR","utc":1767531630}\n'
            '{"kind":"OTHER","utc":1767531660}\n'
        )

        self.assertEqual(response.status_code, 201)
        data = response.json()
        self.assertEqual(data["modes_created"], 3)
        self.assertNotIn("warnings", data)

        modes = list(Mode.objects.filter(vessel=self.vessel).order_by("started_at"))
        self.assertEqual([m.mode for m in modes], ["SAIL", "MOTOR", "OTHER"])
        # Press-time UTC from the sidecar, not upload time.
        self.assertEqual(
            modes[0].started_at,
            datetime(2026, 1, 4, 13, 0, 0, tzinfo=timezone.utc),
        )
        self.assertEqual(modes[0].metadata, {"source": "watch"})

    def test_sidecar_creates_a_mode_row_for_every_wire_mode(self):
        from tracks.events_jsonl import MODE_KINDS
        from tracks.models import Mode

        # The watch picker can set any Mode.MODE_CHOICES value (issue #146),
        # so import has to turn all of them into rows — not just the three the
        # GPIO0 buttons send. One event per minute from the CSV's start.
        base = 1767531600
        kinds = list(MODE_KINDS)
        response = self.upload(
            "".join(
                f'{{"kind":"{kind}","utc":{base + 60 * i}}}\n'
                for i, kind in enumerate(kinds)
            )
        )

        self.assertEqual(response.status_code, 201)
        data = response.json()
        self.assertEqual(data["modes_created"], len(kinds))
        self.assertNotIn("warnings", data)

        modes = list(Mode.objects.filter(vessel=self.vessel).order_by("started_at"))
        self.assertEqual([m.mode for m in modes], list(MODE_KINDS.values()))

    def test_reupload_is_idempotent(self):
        from tracks.models import Mode

        events = '{"kind":"SAIL","utc":1767531600}\n{"kind":"MOTOR","utc":1767535200}\n'

        first = self.upload(events)
        self.assertEqual(first.status_code, 201)
        self.assertEqual(first.json()["modes_created"], 2)

        second = self.upload(events)
        self.assertEqual(second.status_code, 201)
        self.assertEqual(second.json()["modes_created"], 0)
        self.assertEqual(Mode.objects.filter(vessel=self.vessel).count(), 2)

    def test_reupload_does_not_clobber_reclassified_mode(self):
        from tracks.models import Mode

        self.upload('{"kind":"OTHER","utc":1767531600}\n')
        mode = Mode.objects.get(vessel=self.vessel)
        mode.mode = "ANCHOR"  # reclassified in the web UI
        mode.save()

        self.upload('{"kind":"OTHER","utc":1767531600}\n')
        mode.refresh_from_db()
        self.assertEqual(mode.mode, "ANCHOR")

    def test_utc_zero_event_skipped_with_warning(self):
        from tracks.models import Mode

        response = self.upload(
            '{"kind":"SAIL","utc":0}\n{"kind":"MOTOR","utc":1767535200}\n'
        )

        self.assertEqual(response.status_code, 201)
        data = response.json()
        self.assertEqual(data["modes_created"], 1)
        self.assertTrue(any("utc=0" in w for w in data["warnings"]))
        self.assertEqual(Mode.objects.filter(vessel=self.vessel).count(), 1)

    def test_mis_press_burst_becomes_one_mode_row(self):
        from tracks.models import Mode

        # Tapped SAIL, meant MOTOR, tapped again three seconds later — one
        # segment, starting at the first press (issue #161).
        response = self.upload(
            '{"kind":"SAIL","utc":1767531600}\n'
            '{"kind":"MOTOR","utc":1767531603}\n'
            '{"kind":"ANCHOR","utc":1767535200}\n'
        )

        self.assertEqual(response.status_code, 201)
        data = response.json()
        self.assertEqual(data["modes_created"], 2)
        self.assertTrue(any("Debounced" in w for w in data["warnings"]))

        modes = list(Mode.objects.filter(vessel=self.vessel).order_by("started_at"))
        self.assertEqual([m.mode for m in modes], ["MOTOR", "ANCHOR"])
        self.assertEqual(
            modes[0].started_at,
            datetime(2026, 1, 4, 13, 0, 0, tzinfo=timezone.utc),
        )

    def test_sidecar_without_vessel_ignored_with_warning(self):
        from tracks.models import Mode

        response = self.upload('{"kind":"SAIL","utc":1767531600}\n', vessel=None)

        self.assertEqual(response.status_code, 201)
        data = response.json()
        self.assertEqual(data["modes_created"], 0)
        self.assertTrue(any("no vessel" in w for w in data["warnings"]))
        self.assertEqual(Mode.objects.count(), 0)

    def test_malformed_sidecar_does_not_fail_track_upload(self):
        from tracks.models import Track

        response = self.upload("this is not json\n")

        self.assertEqual(response.status_code, 201)
        data = response.json()
        self.assertEqual(data["modes_created"], 0)
        self.assertTrue(data["success"])
        self.assertEqual(Track.objects.count(), 1)

    def test_utc_beyond_the_wire_range_does_not_fail_the_upload(self):
        from tracks.models import Mode, Track

        # datetime.fromtimestamp raises OverflowError on this. The parser is
        # the only thing between it and a 500 on the whole track upload — the
        # sidecar block only catches UnicodeDecodeError.
        response = self.upload('{"kind":"SAIL","utc":100000000000000000000}\n')

        self.assertEqual(response.status_code, 201)
        data = response.json()
        self.assertEqual(data["modes_created"], 0)
        self.assertTrue(any("out of range" in w for w in data["warnings"]))
        self.assertEqual(Track.objects.count(), 1)
        self.assertEqual(Mode.objects.count(), 0)

    def test_boolean_utc_creates_no_mode_row(self):
        from tracks.models import Mode

        response = self.upload('{"kind":"SAIL","utc":true}\n')

        self.assertEqual(response.status_code, 201)
        self.assertEqual(response.json()["modes_created"], 0)
        # Not a row at 1970-01-01T00:00:01Z, which is where an int check that
        # accepts booleans puts it.
        self.assertEqual(Mode.objects.count(), 0)

    def test_events_far_outside_the_track_window_are_kept_with_a_warning(self):
        from tracks.models import Mode

        # Kept, because a Mode row is the record of what the helm said and the
        # upload is not the place to overrule it — but flagged, because the
        # track's own range is the only clue there is and it is gone after
        # this request.
        response = self.upload(
            '{"kind":"SAIL","utc":1767531600}\n{"kind":"MOTOR","utc":1}\n'
        )

        self.assertEqual(response.status_code, 201)
        data = response.json()
        self.assertEqual(data["modes_created"], 2)
        self.assertTrue(
            any("outside the track's time range" in w for w in data["warnings"])
        )
        self.assertEqual(Mode.objects.count(), 2)

    def test_duplicate_sidecar_lines_create_one_mode_row(self):
        from tracks.models import Mode

        # The sidecar has no seq to dedupe on; identical lines share a
        # timestamp, so the debounce collapses them before import.
        response = self.upload('{"kind":"SAIL","utc":1767531600}\n' * 3)

        self.assertEqual(response.status_code, 201)
        data = response.json()
        self.assertEqual(data["modes_created"], 1)
        self.assertTrue(any("Debounced 3" in w for w in data["warnings"]))
        self.assertEqual(Mode.objects.count(), 1)

    def test_reupload_leaves_a_reclassified_mode_alone(self):
        from tracks.models import Mode, Track

        content = '{"kind":"OTHER","utc":1767531600}\n'
        self.assertEqual(self.upload(content).json()["modes_created"], 1)

        # OTHER is the watch's "not sailing, not motoring" placeholder, meant
        # to be reclassified in the web UI (issue #139). A re-upload must not
        # undo that.
        Mode.objects.filter(vessel=self.vessel).update(mode="ANCHOR")

        response = self.upload(content)

        self.assertEqual(response.status_code, 201)
        self.assertEqual(response.json()["modes_created"], 0)
        self.assertEqual(Mode.objects.count(), 1)
        self.assertEqual(Mode.objects.get().mode, "ANCHOR")
        # The track itself is not deduped on source_filename: the same CSV
        # uploaded twice is two tracks (see the Phase 5 findings on #156).
        self.assertEqual(Track.objects.count(), 2)

    def test_upload_without_events_field_unchanged(self):
        csv_file = io.BytesIO(self.CSV_CONTENT.encode("utf-8"))
        csv_file.name = "gps_20260104_130000.csv"

        response = self.client.post(
            self.upload_url, {"file": csv_file, "vessel": self.vessel.id}
        )

        self.assertEqual(response.status_code, 201)
        self.assertNotIn("modes_created", response.json())
