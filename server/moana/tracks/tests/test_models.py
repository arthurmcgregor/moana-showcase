import io
from datetime import datetime, timedelta, timezone

from django.contrib.gis.geos import LineString, Point
from django.test import TestCase
from django.urls import reverse

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
from tracks.statistics import journey_stats


class VesselModelTestCase(TestCase):
    def test_create_vessel(self):
        """Test creating a vessel."""
        vessel = Vessel.objects.create(
            name="Test Yacht", vessel_type="yacht", description="A test yacht"
        )
        self.assertEqual(vessel.name, "Test Yacht")
        self.assertEqual(vessel.vessel_type, "yacht")
        self.assertEqual(str(vessel), "Test Yacht")

    def test_vessel_cascade_delete(self):
        """Test that deleting a vessel deletes all related objects."""
        vessel = Vessel.objects.create(name="Test Boat")

        # Create related objects
        track = Track.objects.create(
            vessel=vessel,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
        )
        Mode.objects.create(
            vessel=vessel,
            mode="SAIL",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        Annotation.objects.create(
            vessel=vessel,
            type="TACK",
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )
        person = Person.objects.create(first_name="John", last_name="Doe")
        Aboard.objects.create(
            vessel=vessel,
            person=person,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        Journey.objects.create(
            vessel=vessel,
            name="Test Journey",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )

        # Delete vessel
        vessel.delete()

        # Check all related objects are deleted
        self.assertEqual(Track.objects.count(), 0)
        self.assertEqual(Mode.objects.count(), 0)
        self.assertEqual(Annotation.objects.count(), 0)
        self.assertEqual(Aboard.objects.count(), 0)
        self.assertEqual(Journey.objects.count(), 0)
        # Person should not be deleted
        self.assertEqual(Person.objects.count(), 1)


class PersonModelTestCase(TestCase):
    def test_create_person(self):
        """Test creating a person."""
        person = Person.objects.create(
            first_name="Alice",
            last_name="Smith",
            email="alice@example.com",
            notes="Test person",
        )
        self.assertEqual(person.first_name, "Alice")
        self.assertEqual(person.last_name, "Smith")
        self.assertEqual(str(person), "Alice Smith")

    def test_person_without_email(self):
        """Test creating a person without email."""
        person = Person.objects.create(first_name="Bob", last_name="Jones")
        self.assertEqual(person.email, "")

    def test_person_ordering(self):
        """Test that people are ordered by last name, first name."""
        Person.objects.create(first_name="Charlie", last_name="Brown")
        Person.objects.create(first_name="Alice", last_name="Brown")
        Person.objects.create(first_name="Bob", last_name="Anderson")

        people = list(Person.objects.all())
        self.assertEqual(people[0].last_name, "Anderson")
        self.assertEqual(people[1].first_name, "Alice")
        self.assertEqual(people[2].first_name, "Charlie")


class ModeModelTestCase(TestCase):
    def setUp(self):
        self.vessel = Vessel.objects.create(name="Test Boat")

    def test_create_mode(self):
        """Test creating a mode."""
        mode = Mode.objects.create(
            vessel=self.vessel,
            mode="SAIL",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            metadata={"reef_level": 0},
            notes="Test mode",
        )
        self.assertEqual(mode.mode, "SAIL")
        self.assertEqual(mode.get_mode_display(), "Sailing")
        self.assertIn("Sailing", str(mode))

    def test_mode_get_duration(self):
        """Test mode duration calculation."""
        mode1 = Mode.objects.create(
            vessel=self.vessel,
            mode="MOTOR",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        mode2 = Mode.objects.create(
            vessel=self.vessel,
            mode="SAIL",
            started_at=datetime(2026, 1, 4, 10, 30, tzinfo=timezone.utc),
        )

        duration = mode1.get_duration()
        self.assertEqual(duration, timedelta(minutes=30))

    def test_mode_get_ended_at(self):
        """Test getting implied end time."""
        mode1 = Mode.objects.create(
            vessel=self.vessel,
            mode="MOTOR",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        mode2 = Mode.objects.create(
            vessel=self.vessel,
            mode="SAIL",
            started_at=datetime(2026, 1, 4, 10, 30, tzinfo=timezone.utc),
        )

        ended_at = mode1.get_ended_at()
        self.assertEqual(ended_at, mode2.started_at)

    def test_mode_get_track(self):
        """Test getting associated track."""
        track = Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
        )
        mode = Mode.objects.create(
            vessel=self.vessel,
            mode="SAIL",
            started_at=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )

        self.assertEqual(mode.get_track(), track)

    def test_mode_without_track(self):
        """Test mode without associated track."""
        mode = Mode.objects.create(
            vessel=self.vessel,
            mode="TRAILERED",
            started_at=datetime(2026, 1, 4, 8, 0, tzinfo=timezone.utc),
        )

        self.assertIsNone(mode.get_track())

    def test_mode_ordering(self):
        """Test that modes are ordered by started_at."""
        mode2 = Mode.objects.create(
            vessel=self.vessel,
            mode="SAIL",
            started_at=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )
        mode1 = Mode.objects.create(
            vessel=self.vessel,
            mode="MOTOR",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )

        modes = list(Mode.objects.all())
        self.assertEqual(modes[0], mode1)
        self.assertEqual(modes[1], mode2)


class AnnotationModelTestCase(TestCase):
    def setUp(self):
        self.vessel = Vessel.objects.create(name="Test Boat")

    def test_create_annotation(self):
        """Test creating an annotation."""
        annotation = Annotation.objects.create(
            vessel=self.vessel,
            type="TACK",
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
            metadata={"from_tack": "port", "to_tack": "starboard"},
            notes="Clean tack",
        )
        self.assertEqual(annotation.type, "TACK")
        self.assertEqual(annotation.get_type_display(), "Tack")
        self.assertIn("Tack", str(annotation))

    def test_annotation_get_track(self):
        """Test getting associated track."""
        track = Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
        )
        annotation = Annotation.objects.create(
            vessel=self.vessel,
            type="TACK",
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )

        self.assertEqual(annotation.get_track(), track)

    def test_annotation_without_track(self):
        """Test annotation without associated track."""
        annotation = Annotation.objects.create(
            vessel=self.vessel,
            type="NOTE",
            timestamp=datetime(2026, 1, 4, 8, 0, tzinfo=timezone.utc),
            notes="Boat maintenance",
        )

        self.assertIsNone(annotation.get_track())

    def test_annotation_get_current_mode(self):
        """Test getting current mode at annotation time."""
        Mode.objects.create(
            vessel=self.vessel,
            mode="MOTOR",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        sail_mode = Mode.objects.create(
            vessel=self.vessel,
            mode="SAIL",
            started_at=datetime(2026, 1, 4, 10, 30, tzinfo=timezone.utc),
        )
        annotation = Annotation.objects.create(
            vessel=self.vessel,
            type="TACK",
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )

        self.assertEqual(annotation.get_current_mode(), sail_mode)

    def test_annotation_ordering(self):
        """Test that annotations are ordered by timestamp."""
        ann2 = Annotation.objects.create(
            vessel=self.vessel,
            type="GYBE",
            timestamp=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
        )
        ann1 = Annotation.objects.create(
            vessel=self.vessel,
            type="TACK",
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )

        annotations = list(Annotation.objects.all())
        self.assertEqual(annotations[0], ann1)
        self.assertEqual(annotations[1], ann2)


class AboardModelTestCase(TestCase):
    def setUp(self):
        self.vessel = Vessel.objects.create(name="Test Boat")
        self.person = Person.objects.create(first_name="Alice", last_name="Smith")

    def test_create_aboard(self):
        """Test creating an aboard record."""
        aboard = Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
            notes="Test aboard",
        )
        self.assertEqual(aboard.vessel, self.vessel)
        self.assertEqual(aboard.person, self.person)
        self.assertIn("Alice Smith", str(aboard))

    def test_aboard_is_currently_aboard(self):
        """Test checking if person is currently aboard."""
        aboard1 = Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
        )
        aboard2 = Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 5, 10, 0, tzinfo=timezone.utc),
        )

        self.assertFalse(aboard1.is_currently_aboard())
        self.assertTrue(aboard2.is_currently_aboard())

    def test_aboard_get_duration(self):
        """Test calculating duration aboard."""
        aboard1 = Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
        )
        aboard2 = Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 5, 10, 0, tzinfo=timezone.utc),
        )

        self.assertEqual(aboard1.get_duration(), timedelta(hours=4))
        self.assertIsNone(aboard2.get_duration())

    def test_aboard_get_track(self):
        """Test getting associated track."""
        track = Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
        )
        aboard = Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 13, 0, tzinfo=timezone.utc),
        )

        self.assertEqual(aboard.get_track(), track)

    def test_aboard_ordering(self):
        """Test that aboard records are ordered by started_at."""
        aboard2 = Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 5, 10, 0, tzinfo=timezone.utc),
        )
        aboard1 = Aboard.objects.create(
            vessel=self.vessel,
            person=self.person,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )

        aboard_records = list(Aboard.objects.all())
        self.assertEqual(aboard_records[0], aboard1)
        self.assertEqual(aboard_records[1], aboard2)


class JourneyModelTestCase(TestCase):
    def setUp(self):
        self.vessel = Vessel.objects.create(name="Test Boat")

    def test_create_journey(self):
        """Test creating a journey."""
        journey = Journey.objects.create(
            vessel=self.vessel,
            name="Saturday Sail",
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 16, 0, tzinfo=timezone.utc),
            description="Test journey",
            notes="Test notes",
        )
        self.assertEqual(journey.name, "Saturday Sail")
        self.assertIn("Saturday Sail", str(journey))
        self.assertIn("2026-01-04", str(journey))

    def test_journey_manual_route_roundtrip(self):
        """Journey can be saved with a manual_route LineString and without."""
        journey_with_route = Journey.objects.create(
            vessel=self.vessel,
            name="Manual Journey",
            started_at=datetime(2026, 1, 4, 0, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 23, 59, 59, tzinfo=timezone.utc),
            manual_route=LineString(
                (174.78, -41.29), (174.82, -41.30), (174.85, -41.28), srid=4326
            ),
        )
        journey_with_route.refresh_from_db()
        self.assertIsNotNone(journey_with_route.manual_route)
        self.assertEqual(len(journey_with_route.manual_route.coords), 3)

        journey_without = Journey.objects.create(
            vessel=self.vessel,
            name="GPS Journey",
            started_at=datetime(2026, 1, 5, 9, 0, tzinfo=timezone.utc),
        )
        journey_without.refresh_from_db()
        self.assertIsNone(journey_without.manual_route)

    def test_journey_get_duration(self):
        """Test journey duration calculation."""
        journey1 = Journey.objects.create(
            vessel=self.vessel,
            name="Day Trip",
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 16, 0, tzinfo=timezone.utc),
        )
        journey2 = Journey.objects.create(
            vessel=self.vessel,
            name="Ongoing Trip",
            started_at=datetime(2026, 1, 5, 9, 0, tzinfo=timezone.utc),
        )

        self.assertEqual(journey1.get_duration(), timedelta(hours=7))
        self.assertIsNone(journey2.get_duration())

    def test_journey_get_tracks(self):
        """Test getting tracks associated with journey."""
        journey = Journey.objects.create(
            vessel=self.vessel,
            name="Test Journey",
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 16, 0, tzinfo=timezone.utc),
        )
        track1 = Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
        )
        track2 = Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 4, 13, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 15, 0, tzinfo=timezone.utc),
        )
        # Track outside journey
        Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 5, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 5, 12, 0, tzinfo=timezone.utc),
        )

        tracks = journey.get_tracks()
        self.assertEqual(tracks.count(), 2)
        self.assertIn(track1, tracks)
        self.assertIn(track2, tracks)

    def test_journey_get_modes(self):
        """Test getting modes during journey."""
        journey = Journey.objects.create(
            vessel=self.vessel,
            name="Test Journey",
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 16, 0, tzinfo=timezone.utc),
        )
        mode1 = Mode.objects.create(
            vessel=self.vessel,
            mode="MOTOR",
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
        )
        mode2 = Mode.objects.create(
            vessel=self.vessel,
            mode="SAIL",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        # Mode outside journey
        Mode.objects.create(
            vessel=self.vessel,
            mode="TRAILERED",
            started_at=datetime(2026, 1, 5, 10, 0, tzinfo=timezone.utc),
        )

        modes = journey.get_modes()
        self.assertEqual(modes.count(), 2)
        self.assertIn(mode1, modes)
        self.assertIn(mode2, modes)

    def test_journey_ordering(self):
        """Test that journeys are ordered by started_at."""
        journey2 = Journey.objects.create(
            vessel=self.vessel,
            name="Second Journey",
            started_at=datetime(2026, 1, 5, 9, 0, tzinfo=timezone.utc),
        )
        journey1 = Journey.objects.create(
            vessel=self.vessel,
            name="First Journey",
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
        )

        journeys = list(Journey.objects.all())
        self.assertEqual(journeys[0], journey1)
        self.assertEqual(journeys[1], journey2)


class LegModelTestCase(TestCase):
    def setUp(self):
        self.vessel = Vessel.objects.create(name="Test Boat")
        self.journey = Journey.objects.create(
            vessel=self.vessel,
            name="Test Journey",
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 16, 0, tzinfo=timezone.utc),
        )

    def test_create_leg(self):
        """Test creating a leg."""
        leg = Leg.objects.create(
            journey=self.journey,
            name="Sail to Island",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
            notes="Test leg",
        )
        self.assertEqual(leg.name, "Sail to Island")
        self.assertIn("Test Journey", str(leg))
        self.assertIn("Sail to Island", str(leg))

    def test_leg_get_duration(self):
        """Test leg duration calculation."""
        leg1 = Leg.objects.create(
            journey=self.journey,
            name="First Leg",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
        )
        leg2 = Leg.objects.create(
            journey=self.journey,
            name="Ongoing Leg",
            started_at=datetime(2026, 1, 4, 13, 0, tzinfo=timezone.utc),
        )

        self.assertEqual(leg1.get_duration(), timedelta(hours=2))
        self.assertIsNone(leg2.get_duration())

    def test_leg_get_modes(self):
        """Test getting modes during leg."""
        leg = Leg.objects.create(
            journey=self.journey,
            name="Sailing Leg",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
        )
        mode1 = Mode.objects.create(
            vessel=self.vessel,
            mode="SAIL",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        mode2 = Mode.objects.create(
            vessel=self.vessel,
            mode="ANCHOR",
            started_at=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )
        # Mode outside leg
        Mode.objects.create(
            vessel=self.vessel,
            mode="MOTOR",
            started_at=datetime(2026, 1, 4, 13, 0, tzinfo=timezone.utc),
        )

        modes = leg.get_modes()
        self.assertEqual(modes.count(), 2)
        self.assertIn(mode1, modes)
        self.assertIn(mode2, modes)

    def test_leg_get_tracks(self):
        """Test getting tracks during leg."""
        leg = Leg.objects.create(
            journey=self.journey,
            name="Test Leg",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
        )
        track = Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 4, 10, 30, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 11, 30, tzinfo=timezone.utc),
        )
        # Track outside leg
        Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 4, 13, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
        )

        tracks = leg.get_tracks()
        self.assertEqual(tracks.count(), 1)
        self.assertIn(track, tracks)

    def test_leg_get_aboard(self):
        """Test getting aboard records during leg."""
        leg = Leg.objects.create(
            journey=self.journey,
            name="Test Leg",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
        )
        person = Person.objects.create(first_name="Alice", last_name="Smith")
        aboard = Aboard.objects.create(
            vessel=self.vessel,
            person=person,
            started_at=datetime(2026, 1, 4, 9, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 13, 0, tzinfo=timezone.utc),
        )

        aboard_records = leg.get_aboard()
        self.assertEqual(aboard_records.count(), 1)
        self.assertIn(aboard, aboard_records)

    def test_leg_ordering(self):
        """Test that legs are ordered by started_at."""
        leg2 = Leg.objects.create(
            journey=self.journey,
            name="Second Leg",
            started_at=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
        )
        leg1 = Leg.objects.create(
            journey=self.journey,
            name="First Leg",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )

        legs = list(Leg.objects.all())
        self.assertEqual(legs[0], leg1)
        self.assertEqual(legs[1], leg2)


class TrackModelTestCase(TestCase):
    def setUp(self):
        self.vessel = Vessel.objects.create(name="Test Boat")

    def test_create_track(self):
        """Test creating a track."""
        track = Track.objects.create(
            vessel=self.vessel,
            name="Morning Sail",
            description="Test track",
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
        )
        self.assertEqual(track.name, "Morning Sail")
        self.assertEqual(str(track), "Morning Sail")

    def test_track_without_name(self):
        """Test track display without name."""
        track = Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        self.assertIn(f"Track {track.pk}", str(track))

    def test_track_with_points(self):
        """Test track with track points."""
        track = Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 14, 0, tzinfo=timezone.utc),
        )
        TrackPoint.objects.create(
            track=track,
            location=Point(172.5, -43.5, srid=4326),
            timestamp=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        TrackPoint.objects.create(
            track=track,
            location=Point(172.6, -43.6, srid=4326),
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )

        self.assertEqual(track.points.count(), 2)

    def test_trackpoint_ordering(self):
        """Test that track points are ordered by timestamp."""
        track = Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        point2 = TrackPoint.objects.create(
            track=track,
            location=Point(172.6, -43.6, srid=4326),
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )
        point1 = TrackPoint.objects.create(
            track=track,
            location=Point(172.5, -43.5, srid=4326),
            timestamp=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )

        points = list(track.points.all())
        self.assertEqual(points[0], point1)
        self.assertEqual(points[1], point2)


class StatisticsTestCase(TestCase):
    """Tests for calculate_time_range_statistics distance calculation."""

    def setUp(self):
        self.vessel = Vessel.objects.create(name="Test Boat")

    def test_distance_uses_geodesic_not_cartesian(self):
        """
        Regression test: distance must be computed as geodesic meters,
        not Cartesian degrees. Two points ~6 NM apart should yield a
        distance in that range, not near zero.
        """
        # Lyttelton (172.72, -43.60) to Diamond Harbour (172.63, -43.63)
        # Approx 6 NM apart
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
            speed=5.0,
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )

        stats = journey_stats.for_time_range(
            vessel_id=self.vessel.id,
            start=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            end=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
            modes=Mode.objects.none(),
        ).to_dict()

        # Should be roughly 4-5 NM, definitely not near zero
        self.assertGreater(stats["total_distance_nm"], 3.0)
        self.assertLess(stats["total_distance_nm"], 7.0)

    def test_statistics_with_multiple_points(self):
        """Test statistics across multiple track points."""
        track = Track.objects.create(
            vessel=self.vessel,
            started_at=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            ended_at=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
        )
        # Three points forming a path
        TrackPoint.objects.create(
            track=track,
            location=Point(172.70, -43.60, srid=4326),
            speed=3.0,
            timestamp=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
        )
        TrackPoint.objects.create(
            track=track,
            location=Point(172.71, -43.60, srid=4326),
            speed=4.0,
            timestamp=datetime(2026, 1, 4, 11, 0, tzinfo=timezone.utc),
        )
        TrackPoint.objects.create(
            track=track,
            location=Point(172.72, -43.60, srid=4326),
            speed=5.0,
            timestamp=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
        )

        stats = journey_stats.for_time_range(
            vessel_id=self.vessel.id,
            start=datetime(2026, 1, 4, 10, 0, tzinfo=timezone.utc),
            end=datetime(2026, 1, 4, 12, 0, tzinfo=timezone.utc),
            modes=Mode.objects.none(),
        ).to_dict()

        # 0.02 degrees longitude at lat -43.6 ≈ 0.87 NM
        self.assertGreater(stats["total_distance_nm"], 0.5)
        self.assertLess(stats["total_distance_nm"], 1.5)
        self.assertEqual(stats["total_duration_seconds"], 7200)
        self.assertEqual(stats["max_speed_knots"], 5.0)
        self.assertAlmostEqual(stats["avg_speed_knots"], 4.0)
