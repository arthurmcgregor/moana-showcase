import json

from django.contrib.gis.geos import GEOSGeometry
from django.contrib.gis.geos.error import GEOSException
from drf_spectacular.utils import extend_schema_field
from rest_framework import serializers

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


class VesselSerializer(serializers.ModelSerializer):
    class Meta:
        model = Vessel
        fields = "__all__"


class PersonSerializer(serializers.ModelSerializer):
    class Meta:
        model = Person
        fields = "__all__"


class TrackPointSerializer(serializers.ModelSerializer):
    location = serializers.CharField(help_text="WKT point, e.g. SRID=4326;POINT (lng lat)")

    class Meta:
        model = TrackPoint
        fields = "__all__"


class TrackSerializer(serializers.ModelSerializer):
    points_count = serializers.IntegerField(read_only=True)

    class Meta:
        model = Track
        fields = "__all__"


class TrackDetailSerializer(serializers.ModelSerializer):
    """Detailed track serializer with nested points."""

    points = TrackPointSerializer(many=True, read_only=True)

    class Meta:
        model = Track
        fields = "__all__"


class ModeSerializer(serializers.ModelSerializer):
    mode_display = serializers.CharField(source="get_mode_display", read_only=True)
    duration = serializers.SerializerMethodField()
    ended_at = serializers.SerializerMethodField()

    class Meta:
        model = Mode
        fields = "__all__"

    def get_validators(self):
        # On create, ModeViewSet upserts via update_or_create — skip the
        # auto-generated UniqueTogetherValidator so repeat POSTs aren't 400'd.
        # Keep it for update/partial_update so PATCHing onto another row's
        # (vessel, started_at) fails cleanly instead of hitting a DB IntegrityError.
        view = self.context.get("view")
        if view is not None and getattr(view, "action", None) == "create":
            return []
        return super().get_validators()

    @extend_schema_field(serializers.FloatField(allow_null=True, help_text="Duration in seconds"))
    def get_duration(self, obj):
        end_boundary = self.context.get("end_boundary")
        duration = obj.get_duration(end_boundary=end_boundary)
        return duration.total_seconds() if duration else None

    @extend_schema_field(serializers.DateTimeField(allow_null=True))
    def get_ended_at(self, obj):
        end_boundary = self.context.get("end_boundary")
        ended_at = obj.get_ended_at(end_boundary=end_boundary)
        return ended_at.isoformat() if ended_at else None


class AnnotationSerializer(serializers.ModelSerializer):
    type_display = serializers.CharField(source="get_type_display", read_only=True)

    class Meta:
        model = Annotation
        fields = "__all__"

    def get_validators(self):
        # On create, AnnotationViewSet upserts via update_or_create — skip the
        # auto-generated UniqueTogetherValidator so repeat POSTs aren't 400'd.
        # Keep it for update/partial_update so PATCHing onto another row's
        # (vessel, type, timestamp) fails cleanly instead of a DB IntegrityError.
        view = self.context.get("view")
        if view is not None and getattr(view, "action", None) == "create":
            return []
        return super().get_validators()


class AboardSerializer(serializers.ModelSerializer):
    person_name = serializers.CharField(source="person.__str__", read_only=True)
    vessel_name = serializers.CharField(source="vessel.name", read_only=True)
    duration = serializers.SerializerMethodField()
    is_currently_aboard = serializers.SerializerMethodField()

    class Meta:
        model = Aboard
        fields = "__all__"

    @extend_schema_field(serializers.FloatField(allow_null=True, help_text="Duration in seconds"))
    def get_duration(self, obj):
        duration = obj.get_duration()
        return duration.total_seconds() if duration else None

    @extend_schema_field(serializers.BooleanField())
    def get_is_currently_aboard(self, obj):
        return obj.is_currently_aboard()

    def validate(self, attrs):
        person = attrs.get("person") or getattr(self.instance, "person", None)
        started_at = attrs.get("started_at") or getattr(self.instance, "started_at", None)
        ended_at = attrs.get("ended_at", getattr(self.instance, "ended_at", None))

        if person is None or started_at is None:
            return attrs

        qs = Aboard.objects.filter(person=person)
        if self.instance is not None:
            qs = qs.exclude(pk=self.instance.pk)

        # Overlap: existing.started_at < new.ended_at AND existing.ended_at > new.started_at.
        # Treat null ended_at as +infinity on either side.
        for record in qs:
            existing_end_open = record.ended_at is None
            new_end_open = ended_at is None
            starts_before_new_end = new_end_open or record.started_at < ended_at
            ends_after_new_start = existing_end_open or record.ended_at > started_at
            if starts_before_new_end and ends_after_new_start:
                raise serializers.ValidationError(
                    "Aboard record overlaps an existing record for this person."
                )

        return attrs


class LegSerializer(serializers.ModelSerializer):
    duration = serializers.SerializerMethodField()

    class Meta:
        model = Leg
        fields = "__all__"

    @extend_schema_field(serializers.FloatField(allow_null=True, help_text="Duration in seconds"))
    def get_duration(self, obj):
        duration = obj.get_duration()
        return duration.total_seconds() if duration else None


class LegDetailSerializer(LegSerializer):
    """Leg serializer with computed statistics."""

    statistics = serializers.SerializerMethodField()

    @extend_schema_field(serializers.DictField(allow_null=True))
    def get_statistics(self, obj):
        precomputed = getattr(obj, "_precomputed_stats", None)
        if precomputed is not None:
            return precomputed
        return obj.get_statistics()


class ManualRouteField(serializers.JSONField):
    """GeoJSON LineString field for Journey.manual_route."""

    def to_representation(self, value):
        if value is None:
            return None
        return json.loads(value.geojson)

    def to_internal_value(self, data):
        if data is None:
            return None
        try:
            geom = GEOSGeometry(json.dumps(data) if isinstance(data, dict) else data)
        except (GEOSException, ValueError, TypeError) as exc:
            raise serializers.ValidationError(f"Invalid GeoJSON: {exc}")
        if geom.geom_type != "LineString":
            raise serializers.ValidationError(
                "manual_route must be a GeoJSON LineString"
            )
        if len(geom.coords) < 2:
            raise serializers.ValidationError(
                "manual_route must have at least 2 points"
            )
        geom.srid = 4326
        return geom


class JourneySerializer(serializers.ModelSerializer):
    legs_count = serializers.IntegerField(read_only=True)
    duration = serializers.SerializerMethodField()
    distance_nm = serializers.SerializerMethodField()
    estimated_distance_nm = serializers.SerializerMethodField()
    manual_route = ManualRouteField(required=False, allow_null=True)

    class Meta:
        model = Journey
        fields = "__all__"

    @extend_schema_field(serializers.FloatField(allow_null=True, help_text="Duration in seconds"))
    def get_duration(self, obj):
        duration = obj.get_duration()
        return duration.total_seconds() if duration else None

    @extend_schema_field(serializers.FloatField(help_text="On-water distance in nautical miles"))
    def get_distance_nm(self, obj):
        from .statistics import journey_stats

        return round(journey_stats.on_water_distance_nm(obj), 1)

    @extend_schema_field(
        serializers.FloatField(
            allow_null=True,
            help_text="Estimated distance from manual_route in nautical miles",
        )
    )
    def get_estimated_distance_nm(self, obj):
        from .statistics import journey_stats

        value = journey_stats.estimated_distance_nm(obj)
        return round(value, 1) if value is not None else None


class JourneyDetailSerializer(serializers.ModelSerializer):
    """Detailed journey serializer with nested legs and per-leg statistics."""

    vessel_name = serializers.CharField(source="vessel.name", read_only=True)
    duration = serializers.SerializerMethodField()
    legs = LegDetailSerializer(many=True, read_only=True)
    statistics = serializers.SerializerMethodField()
    estimated_distance_nm = serializers.SerializerMethodField()
    manual_route = ManualRouteField(required=False, allow_null=True)

    class Meta:
        model = Journey
        fields = "__all__"

    @extend_schema_field(
        serializers.FloatField(
            allow_null=True,
            help_text="Estimated distance from manual_route in nautical miles",
        )
    )
    def get_estimated_distance_nm(self, obj):
        from .statistics import journey_stats

        value = journey_stats.estimated_distance_nm(obj)
        return round(value, 1) if value is not None else None

    @extend_schema_field(serializers.FloatField(allow_null=True, help_text="Duration in seconds"))
    def get_duration(self, obj):
        duration = obj.get_duration()
        return duration.total_seconds() if duration else None

    @extend_schema_field(serializers.DictField(allow_null=True))
    def get_statistics(self, obj):
        return getattr(obj, "_precomputed_journey_stats", None) or obj.get_statistics()

    def to_representation(self, instance):
        from .statistics import journey_stats

        stats = journey_stats.for_journey(instance)
        instance._precomputed_journey_stats = stats.journey.to_dict()
        for leg in instance.legs.all():
            if leg.id in stats.legs:
                leg._precomputed_stats = stats.legs[leg.id].to_dict()
        return super().to_representation(instance)


class WeatherStationSerializer(serializers.ModelSerializer):
    location = serializers.CharField(
        allow_null=True, required=False,
        help_text="WKT point, e.g. SRID=4326;POINT (lng lat)",
    )

    class Meta:
        model = WeatherStation
        fields = "__all__"


class WeatherObservationSerializer(serializers.ModelSerializer):
    class Meta:
        model = WeatherObservation
        fields = "__all__"
