from django.contrib.gis.db import models

# Sentinel for Mode._precomputed_ended_at when the value is explicitly None.
_NO_END = object()


class Person(models.Model):
    """
    Individual person who can be aboard vessels.
    """

    first_name = models.CharField(max_length=100)
    last_name = models.CharField(max_length=100)
    email = models.EmailField(blank=True)
    notes = models.TextField(blank=True)
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name_plural = "people"
        ordering = ["last_name", "first_name"]

    def __str__(self):
        return f"{self.first_name} {self.last_name}"


class Vessel(models.Model):
    """
    Represents a boat/vessel. Top-level model for organizing tracks, modes, and annotations.
    """

    name = models.CharField(max_length=255, help_text="Name of the vessel")
    description = models.TextField(blank=True)
    vessel_type = models.CharField(
        max_length=100,
        blank=True,
        help_text="Type of vessel (e.g., dinghy, yacht, kayak)",
    )
    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return self.name


class Track(models.Model):
    """
    Represents a GPS track recording session for a vessel.
    """

    vessel = models.ForeignKey(
        Vessel,
        related_name="tracks",
        on_delete=models.CASCADE,
        null=True,
        blank=True,
        help_text="Vessel this track belongs to (can be assigned later)",
    )
    name = models.CharField(
        max_length=255, blank=True, help_text="Optional name for the track"
    )
    description = models.TextField(blank=True)
    started_at = models.DateTimeField(help_text="When the track recording started")
    ended_at = models.DateTimeField(
        null=True, blank=True, help_text="When the track recording ended"
    )
    source_filename = models.CharField(
        max_length=255,
        blank=True,
        default="",
        db_index=True,
        help_text="original device filename this track was imported from",
    )
    imported_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        indexes = [
            models.Index(fields=["vessel", "started_at", "ended_at"]),
        ]

    def __str__(self):
        return f"{self.name}" if self.name else f"Track {self.pk}"


class TrackPoint(models.Model):
    track = models.ForeignKey(Track, related_name="points", on_delete=models.CASCADE)
    location = models.PointField(geography=True)  # Stores (lng, lat)
    speed = models.FloatField(null=True, blank=True, help_text="Speed in m/s or km/h")
    satellites = models.IntegerField(null=True, blank=True)
    altitude = models.FloatField(null=True, blank=True, help_text="Altitude in meters")
    hdop = models.FloatField(
        null=True, blank=True, help_text="Horizontal dilution of precision"
    )
    course = models.FloatField(
        null=True, blank=True, help_text="Course over ground in degrees"
    )
    heel = models.FloatField(
        null=True, blank=True, help_text="Boat heel angle in degrees, + to starboard"
    )
    trim = models.FloatField(
        null=True, blank=True, help_text="Boat trim angle in degrees, + bow up"
    )
    heading = models.FloatField(
        null=True, blank=True, help_text="Magnetic heading in degrees, 0–360"
    )
    timestamp = models.DateTimeField(help_text="Timestamp of the GPS point")

    class Meta:
        ordering = ["timestamp"]
        indexes = [
            models.Index(fields=["track", "timestamp"]),
        ]

    def __str__(self):
        return f"Point at {self.timestamp} ({self.location})"


class Mode(models.Model):
    """
    Represents the boat's operating mode starting at a specific time.
    End time is implied by the start of the next mode.
    Associated track is derived from vessel + timestamp, not stored directly.
    """

    MODE_CHOICES = [
        ("SAIL", "Sailing"),
        ("MOTORSAIL", "Motor Sailing"),
        ("MOTOR", "Motoring"),
        ("ROW", "Rowing"),
        ("UNPOWERED", "Drifting/Unpowered"),
        ("ANCHOR", "Anchored"),
        ("ALONGSIDE", "Alongside/Docked"),
        ("BEACHED", "Beached"),
        ("TRAILERED", "On Trailer"),
        # Watch quick-mark placeholder (issue #139): "not sailing, not
        # motoring" set on the water, meant to be reclassified in the web UI.
        ("OTHER", "Other"),
    ]

    vessel = models.ForeignKey(Vessel, related_name="modes", on_delete=models.CASCADE)
    mode = models.CharField(max_length=20, choices=MODE_CHOICES)
    started_at = models.DateTimeField()
    metadata = models.JSONField(default=dict, blank=True)
    notes = models.TextField(blank=True)

    class Meta:
        ordering = ["started_at"]
        indexes = [
            models.Index(fields=["vessel", "started_at"]),
        ]
        unique_together = [["vessel", "started_at"]]

    def __str__(self):
        return f"{self.get_mode_display()} at {self.started_at}"

    def get_track(self):
        """Get the track (if any) that contains this mode's timestamp."""
        return Track.objects.filter(
            vessel=self.vessel,
            started_at__lte=self.started_at,
            ended_at__gte=self.started_at,
        ).first()

    def get_duration(self, end_boundary=None):
        """Calculate duration until next mode for this vessel.

        Args:
            end_boundary: Optional datetime to cap the end time (e.g. journey end).
        """
        ended_at = self.get_ended_at(end_boundary=end_boundary)
        if ended_at:
            return ended_at - self.started_at
        return None  # Ongoing/no end time

    def get_ended_at(self, end_boundary=None):
        """Get the implied end time.

        Args:
            end_boundary: Optional datetime to cap the end time (e.g. journey end).

        If ``_precomputed_ended_at`` has been set (e.g. by a viewset that
        pre-computes end times in bulk), it is used directly to avoid N+1
        queries.  The sentinel ``_NO_END`` means "already computed, but None".
        """
        precomputed = getattr(self, "_precomputed_ended_at", None)
        if precomputed is _NO_END:
            return None
        if precomputed is not None:
            end = precomputed
        else:
            next_mode = Mode.objects.filter(
                vessel=self.vessel, started_at__gt=self.started_at
            ).first()

            if next_mode:
                end = next_mode.started_at
            else:
                # Try to use associated track's end time
                track = self.get_track()
                if track and track.ended_at:
                    end = track.ended_at
                else:
                    return None

        if end_boundary and end > end_boundary:
            return end_boundary

        return end


def precompute_mode_ended_at(modes, tracks=None):
    """Set ``_precomputed_ended_at`` on each mode to avoid N+1 queries.

    Args:
        modes: iterable of Mode instances **sorted by started_at**.
        tracks: optional queryset/list of Track instances covering the time
            range.  Used as fallback for the last mode when there is no next
            mode.  If *None*, the last mode gets ``_NO_END``.
    """
    modes_list = list(modes)
    if not modes_list:
        return modes_list

    # Build a lookup of track end times for the fallback case
    track_end_map = {}
    if tracks is not None:
        for t in tracks:
            if t.ended_at and t.vessel_id:
                track_end_map[t.vessel_id] = max(
                    track_end_map.get(t.vessel_id, t.ended_at), t.ended_at
                )

    for i, mode in enumerate(modes_list):
        if i + 1 < len(modes_list):
            mode._precomputed_ended_at = modes_list[i + 1].started_at
        else:
            # Last mode — try track end time
            track_end = track_end_map.get(mode.vessel_id)
            if track_end:
                mode._precomputed_ended_at = track_end
            else:
                mode._precomputed_ended_at = _NO_END

    return modes_list


class Annotation(models.Model):
    """
    Point-in-time events or observations.
    Associated track is derived from vessel + timestamp, not stored directly.
    """

    TYPE_CHOICES = [
        # Maneuvers
        ("TACK", "Tack"),
        ("GYBE", "Gybe"),
        ("COURSE_CHANGE", "Course Change"),
        # Other
        ("NOTE", "General Note"),
    ]

    vessel = models.ForeignKey(
        Vessel, related_name="annotations", on_delete=models.CASCADE
    )
    type = models.CharField(max_length=30, choices=TYPE_CHOICES)
    timestamp = models.DateTimeField()
    metadata = models.JSONField(default=dict, blank=True)
    notes = models.TextField(blank=True)

    class Meta:
        ordering = ["timestamp"]
        indexes = [
            models.Index(fields=["vessel", "timestamp"]),
            models.Index(fields=["vessel", "type"]),
        ]
        unique_together = [["vessel", "type", "timestamp"]]

    def __str__(self):
        return f"{self.get_type_display()} at {self.timestamp}"

    def get_track(self):
        """Get the track (if any) that contains this annotation's timestamp."""
        return Track.objects.filter(
            vessel=self.vessel,
            started_at__lte=self.timestamp,
            ended_at__gte=self.timestamp,
        ).first()

    def get_current_mode(self):
        """Get the mode active at this annotation's timestamp for this vessel."""
        return (
            Mode.objects.filter(vessel=self.vessel, started_at__lte=self.timestamp)
            .order_by("-started_at")
            .first()
        )


class Aboard(models.Model):
    """
    Tracks who is aboard a vessel during a time period.
    End time is implied by disembarked_at (None = still aboard).
    """

    vessel = models.ForeignKey(Vessel, related_name="aboard", on_delete=models.CASCADE)
    person = models.ForeignKey(
        Person, related_name="aboard_records", on_delete=models.CASCADE
    )
    started_at = models.DateTimeField(help_text="When the person came aboard")
    ended_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="When the person went ashore (None = still aboard)",
    )
    notes = models.TextField(blank=True)

    class Meta:
        ordering = ["started_at"]
        verbose_name_plural = "aboard records"
        indexes = [
            models.Index(fields=["vessel", "started_at"]),
            models.Index(fields=["person", "started_at"]),
        ]

    def __str__(self):
        status = "aboard" if self.ended_at is None else "disembarked"
        return f"{self.person} {status} {self.vessel} at {self.started_at}"

    def is_currently_aboard(self):
        """Check if person is currently aboard (no end time)."""
        return self.ended_at is None

    def get_duration(self):
        """Calculate duration aboard."""
        if self.ended_at:
            return self.ended_at - self.started_at
        return None  # Still aboard

    def get_track(self):
        """Get the track (if any) that contains this aboard period."""
        # Find track that overlaps with this aboard period
        tracks = Track.objects.filter(
            vessel=self.vessel,
            started_at__lte=self.started_at,
        )

        if self.ended_at:
            tracks = tracks.filter(ended_at__gte=self.started_at)

        return tracks.first()


class Journey(models.Model):
    """
    A complete outing with the vessel.
    Typically from leaving storage to returning to storage.
    """

    vessel = models.ForeignKey(
        Vessel, related_name="journeys", on_delete=models.CASCADE
    )
    name = models.CharField(max_length=255, help_text="Name of the journey")
    started_at = models.DateTimeField(help_text="When the journey started")
    ended_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="When the journey ended (None = ongoing)",
    )
    description = models.TextField(blank=True)
    notes = models.TextField(blank=True)
    weather_station = models.ForeignKey(
        "WeatherStation",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="journeys",
        help_text="Preferred weather station for this journey",
    )
    manual_route = models.LineStringField(
        srid=4326,
        geography=True,
        null=True,
        blank=True,
        help_text="Manually-traced route for journeys without GPS data",
    )

    class Meta:
        ordering = ["started_at"]
        verbose_name_plural = "journeys"
        indexes = [
            models.Index(fields=["vessel", "started_at"]),
        ]

    def __str__(self):
        return f"{self.name} ({self.started_at.date()})"

    def get_duration(self):
        """Calculate journey duration."""
        if self.ended_at:
            return self.ended_at - self.started_at
        return None  # Ongoing

    def get_tracks(self):
        """Get all tracks that overlap with this journey."""
        tracks = Track.objects.filter(
            vessel=self.vessel,
            started_at__lte=self.ended_at if self.ended_at else models.F("started_at"),
        ).filter(
            models.Q(ended_at__isnull=True) | models.Q(ended_at__gte=self.started_at)
        )
        return tracks

    def get_modes(self):
        """Get all modes during this journey."""
        modes = Mode.objects.filter(
            vessel=self.vessel,
            started_at__gte=self.started_at,
        )
        if self.ended_at:
            modes = modes.filter(started_at__lte=self.ended_at)
        return modes

    def get_statistics(self):
        """
        Calculate statistics for this journey on-demand.
        Returns dict with calculated stats.
        """
        from .statistics import journey_stats

        return journey_stats.for_time_range(
            vessel_id=self.vessel_id,
            start=self.started_at,
            end=self.ended_at,
            modes=self.get_modes(),
        ).to_dict()


class Leg(models.Model):
    """
    A segment within a journey.
    Defined by timestamps, flexible segmentation.
    """

    journey = models.ForeignKey(Journey, related_name="legs", on_delete=models.CASCADE)
    name = models.CharField(max_length=255, help_text="Name of the leg")
    started_at = models.DateTimeField(help_text="When the leg started")
    ended_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="When the leg ended (None = ongoing)",
    )
    notes = models.TextField(blank=True)

    class Meta:
        ordering = ["started_at"]
        indexes = [
            models.Index(fields=["journey", "started_at"]),
        ]

    def __str__(self):
        return f"{self.journey.name} - {self.name}"

    def get_duration(self):
        """Calculate leg duration."""
        if self.ended_at:
            return self.ended_at - self.started_at
        return None  # Ongoing

    def get_modes(self):
        """Get all modes during this leg."""
        modes = Mode.objects.filter(
            vessel=self.journey.vessel,
            started_at__gte=self.started_at,
        )
        if self.ended_at:
            modes = modes.filter(started_at__lte=self.ended_at)
        return modes

    def get_tracks(self):
        """Get all tracks that overlap with this leg."""
        tracks = Track.objects.filter(
            vessel=self.journey.vessel,
            started_at__gte=self.started_at,
        )
        if self.ended_at:
            tracks = tracks.filter(started_at__lte=self.ended_at)
        return tracks

    def get_aboard(self):
        """Get all aboard records during this leg."""
        aboard = Aboard.objects.filter(
            vessel=self.journey.vessel,
            started_at__lte=self.started_at,
        )
        if self.ended_at:
            aboard = aboard.filter(
                models.Q(ended_at__isnull=True)
                | models.Q(ended_at__gte=self.started_at)
            )
        return aboard

    def get_statistics(self):
        """
        Calculate statistics for this leg on-demand.
        Returns dict with calculated stats.
        """
        from .statistics import journey_stats

        return journey_stats.for_leg(self).to_dict()


class WeatherStation(models.Model):
    name = models.CharField(max_length=255)
    location = models.PointField(geography=True, null=True, blank=True)
    description = models.TextField(blank=True)
    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return self.name


class WeatherObservation(models.Model):
    station = models.ForeignKey(
        WeatherStation, related_name="observations", on_delete=models.CASCADE
    )
    timestamp = models.DateTimeField()  # stored UTC
    temperature = models.FloatField(null=True, blank=True)      # °C
    rain_accumulated = models.FloatField(null=True, blank=True) # mm (10m acc)
    wind_direction = models.FloatField(null=True, blank=True)   # degrees
    wind_speed = models.FloatField(null=True, blank=True)       # knots
    gust_speed = models.FloatField(null=True, blank=True)       # knots
    humidity = models.FloatField(null=True, blank=True)         # %
    tide_observation = models.FloatField(null=True, blank=True) # meters (sparse)

    class Meta:
        ordering = ["timestamp"]
        indexes = [
            models.Index(fields=["station", "timestamp"]),
        ]
        unique_together = [["station", "timestamp"]]

    def __str__(self):
        return f"{self.station.name} at {self.timestamp}"
