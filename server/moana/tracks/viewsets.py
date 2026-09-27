import csv
import json
from datetime import datetime, timedelta, timezone as dt_timezone
from io import StringIO

from django.contrib.gis.db.models.functions import Length as GisLength
from django.contrib.gis.geos import Point

from django.db import models, transaction
from django.http import HttpResponse
from django.utils import timezone
from django_filters import rest_framework as filters
from drf_spectacular.types import OpenApiTypes
from drf_spectacular.utils import OpenApiParameter, extend_schema, inline_serializer
from rest_framework import serializers as drf_serializers
from rest_framework import status, viewsets
from rest_framework.decorators import action
from rest_framework.parsers import MultiPartParser
from rest_framework.response import Response

from .models import (
    Aboard,
    Annotation,
    Journey,
    Leg,
    Mode,
    Person,
    Track,
    TrackPoint,
    Vessel,
    WeatherObservation,
    WeatherStation,
)
from .serializers import (
    AboardSerializer,
    AnnotationSerializer,
    JourneyDetailSerializer,
    JourneySerializer,
    LegSerializer,
    ModeSerializer,
    PersonSerializer,
    TrackDetailSerializer,
    TrackPointSerializer,
    TrackSerializer,
    VesselSerializer,
    WeatherObservationSerializer,
    WeatherStationSerializer,
)


# Filters
class TrackFilter(filters.FilterSet):
    vessel = filters.NumberFilter(field_name="vessel__id")
    started_after = filters.DateTimeFilter(field_name="started_at", lookup_expr="gte")
    started_before = filters.DateTimeFilter(field_name="started_at", lookup_expr="lte")

    class Meta:
        model = Track
        fields = ["vessel", "started_after", "started_before"]


class ModeFilter(filters.FilterSet):
    vessel = filters.NumberFilter(field_name="vessel__id")
    mode = filters.CharFilter(field_name="mode")
    started_after = filters.DateTimeFilter(field_name="started_at", lookup_expr="gte")
    started_before = filters.DateTimeFilter(field_name="started_at", lookup_expr="lte")

    class Meta:
        model = Mode
        fields = ["vessel", "mode", "started_after", "started_before"]


class AnnotationFilter(filters.FilterSet):
    vessel = filters.NumberFilter(field_name="vessel__id")
    type = filters.CharFilter(field_name="type")
    timestamp_after = filters.DateTimeFilter(field_name="timestamp", lookup_expr="gte")
    timestamp_before = filters.DateTimeFilter(field_name="timestamp", lookup_expr="lte")

    class Meta:
        model = Annotation
        fields = ["vessel", "type", "timestamp_after", "timestamp_before"]


class AboardFilter(filters.FilterSet):
    vessel = filters.NumberFilter(field_name="vessel__id")
    person = filters.NumberFilter(field_name="person__id")
    current = filters.BooleanFilter(method="filter_current")
    started_after = filters.DateTimeFilter(field_name="started_at", lookup_expr="gte")
    started_before = filters.DateTimeFilter(field_name="started_at", lookup_expr="lte")

    class Meta:
        model = Aboard
        fields = ["vessel", "person", "current", "started_after", "started_before"]

    def filter_current(self, queryset, name, value):
        if value:
            return queryset.filter(ended_at__isnull=True)
        return queryset


class JourneyFilter(filters.FilterSet):
    vessel = filters.NumberFilter(field_name="vessel__id")
    started_after = filters.DateTimeFilter(field_name="started_at", lookup_expr="gte")
    started_before = filters.DateTimeFilter(field_name="started_at", lookup_expr="lte")
    ongoing = filters.BooleanFilter(method="filter_ongoing")

    class Meta:
        model = Journey
        fields = ["vessel", "started_after", "started_before", "ongoing"]

    def filter_ongoing(self, queryset, name, value):
        if value:
            return queryset.filter(ended_at__isnull=True)
        return queryset


class LegFilter(filters.FilterSet):
    journey = filters.NumberFilter(field_name="journey__id")
    started_after = filters.DateTimeFilter(field_name="started_at", lookup_expr="gte")
    started_before = filters.DateTimeFilter(field_name="started_at", lookup_expr="lte")

    class Meta:
        model = Leg
        fields = ["journey", "started_after", "started_before"]


class WeatherObservationFilter(filters.FilterSet):
    station = filters.NumberFilter(field_name="station__id")
    timestamp_after = filters.DateTimeFilter(field_name="timestamp", lookup_expr="gte")
    timestamp_before = filters.DateTimeFilter(field_name="timestamp", lookup_expr="lte")

    class Meta:
        model = WeatherObservation
        fields = ["station", "timestamp_after", "timestamp_before"]


# ViewSets
class VesselViewSet(viewsets.ModelViewSet):
    """
    API endpoint for vessels.

    list: Get all vessels
    create: Create a new vessel
    retrieve: Get vessel detail
    update: Update vessel
    partial_update: Partially update vessel
    destroy: Delete vessel
    """

    queryset = Vessel.objects.all()
    serializer_class = VesselSerializer
    search_fields = ["name", "vessel_type", "description"]
    ordering_fields = ["name", "created_at"]
    ordering = ["-created_at"]


class PersonViewSet(viewsets.ModelViewSet):
    """
    API endpoint for people.

    list: Get all people
    create: Create a new person
    retrieve: Get person detail
    update: Update person
    partial_update: Partially update person
    destroy: Delete person
    """

    queryset = Person.objects.all()
    serializer_class = PersonSerializer
    search_fields = ["first_name", "last_name", "email"]
    ordering_fields = ["last_name", "first_name", "created_at"]
    ordering = ["last_name", "first_name"]

    @extend_schema(responses=AboardSerializer(many=True))
    @action(detail=True, methods=["get"])
    def history(self, request, pk=None):
        """Get aboard history for this person."""
        person = self.get_object()
        aboard_records = Aboard.objects.filter(person=person).select_related("vessel").order_by("-started_at")
        serializer = AboardSerializer(aboard_records, many=True)
        return Response(serializer.data)


class TrackViewSet(viewsets.ModelViewSet):
    """
    API endpoint for tracks.

    list: Get all tracks (filter by ?vessel=:id)
    create: Create a new track
    retrieve: Get track detail
    update: Update track
    partial_update: Partially update track
    destroy: Delete track
    points: Get track points for a specific track
    upload_csv: Upload a CSV file to create a track with points
    """

    queryset = Track.objects.annotate(points_count=models.Count("points"))
    serializer_class = TrackSerializer
    filterset_class = TrackFilter
    search_fields = ["name", "description"]
    ordering_fields = ["started_at", "ended_at", "imported_at"]
    ordering = ["-started_at"]

    def get_serializer_class(self):
        if self.action == "retrieve":
            return TrackDetailSerializer
        return TrackSerializer

    @extend_schema(responses=TrackPointSerializer(many=True))
    @action(detail=True, methods=["get"])
    def points(self, request, pk=None):
        """Get all track points for this track."""
        track = self.get_object()
        points = track.points.all()
        serializer = TrackPointSerializer(points, many=True)
        return Response(serializer.data)

    @extend_schema(
        parameters=[
            OpenApiParameter("weather_station", OpenApiTypes.INT, OpenApiParameter.QUERY, required=False,
                             description="Weather station ID to join nearest observations"),
        ],
        responses={(200, "application/json"): OpenApiTypes.OBJECT},
        description="Get all track points as a GeoJSON FeatureCollection.",
    )
    @action(detail=True, methods=["get"])
    def geojson(self, request, pk=None):
        """Get all track points for this track as GeoJSON FeatureCollection."""
        track = self.get_object()
        points = track.points.all().order_by("timestamp")

        weather_station_id = request.query_params.get("weather_station")
        obs_list = []
        ts_list = []
        if weather_station_id and track.started_at and track.ended_at:
            obs_list = _fetch_weather_obs(weather_station_id, track.started_at, track.ended_at)
            ts_list = [o["timestamp"] for o in obs_list]

        features = [
            _build_geojson_feature(point, obs_list, ts_list, {"id": point.id})
            for point in points
        ]

        geojson_data = {
            "type": "FeatureCollection",
            "features": features,
            "properties": {
                "track_id": track.id,
                "track_name": track.name,
                "vessel_id": track.vessel_id,
                "started_at": track.started_at.isoformat()
                if track.started_at
                else None,
                "ended_at": track.ended_at.isoformat() if track.ended_at else None,
            },
        }

        return Response(geojson_data)

    @extend_schema(
        parameters=[
            OpenApiParameter("zoom", OpenApiTypes.STR, OpenApiParameter.QUERY, required=False,
                             enum=["region", "detail"], description="Zoom level (default: region)"),
        ],
        responses={(200, "image/png"): OpenApiTypes.BINARY, 204: None},
        description="Get a static map PNG thumbnail of the track.",
    )
    @action(detail=True, methods=["get"])
    def preview(self, request, pk=None):
        """
        Get a static map PNG thumbnail of the track.

        Returns a PNG image with the track's points plotted on a nautical basemap.
        Cached to disk on first request; subsequent requests serve the cached file.

        Query params:
            zoom: "region" (default) shows wide area for location context,
                  "detail" shows close-in view of the track.
        """
        from .previews import get_or_generate_track_preview

        track = self.get_object()
        zoom = request.query_params.get("zoom", "region")

        path = get_or_generate_track_preview(track, zoom)
        if path is None:
            return HttpResponse(status=204)

        response = HttpResponse(path.read_bytes(), content_type="image/png")
        response["Cache-Control"] = "public, max-age=86400"
        return response

    @extend_schema(
        request={"multipart/form-data": {
            "type": "object",
            "properties": {
                "file": {"type": "string", "format": "binary", "description": "CSV file"},
                "events": {"type": "string", "format": "binary",
                           "description": "Optional events sidecar (JSONL) recorded alongside the track"},
                "name": {"type": "string"},
                "description": {"type": "string"},
                "vessel": {"type": "integer"},
            },
            "required": ["file"],
        }},
        responses={(201, "application/json"): inline_serializer(
            name="TrackUploadResponse",
            fields={
                "success": drf_serializers.BooleanField(),
                "track_id": drf_serializers.IntegerField(),
                "track_name": drf_serializers.CharField(),
                "vessel_id": drf_serializers.IntegerField(allow_null=True),
                "vessel_name": drf_serializers.CharField(allow_null=True),
                "points_created": drf_serializers.IntegerField(),
                "modes_created": drf_serializers.IntegerField(required=False),
                "started_at": drf_serializers.DateTimeField(),
                "ended_at": drf_serializers.DateTimeField(),
                "warnings": drf_serializers.ListField(child=drf_serializers.CharField(), required=False),
            },
        )},
    )
    @action(
        detail=False,
        methods=["post"],
        parser_classes=[MultiPartParser],
        url_path="upload-csv",
    )
    def upload_csv(self, request):
        """
        Upload a CSV file containing GPS track points.

        Expected CSV format:
        lat,lng,speed,satellites,altitude,hdop,course,gps_date,gps_time

        Form fields:
        - file: CSV file (required)
        - events: events sidecar (JSONL) recorded alongside the track
          (optional) — one {"kind": <mode name>, "utc": <epoch>} object per
          line, ingested as Mode rows (issue #139). `kind` is any
          Mode.MODE_CHOICES value (see events_jsonl.MODE_KINDS); the watch can
          set all of them since issue #146. Requires a vessel (Mode is
          vessel-anchored); idempotent on re-upload. Bursts of presses within
          events_jsonl.MODE_DEBOUNCE_SECONDS collapse to one Mode row each
          (issue #161) and say so in `warnings`.
        - name: Track name (optional)
        - description: Track description (optional)
        - vessel: Vessel ID (optional)
        """
        from .events_jsonl import (
            collapse_mode_bursts,
            events_outside_window,
            parse_events_jsonl,
        )
        from .gps_csv import parse_gps_csv

        if "file" not in request.FILES:
            return Response(
                {"error": "No file provided. Use 'file' field in multipart form."},
                status=status.HTTP_400_BAD_REQUEST,
            )

        try:
            csv_content = request.FILES["file"].read().decode("utf-8")
        except UnicodeDecodeError:
            return Response(
                {"error": "File must be UTF-8 encoded CSV"},
                status=status.HTTP_400_BAD_REQUEST,
            )

        try:
            result = parse_gps_csv(csv_content)
        except ValueError as e:
            return Response(
                {"error": str(e)},
                status=status.HTTP_400_BAD_REQUEST,
            )

        if not result.is_valid:
            return Response(
                {"error": "No valid track points found in CSV", "details": result.errors},
                status=status.HTTP_400_BAD_REQUEST,
            )

        vessel_id = request.data.get("vessel")
        vessel = None
        if vessel_id:
            try:
                vessel = Vessel.objects.get(pk=vessel_id)
            except Vessel.DoesNotExist:
                return Response(
                    {"error": f"Vessel with id {vessel_id} not found"},
                    status=status.HTTP_400_BAD_REQUEST,
                )

        # Events sidecar (issue #139): parse up front so a malformed file is
        # a warning, never a failed track upload.
        events_result = None
        events_warnings = []
        if "events" in request.FILES:
            try:
                events_content = request.FILES["events"].read().decode("utf-8")
                events_result = parse_events_jsonl(events_content)
                events_warnings = list(events_result.warnings)
                # Mis-press debounce (issue #161): the sidecar keeps every
                # press, the segments keep only the ones that meant something.
                events_result.events, debounce_warnings = collapse_mode_bursts(
                    events_result.events
                )
                events_warnings.extend(debounce_warnings)
                outside = events_outside_window(
                    events_result.events, result.started_at, result.ended_at
                )
                if outside:
                    events_warnings.append(
                        f"{len(outside)} of {len(events_result.events)} sidecar "
                        f"events fall well outside the track's time range "
                        f"({result.started_at.isoformat()} to "
                        f"{result.ended_at.isoformat()}) — kept as Mode rows; "
                        f"earliest is "
                        f"{min(e.started_at for e in outside).isoformat()}"
                    )
            except UnicodeDecodeError:
                events_result = None
                events_warnings = ["Events file must be UTF-8 JSONL — ignored"]
            if events_result and events_result.events and vessel is None:
                # Mode rows are vessel-anchored; without a vessel there is
                # nothing to attach the events to.
                events_result = None
                events_warnings.append(
                    "Events sidecar ignored: no vessel provided"
                )

        try:
            with transaction.atomic():
                track = Track.objects.create(
                    name=request.data.get("name", ""),
                    description=request.data.get("description", ""),
                    vessel=vessel,
                    started_at=result.started_at,
                    ended_at=result.ended_at,
                    source_filename=request.FILES["file"].name or "",
                )
                TrackPoint.objects.bulk_create(
                    [TrackPoint(track=track, **vars(p)) for p in result.points],
                    batch_size=1000,
                )

                # Sidecar events -> Mode rows. get_or_create on the model's
                # (vessel, started_at) uniqueness makes re-uploads
                # idempotent: existing rows (possibly reclassified in the
                # web UI since) are left untouched.
                modes_created = 0
                if events_result:
                    for event in events_result.events:
                        _, created = Mode.objects.get_or_create(
                            vessel=vessel,
                            started_at=event.started_at,
                            defaults={
                                "mode": event.mode,
                                "metadata": {"source": "watch"},
                            },
                        )
                        if created:
                            modes_created += 1

                response_data = {
                    "success": True,
                    "track_id": track.id,
                    "track_name": track.name or f"Track {track.id}",
                    "vessel_id": track.vessel.id if track.vessel else None,
                    "vessel_name": track.vessel.name if track.vessel else None,
                    "points_created": len(result.points),
                    "started_at": result.started_at.isoformat(),
                    "ended_at": result.ended_at.isoformat(),
                }

                if "events" in request.FILES:
                    response_data["modes_created"] = modes_created

                warnings = result.errors + events_warnings
                if warnings:
                    response_data["warnings"] = warnings

                return Response(response_data, status=status.HTTP_201_CREATED)

        except Exception as e:
            return Response(
                {"error": f"Database error: {str(e)}"},
                status=status.HTTP_500_INTERNAL_SERVER_ERROR,
            )


class TrackPointViewSet(viewsets.GenericViewSet):
    """
    Destroy-only endpoint for individual GPS points.

    destroy: Hard-delete a point from its track and remove any annotations
    or modes pinned to the exact same (vessel, timestamp). Journeys are
    computed from vessel + time window, so the point disappears from every
    journey automatically.
    """

    queryset = TrackPoint.objects.select_related("track").all()
    serializer_class = TrackPointSerializer

    def destroy(self, request, *args, **kwargs):
        from django.db.models import Q

        from .previews import invalidate_previews, invalidate_track_previews

        point = self.get_object()
        track_id = point.track_id
        vessel_id = point.track.vessel_id
        timestamp = point.timestamp
        with transaction.atomic():
            if vessel_id is not None:
                Annotation.objects.filter(vessel_id=vessel_id, timestamp=timestamp).delete()
                Mode.objects.filter(vessel_id=vessel_id, started_at=timestamp).delete()
            point.delete()

        invalidate_track_previews(track_id)

        if vessel_id is not None:
            journeys = Journey.objects.filter(
                vessel_id=vessel_id,
                started_at__lte=timestamp,
            ).filter(Q(ended_at__isnull=True) | Q(ended_at__gte=timestamp))
            for journey in journeys:
                invalidate_previews(journey.pk)

        return Response(status=status.HTTP_204_NO_CONTENT)


class ModeViewSet(viewsets.ModelViewSet):
    """
    API endpoint for modes.

    list: Get all modes (filter by ?vessel=:id, ?mode=:type)
    create: Create a new mode
    retrieve: Get mode detail
    update: Update mode
    partial_update: Partially update mode
    destroy: Delete mode
    """

    queryset = Mode.objects.all()
    serializer_class = ModeSerializer
    filterset_class = ModeFilter
    ordering_fields = ["started_at"]
    ordering = ["-started_at"]

    def create(self, request, *args, **kwargs):
        # Upsert by (vessel, started_at): replacing the mode at a given
        # timestamp is a modification, not a new row.
        serializer = self.get_serializer(data=request.data)
        serializer.is_valid(raise_exception=True)
        v = serializer.validated_data
        obj, created = Mode.objects.update_or_create(
            vessel=v["vessel"],
            started_at=v["started_at"],
            defaults={k: v[k] for k in v if k not in ("vessel", "started_at")},
        )
        out = self.get_serializer(obj)
        return Response(
            out.data,
            status=status.HTTP_201_CREATED if created else status.HTTP_200_OK,
        )


class AnnotationViewSet(viewsets.ModelViewSet):
    """
    API endpoint for annotations.

    list: Get all annotations (filter by ?vessel=:id, ?type=:type)
    create: Create a new annotation
    retrieve: Get annotation detail
    update: Update annotation
    partial_update: Partially update annotation
    destroy: Delete annotation
    """

    queryset = Annotation.objects.all()
    serializer_class = AnnotationSerializer
    filterset_class = AnnotationFilter
    search_fields = ["notes"]
    ordering_fields = ["timestamp"]
    ordering = ["-timestamp"]

    def create(self, request, *args, **kwargs):
        # Upsert by (vessel, type, timestamp): only one annotation of a given
        # type per point.
        serializer = self.get_serializer(data=request.data)
        serializer.is_valid(raise_exception=True)
        v = serializer.validated_data
        keys = ("vessel", "type", "timestamp")
        obj, created = Annotation.objects.update_or_create(
            **{k: v[k] for k in keys},
            defaults={k: v[k] for k in v if k not in keys},
        )
        out = self.get_serializer(obj)
        return Response(
            out.data,
            status=status.HTTP_201_CREATED if created else status.HTTP_200_OK,
        )


class AboardViewSet(viewsets.ModelViewSet):
    """
    API endpoint for aboard records.

    list: Get all aboard records (filter by ?vessel=:id, ?person=:id, ?current=true)
    create: Create a new aboard record
    retrieve: Get aboard detail
    update: Update aboard record
    partial_update: Partially update aboard record
    destroy: Delete aboard record
    """

    queryset = Aboard.objects.select_related("person", "vessel").all()
    serializer_class = AboardSerializer
    filterset_class = AboardFilter
    ordering_fields = ["started_at", "ended_at"]
    ordering = ["-started_at"]


class JourneyViewSet(viewsets.ModelViewSet):
    """
    API endpoint for journeys.

    list: Get all journeys (filter by ?vessel=:id, ?ongoing=true)
    create: Create a new journey
    retrieve: Get journey detail (with nested legs)
    update: Update journey
    partial_update: Partially update journey
    destroy: Delete journey
    detail: Get journey with all related data (modes, annotations, aboard, tracks summary)
    tracks: Get track point data in GeoJSON format
    """

    queryset = (
        Journey.objects.select_related("vessel")
        .prefetch_related("legs")
        .annotate(
            legs_count=models.Count("legs"),
            _manual_route_length_m=GisLength("manual_route"),
        )
    )
    serializer_class = JourneySerializer
    filterset_class = JourneyFilter
    search_fields = ["name", "description"]
    ordering_fields = ["started_at", "ended_at"]
    ordering = ["-started_at"]

    def get_serializer_class(self):
        if self.action == "retrieve":
            return JourneyDetailSerializer
        return JourneySerializer

    def perform_create(self, serializer):
        journey = serializer.save()
        # A drawn journey starts at midnight and has no fixes to label; a mode
        # there would claim the boat was sailing all day for whatever is
        # recorded later.
        if journey.manual_route is not None:
            return
        # Use get_or_create: Mode.unique_together=(vessel, started_at) means a
        # second journey starting at the same instant as an existing mode would
        # otherwise IntegrityError.
        Mode.objects.get_or_create(
            vessel=journey.vessel,
            started_at=journey.started_at,
            defaults={"mode": "SAIL"},
        )

    @extend_schema(responses=ModeSerializer(many=True))
    @action(detail=True, methods=["get"])
    def modes(self, request, pk=None):
        """Get modes within the journey timeframe."""
        from .models import precompute_mode_ended_at

        journey = self.get_object()
        qs = Mode.objects.filter(
            vessel=journey.vessel,
            started_at__gte=journey.started_at,
        )
        if journey.ended_at:
            qs = qs.filter(started_at__lte=journey.ended_at)
        modes = precompute_mode_ended_at(qs, tracks=journey.get_tracks())
        return Response(
            ModeSerializer(modes, many=True, context={"end_boundary": journey.ended_at}).data
        )

    @extend_schema(responses=AnnotationSerializer(many=True))
    @action(detail=True, methods=["get"])
    def annotations(self, request, pk=None):
        """Get annotations within the journey timeframe."""
        journey = self.get_object()
        qs = Annotation.objects.filter(
            vessel=journey.vessel,
            timestamp__gte=journey.started_at,
        )
        if journey.ended_at:
            qs = qs.filter(timestamp__lte=journey.ended_at)
        return Response(AnnotationSerializer(qs, many=True).data)

    @extend_schema(responses=AboardSerializer(many=True))
    @action(detail=True, methods=["get"])
    def aboard(self, request, pk=None):
        """Get aboard records overlapping the journey timeframe."""
        journey = self.get_object()
        qs = Aboard.objects.select_related("person", "vessel").filter(vessel=journey.vessel)
        if journey.ended_at:
            qs = qs.filter(started_at__lte=journey.ended_at)
        qs = qs.filter(
            models.Q(ended_at__isnull=True) | models.Q(ended_at__gte=journey.started_at)
        )
        return Response(AboardSerializer(qs, many=True).data)

    @extend_schema(
        responses={(200, "application/json"): OpenApiTypes.OBJECT},
        description="Get track points within journey timeframe as GeoJSON FeatureCollection. "
                    "Weather observations are automatically joined when the journey has a weather station.",
    )
    @action(detail=True, methods=["get"])
    def tracks(self, request, pk=None):
        """
        Get track point data in GeoJSON format.

        Returns lightweight GeoJSON with:
        - Point coordinates (lng, lat)
        - Timestamp, speed, altitude per point
        - No mode/annotation data (use /detail/ endpoint for that)

        If the journey has a weather_station set, nearest weather
        observations are joined to each point automatically.
        """
        from django.db.models import FloatField
        from django.db.models.expressions import RawSQL

        journey = self.get_object()

        obs_list = []
        ts_list = []
        if journey.weather_station_id and journey.started_at and journey.ended_at:
            obs_list = _fetch_weather_obs(journey.weather_station_id, journey.started_at, journey.ended_at)
            ts_list = [o["timestamp"] for o in obs_list]

        # Single flat query with coordinate extraction — avoids full model instantiation
        points_qs = TrackPoint.objects.filter(
            track__vessel=journey.vessel,
            timestamp__gte=journey.started_at,
        ).annotate(
            lng=RawSQL("ST_X(location::geometry)", [], output_field=FloatField()),
            lat=RawSQL("ST_Y(location::geometry)", [], output_field=FloatField()),
        )
        if journey.ended_at:
            points_qs = points_qs.filter(timestamp__lte=journey.ended_at)

        points = points_qs.order_by("timestamp").values(
            "id", "timestamp", "speed", "altitude", "satellites", "hdop", "course",
            "heel", "trim", "heading",
            "lng", "lat", "track_id",
        )

        features = [
            _build_geojson_feature_from_dict(p, obs_list, ts_list)
            for p in points
        ]

        if journey.manual_route is not None:
            features.append(
                {
                    "type": "Feature",
                    "geometry": json.loads(journey.manual_route.geojson),
                    "properties": {"kind": "manual_route"},
                }
            )

        return Response(
            {
                "type": "FeatureCollection",
                "properties": {
                    "journey_id": journey.id,
                    "journey_name": journey.name,
                    "vessel_id": journey.vessel.id,
                    "vessel_name": journey.vessel.name,
                },
                "features": features,
            }
        )

    @extend_schema(
        responses={(200, "application/json"): OpenApiTypes.OBJECT},
        description="Get summary statistics for the journey's weather station over the journey window.",
    )
    @action(detail=True, methods=["get"], url_path="weather-summary")
    def weather_summary(self, request, pk=None):
        """
        Aggregate weather observations for the journey's selected station
        across the journey window.

        Returns cards-friendly summary stats plus a 16-direction × 6-speed-band
        wind rose histogram (matching the WindRoseView.vue speed bands), and a
        per-observation `series` array for overlays and mini time-series.
        """
        journey = self.get_object()

        if not journey.ended_at:
            return Response(
                {"error": "Journey is ongoing; weather summary needs a closed window."},
                status=status.HTTP_400_BAD_REQUEST,
            )

        if not journey.weather_station_id:
            return Response(_empty_weather_summary(None))

        obs_qs = (
            WeatherObservation.objects.filter(
                station_id=journey.weather_station_id,
                timestamp__gte=journey.started_at,
                timestamp__lte=journey.ended_at,
            )
            .order_by("timestamp")
            .values(
                "timestamp", "temperature", "rain_accumulated",
                "wind_direction", "wind_speed", "gust_speed",
                "humidity", "tide_observation",
            )
        )
        station = journey.weather_station
        return Response(
            _summarize_weather(
                list(obs_qs),
                station=station,
                window_start=journey.started_at,
                window_end=journey.ended_at,
            )
        )

    @extend_schema(
        parameters=[
            OpenApiParameter("zoom", OpenApiTypes.STR, OpenApiParameter.QUERY, required=False,
                             enum=["region", "detail"], description="Zoom level (default: region)"),
        ],
        responses={(200, "image/png"): OpenApiTypes.BINARY, 204: None},
        description="Get a static map PNG thumbnail of the journey's track.",
    )
    @action(detail=True, methods=["get"])
    def preview(self, request, pk=None):
        """
        Get a static map PNG thumbnail of the journey's track.

        Returns a PNG image with the journey's track plotted on a nautical basemap.
        Cached to disk on first request; subsequent requests serve the cached file.

        Query params:
            zoom: "region" (default) shows wide area for location context,
                  "detail" shows close-in view of the track.
        """
        from .previews import get_or_generate_preview

        journey = self.get_object()
        zoom = request.query_params.get("zoom", "region")

        path = get_or_generate_preview(journey, zoom)
        if path is None:
            return HttpResponse(status=204)

        response = HttpResponse(path.read_bytes(), content_type="image/png")
        response["Cache-Control"] = "public, max-age=86400"
        return response

    @extend_schema(
        responses={(200, "application/json"): OpenApiTypes.OBJECT},
        description="List Immich photos and videos taken during the journey window (±2h).",
    )
    @action(detail=True, methods=["get"], url_path="photos", url_name="photos")
    def photos(self, request, pk=None):
        from django.conf import settings
        from .immich import ImmichClient, ImmichError
        from .photos import journey_photo_window, serialize_immich_asset

        journey = self.get_object()
        client = ImmichClient.from_settings()
        public_url = settings.IMMICH_PUBLIC_URL
        if client is None:
            return Response({"photos": [], "immichPublicUrl": None, "window": None})

        window = journey_photo_window(journey)
        if window is None:
            return Response({"photos": [], "immichPublicUrl": public_url, "window": None})

        window_payload = {"start": window[0].isoformat(), "end": window[1].isoformat()}
        try:
            assets = client.search_assets(window[0], window[1])
        except ImmichError:
            return Response(
                {"error": "Immich is unavailable.", "window": window_payload},
                status=status.HTTP_502_BAD_GATEWAY,
            )

        photos = [serialize_immich_asset(a, journey.id, request) for a in assets]
        photos.sort(key=lambda p: p.get("takenAt") or "")
        return Response({
            "photos": photos,
            "immichPublicUrl": public_url,
            "window": window_payload,
        })

    @extend_schema(
        responses={(200, "image/*"): OpenApiTypes.BINARY},
        description="Proxy a thumbnail for a single Immich asset through Django.",
    )
    @action(
        detail=True,
        methods=["get"],
        url_path=r"photos/(?P<asset_id>[^/.]+)/thumb",
        url_name="photo-thumb",
    )
    def photo_thumb(self, request, pk=None, asset_id=None):
        return self._proxy_image(asset_id, "thumbnail")

    @extend_schema(
        responses={(200, "image/*"): OpenApiTypes.BINARY},
        description="Proxy a preview for a single Immich asset through Django.",
    )
    @action(
        detail=True,
        methods=["get"],
        url_path=r"photos/(?P<asset_id>[^/.]+)/preview",
        url_name="photo-preview",
    )
    def photo_preview(self, request, pk=None, asset_id=None):
        return self._proxy_image(asset_id, "preview")

    def _proxy_image(self, asset_id, size):
        from .immich import ImmichClient, ImmichError

        client = ImmichClient.from_settings()
        if client is None:
            return HttpResponse(status=404)
        try:
            data, content_type = client.fetch_thumbnail(asset_id, size=size)
        except ImmichError:
            return HttpResponse(status=502)
        response = HttpResponse(data, content_type=content_type)
        response["Cache-Control"] = "public, max-age=31536000, immutable"
        return response


class LegViewSet(viewsets.ModelViewSet):
    """
    API endpoint for legs.

    list: Get all legs (filter by ?journey=:id)
    create: Create a new leg
    retrieve: Get leg detail
    update: Update leg
    partial_update: Partially update leg
    destroy: Delete leg
    """

    queryset = Leg.objects.all()
    serializer_class = LegSerializer
    filterset_class = LegFilter
    search_fields = ["name", "notes"]
    ordering_fields = ["started_at", "ended_at"]
    ordering = ["started_at"]


def _build_geojson_feature(point, obs_list=None, ts_list=None, extra_properties=None):
    """Build a GeoJSON Feature dict for a TrackPoint."""
    properties = {
        "timestamp": point.timestamp.isoformat(),
        "speed": point.speed,
        "altitude": point.altitude,
        "satellites": point.satellites,
        "hdop": point.hdop,
        "course": point.course,
        "heel": point.heel,
        "trim": point.trim,
        "heading": point.heading,
        **(_nearest_weather(obs_list, ts_list, point.timestamp) if obs_list else {}),
    }
    if extra_properties:
        properties.update(extra_properties)
    return {
        "type": "Feature",
        "geometry": {"type": "Point", "coordinates": [point.location.x, point.location.y]},
        "properties": properties,
    }


def _build_geojson_feature_from_dict(point_dict, obs_list=None, ts_list=None):
    """Build a GeoJSON Feature dict from a .values() dict with lng/lat annotations."""
    properties = {
        "id": point_dict.get("id"),
        "timestamp": point_dict["timestamp"].isoformat(),
        "speed": point_dict["speed"],
        "altitude": point_dict["altitude"],
        "satellites": point_dict["satellites"],
        "hdop": point_dict["hdop"],
        "course": point_dict["course"],
        "heel": point_dict["heel"],
        "trim": point_dict["trim"],
        "heading": point_dict["heading"],
        "track_id": point_dict["track_id"],
        **(_nearest_weather(obs_list, ts_list, point_dict["timestamp"]) if obs_list else {}),
    }
    return {
        "type": "Feature",
        "geometry": {
            "type": "Point",
            "coordinates": [point_dict["lng"], point_dict["lat"]],
        },
        "properties": properties,
    }


# Wind-rose speed bands matching frontend/src/views/WindRoseView.vue (in knots).
# None as upper bound means "no upper limit".
_WIND_ROSE_SPEED_BANDS = [
    {"label": "< 5 kn",    "min": 0,  "max": 5},
    {"label": "5–10 kn",   "min": 5,  "max": 10},
    {"label": "10–15 kn",  "min": 10, "max": 15},
    {"label": "15–20 kn",  "min": 15, "max": 20},
    {"label": "20–25 kn",  "min": 20, "max": 25},
    {"label": "25+ kn",    "min": 25, "max": None},
]

_WIND_ROSE_DIR_LABELS = [
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
]


def _stats(values):
    """min/max/avg over non-None numeric values, or None if empty."""
    vs = [v for v in values if v is not None]
    if not vs:
        return {"min": None, "max": None, "avg": None}
    return {"min": min(vs), "max": max(vs), "avg": sum(vs) / len(vs)}


def _empty_weather_summary(station):
    station_payload = None
    if station is not None:
        station_payload = {"id": station.id, "name": station.name}
    return {
        "station": station_payload,
        "window": {"started_at": None, "ended_at": None, "observation_count": 0},
        "wind": {"min": None, "max": None, "avg": None},
        "gust": {"max": None, "avg": None},
        "temperature": {"min": None, "max": None, "avg": None},
        "humidity": {"min": None, "max": None, "avg": None},
        "rain_total_mm": 0.0,
        "tide": {"first": None, "last": None, "delta": None, "min": None, "max": None},
        "wind_rose": [],
        "wind_rose_speed_bands": _WIND_ROSE_SPEED_BANDS,
        "series": [],
    }


def _summarize_weather(obs_list, station, window_start, window_end):
    """Aggregate a sorted list of WeatherObservation dicts into summary payload."""
    if not obs_list:
        payload = _empty_weather_summary(station)
        payload["window"] = {
            "started_at": window_start.isoformat() if window_start else None,
            "ended_at": window_end.isoformat() if window_end else None,
            "observation_count": 0,
        }
        return payload

    import bisect

    # Speed-band upper edges, ignoring the final open-ended band.
    band_edges = [b["max"] for b in _WIND_ROSE_SPEED_BANDS if b["max"] is not None]
    last_band_idx = len(_WIND_ROSE_SPEED_BANDS) - 1

    # Single pass: collect per-field arrays, rain total, first/last tide, and wind-rose bins.
    wind_speeds: list = []
    gust_speeds: list = []
    temperatures: list = []
    humidities: list = []
    tide_values: list = []
    rain_total = 0.0
    first_tide = None
    last_tide = None
    bins = [[0] * len(_WIND_ROSE_SPEED_BANDS) for _ in range(16)]

    for o in obs_list:
        wind_speeds.append(o["wind_speed"])
        gust_speeds.append(o["gust_speed"])
        temperatures.append(o["temperature"])
        humidities.append(o["humidity"])

        rain = o["rain_accumulated"]
        if rain is not None:
            rain_total += rain

        tide = o["tide_observation"]
        if tide is not None:
            tide_values.append(tide)
            if first_tide is None:
                first_tide = tide
            last_tide = tide

        d = o["wind_direction"]
        s = o["wind_speed"]
        if d is not None and s is not None:
            dir_idx = int(round((d % 360) / 22.5)) % 16
            band_idx = bisect.bisect_right(band_edges, s)
            if band_idx > last_band_idx:
                band_idx = last_band_idx
            bins[dir_idx][band_idx] += 1

    gust_stats = _stats(gust_speeds)

    if tide_values:
        tide_payload = {
            "first": first_tide,
            "last": last_tide,
            "delta": last_tide - first_tide,
            "min": min(tide_values),
            "max": max(tide_values),
        }
    else:
        tide_payload = {"first": None, "last": None, "delta": None, "min": None, "max": None}

    wind_rose = [
        {
            "direction_deg": i * 22.5,
            "label": _WIND_ROSE_DIR_LABELS[i],
            "bands": bins[i],
            "total": sum(bins[i]),
        }
        for i in range(16)
    ]

    series = [
        {
            "t": o["timestamp"].isoformat(),
            "wind_speed": o["wind_speed"],
            "gust": o["gust_speed"],
            "wind_direction": o["wind_direction"],
            "temperature": o["temperature"],
            "humidity": o["humidity"],
            "rain": o["rain_accumulated"],
            "tide": o["tide_observation"],
        }
        for o in obs_list
    ]

    return {
        "station": {"id": station.id, "name": station.name} if station else None,
        "window": {
            "started_at": window_start.isoformat() if window_start else None,
            "ended_at": window_end.isoformat() if window_end else None,
            "observation_count": len(obs_list),
        },
        "wind": _stats(wind_speeds),
        "gust": {"max": gust_stats["max"], "avg": gust_stats["avg"]},
        "temperature": _stats(temperatures),
        "humidity": _stats(humidities),
        "rain_total_mm": rain_total,
        "tide": tide_payload,
        "wind_rose": wind_rose,
        "wind_rose_speed_bands": _WIND_ROSE_SPEED_BANDS,
        "series": series,
    }


def _fetch_weather_obs(station_id, start_time, end_time):
    """Fetch sorted WeatherObservations covering a time range (with 2-min buffer)."""
    buffer = timedelta(minutes=2)
    return list(
        WeatherObservation.objects.filter(
            station_id=station_id,
            timestamp__gte=start_time - buffer,
            timestamp__lte=end_time + buffer,
        )
        .order_by("timestamp")
        .values("timestamp", "wind_direction", "wind_speed", "gust_speed")
    )


def _nearest_weather(obs_list, ts_list, timestamp, max_gap_seconds=300):
    """Return weather fields from the observation nearest to timestamp (bisect).

    Returns {} if the nearest observation is more than max_gap_seconds away.
    ts_list must be pre-built as [o["timestamp"] for o in obs_list].
    """
    import bisect

    if not obs_list:
        return {}
    idx = bisect.bisect_left(ts_list, timestamp)
    candidates = []
    if idx < len(obs_list):
        candidates.append(obs_list[idx])
    if idx > 0:
        candidates.append(obs_list[idx - 1])
    nearest = min(candidates, key=lambda o: abs((o["timestamp"] - timestamp).total_seconds()))
    if abs((nearest["timestamp"] - timestamp).total_seconds()) > max_gap_seconds:
        return {}
    return {k: nearest[k] for k in ("wind_direction", "wind_speed", "gust_speed")}


class WeatherStationViewSet(viewsets.ModelViewSet):
    queryset = WeatherStation.objects.all()
    serializer_class = WeatherStationSerializer
    search_fields = ["name", "description"]
    ordering_fields = ["name", "created_at"]
    ordering = ["-created_at"]

    @extend_schema(
        request={"multipart/form-data": {
            "type": "object",
            "properties": {
                "file": {"type": "string", "format": "binary", "description": "Weather CSV file"},
            },
            "required": ["file"],
        }},
        responses={(201, "application/json"): inline_serializer(
            name="WeatherUploadResponse",
            fields={
                "success": drf_serializers.BooleanField(),
                "station_id": drf_serializers.IntegerField(),
                "observations_created": drf_serializers.IntegerField(),
                "time_range": drf_serializers.DictField(),
                "warnings": drf_serializers.ListField(child=drf_serializers.CharField(), required=False),
            },
        )},
    )
    @action(
        detail=True,
        methods=["post"],
        parser_classes=[MultiPartParser],
        url_path="upload-csv",
    )
    def upload_csv(self, request, pk=None):
        """
        Upload a weather CSV file for this station.

        Expected CSV columns: Time, Temperature (°C), Rain Accumulation (mm),
        Wind Direction (°), Wind Speed (kn), Gust Speed (kn), Humidity (%),
        Tide Observation (m)
        """
        import zoneinfo

        station = self.get_object()

        if "file" not in request.FILES:
            return Response(
                {"error": "No file provided. Use 'file' field in multipart form."},
                status=status.HTTP_400_BAD_REQUEST,
            )

        try:
            csv_content = request.FILES["file"].read().decode("utf-8")
        except UnicodeDecodeError:
            return Response(
                {"error": "File must be UTF-8 encoded CSV"},
                status=status.HTTP_400_BAD_REQUEST,
            )

        csv_reader = csv.DictReader(StringIO(csv_content))

        if "Time" not in (csv_reader.fieldnames or []):
            return Response(
                {
                    "error": "Missing required column: 'Time'",
                    "found": csv_reader.fieldnames,
                },
                status=status.HTTP_400_BAD_REQUEST,
            )

        nz_tz = zoneinfo.ZoneInfo("Pacific/Auckland")
        observations = []
        warnings = []

        def parse_float(val):
            if val is None or val.strip() == "":
                return None
            try:
                return float(val)
            except ValueError:
                return None

        col_map = {
            "temperature": "Temperature (°C)",
            "rain_accumulated": "Rain Accumulation (mm)",
            "wind_direction": "Wind Direction (°)",
            "wind_speed": "Wind Speed (kn)",
            "gust_speed": "Gust Speed (kn)",
            "humidity": "Humidity (%)",
            "tide_observation": "Tide Observation (m)",
        }

        for row_num, row in enumerate(csv_reader, start=2):
            try:
                naive_dt = datetime.strptime(row["Time"].strip(), "%Y-%m-%d %H:%M:%S")
                local_dt = naive_dt.replace(tzinfo=nz_tz)
                utc_dt = local_dt.astimezone(dt_timezone.utc)
            except (ValueError, KeyError) as e:
                warnings.append(f"Row {row_num}: Could not parse Time - {e}")
                continue

            obs = WeatherObservation(
                station=station,
                timestamp=utc_dt,
                **{field: parse_float(row.get(col)) for field, col in col_map.items()},
            )
            observations.append(obs)

        if not observations:
            return Response(
                {"error": "No valid observations found in CSV", "warnings": warnings},
                status=status.HTTP_400_BAD_REQUEST,
            )

        created = WeatherObservation.objects.bulk_create(
            observations, ignore_conflicts=True
        )

        response_data = {
            "success": True,
            "station_id": station.id,
            "observations_created": len(created),
            "time_range": {
                "start": min(o.timestamp for o in observations).isoformat(),
                "end": max(o.timestamp for o in observations).isoformat(),
            },
        }
        if warnings:
            response_data["warnings"] = warnings

        return Response(response_data, status=status.HTTP_201_CREATED)


class WeatherObservationViewSet(viewsets.ModelViewSet):
    queryset = WeatherObservation.objects.all()
    serializer_class = WeatherObservationSerializer
    filterset_class = WeatherObservationFilter
    ordering_fields = ["timestamp"]
    ordering = ["timestamp"]
