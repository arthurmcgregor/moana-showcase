from datetime import datetime, timezone

from django.test import TestCase
from rest_framework.test import APIClient

from tracks.models import Aboard, Annotation, Journey, Leg, Mode, Person, Vessel


class APITestCase(TestCase):
    def setUp(self):
        self.client = APIClient()
        self.vessel = Vessel.objects.create(name="Test Yacht", vessel_type="yacht")
        self.person = Person.objects.create(first_name="Alice", last_name="Smith")

    def test_vessel_list(self):
        """Test listing vessels."""
        response = self.client.get("/api/tracks/vessels/")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(response.data["results"]), 1)
        self.assertEqual(response.data["results"][0]["name"], "Test Yacht")

    def test_vessel_create(self):
        """Test creating a vessel."""
        data = {"name": "New Boat", "vessel_type": "dinghy"}
        response = self.client.post("/api/tracks/vessels/", data)
        self.assertEqual(response.status_code, 201)
        self.assertEqual(response.data["name"], "New Boat")
        self.assertEqual(Vessel.objects.count(), 2)

    def test_vessel_detail(self):
        """Test getting vessel detail."""
        response = self.client.get(f"/api/tracks/vessels/{self.vessel.id}/")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.data["name"], "Test Yacht")

    def test_vessel_update(self):
        """Test updating a vessel."""
        data = {"name": "Updated Yacht", "vessel_type": "yacht"}
        response = self.client.put(
            f"/api/tracks/vessels/{self.vessel.id}/", data, format="json"
        )
        self.assertEqual(response.status_code, 200)
        self.vessel.refresh_from_db()
        self.assertEqual(self.vessel.name, "Updated Yacht")

    def test_vessel_delete(self):
        """Test deleting a vessel."""
        response = self.client.delete(f"/api/tracks/vessels/{self.vessel.id}/")
        self.assertEqual(response.status_code, 204)
        self.assertEqual(Vessel.objects.count(), 0)

    def test_person_list(self):
        """Test listing people."""
        response = self.client.get("/api/tracks/people/")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(response.data["results"]), 1)

    def test_person_create(self):
        """Test creating a person."""
        data = {"first_name": "Bob", "last_name": "Jones", "email": "bob@example.com"}
        response = self.client.post("/api/tracks/people/", data)
        self.assertEqual(response.status_code, 201)
        self.assertEqual(response.data["first_name"], "Bob")

    def test_mode_list_filter_by_vessel(self):
        """Test filtering modes by vessel."""
        Mode.objects.create(
            vessel=self.vessel,
            mode="SAIL",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        response = self.client.get(f"/api/tracks/modes/?vessel={self.vessel.id}")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(response.data["results"]), 1)
        self.assertEqual(response.data["results"][0]["mode"], "SAIL")

    def test_mode_create(self):
        """Test creating a mode."""
        data = {
            "vessel": self.vessel.id,
            "mode": "MOTOR",
            "started_at": "2026-01-04T10:00:00Z",
        }
        response = self.client.post("/api/tracks/modes/", data, format="json")
        self.assertEqual(response.status_code, 201)
        self.assertEqual(response.data["mode"], "MOTOR")

    def test_annotation_list_filter_by_vessel(self):
        """Test filtering annotations by vessel."""
        Annotation.objects.create(
            vessel=self.vessel,
            type="TACK",
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )
        response = self.client.get(f"/api/tracks/annotations/?vessel={self.vessel.id}")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(response.data["results"]), 1)
        self.assertEqual(response.data["results"][0]["type"], "TACK")

    def test_annotation_filter_by_type(self):
        """Test filtering annotations by type."""
        Annotation.objects.create(
            vessel=self.vessel,
            type="TACK",
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )
        Annotation.objects.create(
            vessel=self.vessel,
            type="GYBE",
            timestamp=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
        )
        response = self.client.get("/api/tracks/annotations/?type=TACK")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(response.data["results"]), 1)
        self.assertEqual(response.data["results"][0]["type"], "TACK")

    def test_aboard_list_filter_current(self):
        """Test filtering aboard records by current status."""
        Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
        )
        Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 5, 10, 0, tzinfo=timezone.utc),
        )
        response = self.client.get("/api/tracks/aboard/?current=true")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(response.data["results"]), 1)
        self.assertTrue(response.data["results"][0]["is_currently_aboard"])

    def test_aboard_create_rejects_overlap_for_same_person(self):
        """Posting an aboard record that overlaps an existing one for the same person fails with 400."""
        Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
        )
        data = {
            "vessel": self.vessel.id,
            "person": self.person.id,
            "started_at": "2026-01-04T13:00:00Z",
            "ended_at": "2026-01-04T15:00:00Z",
        }
        response = self.client.post("/api/tracks/aboard/", data, format="json")
        self.assertEqual(response.status_code, 400)

    def test_aboard_allows_overlap_for_different_people(self):
        """Two people can be aboard at the same time."""
        other = Person.objects.create(first_name="Bob", last_name="Jones")
        Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
        )
        data = {
            "vessel": self.vessel.id,
            "person": other.id,
            "started_at": "2026-01-04T10:00:00Z",
            "ended_at": "2026-01-04T14:00:00Z",
        }
        response = self.client.post("/api/tracks/aboard/", data, format="json")
        self.assertEqual(response.status_code, 201)

    def test_aboard_update_does_not_reject_self(self):
        """PATCHing an existing aboard record should not see itself as an overlap."""
        record = Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
        )
        response = self.client.patch(
            f"/api/tracks/aboard/{record.id}/",
            {"notes": "edited"},
            format="json",
        )
        self.assertEqual(response.status_code, 200)

    def test_journey_list_filter_by_vessel(self):
        """Test filtering journeys by vessel."""
        Journey.objects.create(
            vessel=self.vessel,
            name="Test Journey",
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
        )
        response = self.client.get(f"/api/tracks/journeys/?vessel={self.vessel.id}")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(response.data["results"]), 1)

    def test_journey_detail_with_legs(self):
        """Test journey detail includes nested legs."""
        journey = Journey.objects.create(
            vessel=self.vessel,
            name="Test Journey",
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
        )
        Leg.objects.create(
            journey=journey,
            name="First Leg",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        response = self.client.get(f"/api/tracks/journeys/{journey.id}/")
        self.assertEqual(response.status_code, 200)
        self.assertIn("legs", response.data)
        self.assertEqual(len(response.data["legs"]), 1)

    def test_journey_detail_includes_statistics(self):
        """Test journey detail includes computed statistics."""
        from django.contrib.gis.geos import Point

        from tracks.models import Track, TrackPoint

        journey = Journey.objects.create(
            vessel=self.vessel,
            name="Test Journey",
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
        )
        track = Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )
        TrackPoint.objects.create(
            track=track,
            location=Point(172.72, -43.60, srid=4326),
            speed=5.0,
            timestamp=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        TrackPoint.objects.create(
            track=track,
            location=Point(172.63, -43.63, srid=4326),
            speed=6.0,
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )

        response = self.client.get(f"/api/tracks/journeys/{journey.id}/")
        self.assertEqual(response.status_code, 200)
        self.assertIn("statistics", response.data)
        stats = response.data["statistics"]
        self.assertIn("total_distance_nm", stats)
        self.assertIn("total_duration_seconds", stats)
        self.assertIn("max_speed_knots", stats)
        self.assertIn("avg_speed_knots", stats)
        self.assertIn("by_mode", stats)
        self.assertGreater(stats["total_distance_nm"], 0)

    def test_journey_list_includes_distance_nm(self):
        """Test journey list API response includes on-water distance."""
        from django.contrib.gis.geos import Point

        from tracks.models import Track, TrackPoint

        journey = Journey.objects.create(
            vessel=self.vessel,
            name="Test Journey",
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
        )
        track = Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
        )
        TrackPoint.objects.create(
            track=track,
            location=Point(172.72, -43.60, srid=4326),
            speed=5.0,
            timestamp=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        TrackPoint.objects.create(
            track=track,
            location=Point(172.63, -43.63, srid=4326),
            speed=6.0,
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )
        response = self.client.get("/api/tracks/journeys/")
        self.assertEqual(response.status_code, 200)
        j = response.data["results"][0]
        self.assertIn("distance_nm", j)
        self.assertIsInstance(j["distance_nm"], float)
        self.assertGreater(j["distance_nm"], 0)

    def test_journey_create_with_manual_route(self):
        """POST /journeys/ accepts manual_route GeoJSON LineString."""
        data = {
            "vessel": self.vessel.id,
            "name": "Manual Journey",
            "started_at": "2026-02-10T00:00:00Z",
            "ended_at": "2026-02-10T23:59:59Z",
            "manual_route": {
                "type": "LineString",
                "coordinates": [
                    [174.78, -41.29],
                    [174.82, -41.30],
                    [174.85, -41.28],
                ],
            },
        }
        response = self.client.post("/api/tracks/journeys/", data, format="json")
        self.assertEqual(response.status_code, 201, response.data)
        self.assertEqual(
            response.data["manual_route"]["type"], "LineString"
        )
        self.assertEqual(
            len(response.data["manual_route"]["coordinates"]), 3
        )
        self.assertIsInstance(response.data["estimated_distance_nm"], float)
        self.assertGreater(response.data["estimated_distance_nm"], 0)
        self.assertFalse(Mode.objects.filter(vessel=self.vessel).exists())

    def test_journey_manual_route_rejects_non_linestring(self):
        """Invalid geometry types are rejected with 400."""
        data = {
            "vessel": self.vessel.id,
            "name": "Bad Journey",
            "started_at": "2026-02-11T00:00:00Z",
            "ended_at": "2026-02-11T23:59:59Z",
            "manual_route": {
                "type": "Point",
                "coordinates": [174.78, -41.29],
            },
        }
        response = self.client.post("/api/tracks/journeys/", data, format="json")
        self.assertEqual(response.status_code, 400)

    def test_journey_estimated_distance_null_without_route(self):
        """estimated_distance_nm is null for journeys without manual_route."""
        journey = Journey.objects.create(
            vessel=self.vessel,
            name="GPS Journey",
            started_at=datetime(2026, 2, 12, 9, 0, tzinfo=timezone.utc),
        )
        response = self.client.get(f"/api/tracks/journeys/{journey.id}/")
        self.assertEqual(response.status_code, 200)
        self.assertIsNone(response.data["estimated_distance_nm"])
        self.assertIsNone(response.data["manual_route"])

    def test_journey_tracks_endpoint_includes_manual_route(self):
        """Mixed-geometry FeatureCollection: LineString feature for manual_route."""
        from django.contrib.gis.geos import LineString

        journey = Journey.objects.create(
            vessel=self.vessel,
            name="Manual Journey",
            started_at=datetime(2026, 2, 13, 0, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 2, 13, 23, 59, 59, tzinfo=timezone.utc),
            manual_route=LineString(
                (174.78, -41.29), (174.82, -41.30), srid=4326
            ),
        )
        response = self.client.get(f"/api/tracks/journeys/{journey.id}/tracks/")
        self.assertEqual(response.status_code, 200)
        line_features = [
            f for f in response.data["features"]
            if f["geometry"]["type"] == "LineString"
        ]
        self.assertEqual(len(line_features), 1)
        self.assertEqual(line_features[0]["properties"]["kind"], "manual_route")

    def test_journey_create_auto_creates_sailing_mode(self):
        """Creating a journey via API auto-creates a default Sailing mode."""
        data = {
            "vessel": self.vessel.id,
            "name": "New Journey",
            "started_at": "2026-02-01T09:00:00Z",
            "ended_at": "2026-02-01T15:00:00Z",
        }
        response = self.client.post("/api/tracks/journeys/", data, format="json")
        self.assertEqual(response.status_code, 201)
        modes = Mode.objects.filter(vessel=self.vessel, mode="SAIL")
        self.assertEqual(modes.count(), 1)
        self.assertEqual(
            modes.first().started_at,
            datetime(2026, 2, 1, 9, 0, tzinfo=timezone.utc),
        )

    def test_leg_filter_by_journey(self):
        """Test filtering legs by journey."""
        journey = Journey.objects.create(
            vessel=self.vessel,
            name="Test Journey",
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
        )
        Leg.objects.create(
            journey=journey,
            name="First Leg",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        response = self.client.get(f"/api/tracks/legs/?journey={journey.id}")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(response.data["results"]), 1)

    def test_person_history_endpoint(self):
        """Test person history custom action."""
        Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        response = self.client.get(f"/api/tracks/people/{self.person.id}/history/")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(response.data), 1)

    def test_pagination(self):
        """Test that pagination is working."""
        # Create many vessels
        for i in range(150):
            Vessel.objects.create(name=f"Vessel {i}")

        response = self.client.get("/api/tracks/vessels/")
        self.assertEqual(response.status_code, 200)
        self.assertIn("count", response.data)
        self.assertIn("next", response.data)
        self.assertIn("previous", response.data)
        self.assertEqual(len(response.data["results"]), 100)  # PAGE_SIZE

    def test_ordering(self):
        """Test ordering functionality."""
        Vessel.objects.create(name="Alpha")
        Vessel.objects.create(name="Zulu")

        response = self.client.get("/api/tracks/vessels/?ordering=name")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.data["results"][0]["name"], "Alpha")

        response = self.client.get("/api/tracks/vessels/?ordering=-name")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.data["results"][0]["name"], "Zulu")

    def test_search(self):
        """Test search functionality."""
        Vessel.objects.create(name="Sailboat", vessel_type="yacht")
        Vessel.objects.create(name="Powerboat", vessel_type="motorboat")

        response = self.client.get("/api/tracks/vessels/?search=Sail")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(response.data["results"]), 1)
        self.assertEqual(response.data["results"][0]["name"], "Sailboat")

    def test_journey_detail_endpoint(self):
        """Test journey detail endpoint with all related data."""
        journey = Journey.objects.create(
            vessel=self.vessel,
            name="Test Journey",
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 16, 0, tzinfo=timezone.utc),
        )

        # Create related data
        Mode.objects.create(
            vessel=self.vessel,
            mode="SAIL",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        Annotation.objects.create(
            vessel=self.vessel,
            type="TACK",
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )
        Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
        )

        response = self.client.get(f"/api/tracks/journeys/{journey.id}/modes/")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(response.data), 1)

        response = self.client.get(f"/api/tracks/journeys/{journey.id}/annotations/")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(response.data), 1)

        response = self.client.get(f"/api/tracks/journeys/{journey.id}/aboard/")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(len(response.data), 1)

    def test_journey_tracks_geojson(self):
        """Test journey tracks endpoint returns GeoJSON."""
        from django.contrib.gis.geos import Point

        from tracks.models import Track, TrackPoint

        journey = Journey.objects.create(
            vessel=self.vessel,
            name="Test Journey",
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 16, 0, tzinfo=timezone.utc),
        )

        # Create a track with points
        track = Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
        )
        TrackPoint.objects.create(
            track=track,
            location=Point(172.5, -43.5, srid=4326),
            timestamp=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            speed=5.5,
        )
        TrackPoint.objects.create(
            track=track,
            location=Point(172.6, -43.6, srid=4326),
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
            speed=6.0,
        )

        response = self.client.get(f"/api/tracks/journeys/{journey.id}/tracks/")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.data["type"], "FeatureCollection")
        self.assertIn("features", response.data)
        self.assertEqual(len(response.data["features"]), 2)

        # Check first feature
        feature = response.data["features"][0]
        self.assertEqual(feature["type"], "Feature")
        self.assertEqual(feature["geometry"]["type"], "Point")
        self.assertEqual(len(feature["geometry"]["coordinates"]), 2)
        self.assertIn("timestamp", feature["properties"])
        self.assertIn("speed", feature["properties"])
        self.assertEqual(feature["properties"]["track_id"], track.id)


class AnnotationModeUpsertTestCase(TestCase):
    """Issue #100: POSTing mode/annotation at an existing (vessel, timestamp)
    updates in place instead of 400/duplicating."""

    def setUp(self):
        self.client = APIClient()
        self.vessel = Vessel.objects.create(name="Upsert Yacht", vessel_type="yacht")
        self.ts = datetime(2026, 2, 1, 12, 0, tzinfo=timezone.utc)

    def test_mode_create_twice_upserts(self):
        first = {
            "vessel": self.vessel.id,
            "mode": "SAIL",
            "started_at": self.ts.isoformat(),
            "notes": "initial",
        }
        r1 = self.client.post("/api/tracks/modes/", first, format="json")
        self.assertEqual(r1.status_code, 201)
        mode_id = r1.data["id"]

        second = {**first, "mode": "MOTOR", "notes": "changed"}
        r2 = self.client.post("/api/tracks/modes/", second, format="json")
        self.assertEqual(r2.status_code, 200)
        self.assertEqual(r2.data["id"], mode_id)

        self.assertEqual(Mode.objects.count(), 1)
        mode = Mode.objects.get()
        self.assertEqual(mode.mode, "MOTOR")
        self.assertEqual(mode.notes, "changed")

    def test_annotation_create_twice_upserts_same_type(self):
        first = {
            "vessel": self.vessel.id,
            "type": "TACK",
            "timestamp": self.ts.isoformat(),
            "notes": "first",
        }
        r1 = self.client.post("/api/tracks/annotations/", first, format="json")
        self.assertEqual(r1.status_code, 201)
        ann_id = r1.data["id"]

        second = {**first, "notes": "second"}
        r2 = self.client.post("/api/tracks/annotations/", second, format="json")
        self.assertEqual(r2.status_code, 200)
        self.assertEqual(r2.data["id"], ann_id)

        self.assertEqual(Annotation.objects.count(), 1)
        self.assertEqual(Annotation.objects.get().notes, "second")

    def test_annotation_different_types_at_same_timestamp_coexist(self):
        base = {"vessel": self.vessel.id, "timestamp": self.ts.isoformat()}
        r1 = self.client.post(
            "/api/tracks/annotations/", {**base, "type": "TACK"}, format="json"
        )
        r2 = self.client.post(
            "/api/tracks/annotations/", {**base, "type": "GYBE"}, format="json"
        )
        self.assertEqual(r1.status_code, 201)
        self.assertEqual(r2.status_code, 201)
        self.assertEqual(Annotation.objects.count(), 2)

    def test_journey_create_at_existing_mode_timestamp_does_not_error(self):
        """Guard added in JourneyViewSet.perform_create: auto-created SAIL mode
        must not IntegrityError when a mode already exists at the same instant."""
        Mode.objects.create(vessel=self.vessel, mode="MOTOR", started_at=self.ts)
        data = {
            "vessel": self.vessel.id,
            "name": "J1",
            "started_at": self.ts.isoformat(),
            "ended_at": (self.ts.replace(hour=14)).isoformat(),
        }
        r = self.client.post("/api/tracks/journeys/", data, format="json")
        self.assertEqual(r.status_code, 201)
        self.assertEqual(Mode.objects.filter(started_at=self.ts).count(), 1)
        self.assertEqual(Mode.objects.get(started_at=self.ts).mode, "MOTOR")


class TrackPointDestroyTestCase(TestCase):
    """Issue #100: hard-delete a GPS point and cascade annotations/modes at
    the exact same (vessel, timestamp)."""

    def setUp(self):
        from django.contrib.gis.geos import Point
        from tracks.models import Track, TrackPoint

        self.client = APIClient()
        self.vessel = Vessel.objects.create(name="DelVessel")
        self.other_vessel = Vessel.objects.create(name="OtherVessel")
        self.track = Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 3, 1, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 3, 1, 12, 0, tzinfo=timezone.utc),
        )
        self.ts = datetime(2026, 3, 1, 10, 30, tzinfo=timezone.utc)
        self.other_ts = datetime(2026, 3, 1, 10, 31, tzinfo=timezone.utc)
        self.point = TrackPoint.objects.create(
            track=self.track, location=Point(172.5, -43.5, srid=4326), timestamp=self.ts
        )
        self.other_point = TrackPoint.objects.create(
            track=self.track,
            location=Point(172.6, -43.6, srid=4326),
            timestamp=self.other_ts,
        )

    def test_destroy_returns_204_and_removes_point(self):
        from tracks.models import TrackPoint

        r = self.client.delete(f"/api/tracks/track-points/{self.point.id}/")
        self.assertEqual(r.status_code, 204)
        self.assertFalse(TrackPoint.objects.filter(id=self.point.id).exists())
        self.assertTrue(TrackPoint.objects.filter(id=self.other_point.id).exists())

    def test_destroy_cascades_annotations_and_modes_at_same_timestamp(self):
        at_point = Annotation.objects.create(
            vessel=self.vessel, type="TACK", timestamp=self.ts
        )
        not_at_point = Annotation.objects.create(
            vessel=self.vessel, type="TACK", timestamp=self.other_ts
        )
        mode_at_point = Mode.objects.create(
            vessel=self.vessel, mode="SAIL", started_at=self.ts
        )
        mode_not_at_point = Mode.objects.create(
            vessel=self.vessel, mode="MOTOR", started_at=self.other_ts
        )

        r = self.client.delete(f"/api/tracks/track-points/{self.point.id}/")
        self.assertEqual(r.status_code, 204)

        self.assertFalse(Annotation.objects.filter(id=at_point.id).exists())
        self.assertFalse(Mode.objects.filter(id=mode_at_point.id).exists())
        self.assertTrue(Annotation.objects.filter(id=not_at_point.id).exists())
        self.assertTrue(Mode.objects.filter(id=mode_not_at_point.id).exists())

    def test_destroy_does_not_touch_other_vessels(self):
        other_ann = Annotation.objects.create(
            vessel=self.other_vessel, type="TACK", timestamp=self.ts
        )
        other_mode = Mode.objects.create(
            vessel=self.other_vessel, mode="SAIL", started_at=self.ts
        )

        r = self.client.delete(f"/api/tracks/track-points/{self.point.id}/")
        self.assertEqual(r.status_code, 204)

        self.assertTrue(Annotation.objects.filter(id=other_ann.id).exists())
        self.assertTrue(Mode.objects.filter(id=other_mode.id).exists())
