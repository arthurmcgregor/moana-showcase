from django.contrib import admin
from django.contrib.gis.admin import GISModelAdmin
from django.utils.html import format_html

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


def _format_duration(duration):
    """Format a timedelta as 'Xh Ym' or 'Ym'. Returns Ongoing HTML for None."""
    if duration is None:
        return format_html('<em style="color: {};">Ongoing</em>', "#10b981")
    total_seconds = int(duration.total_seconds())
    hours = total_seconds // 3600
    minutes = (total_seconds % 3600) // 60
    if hours > 0:
        return f"{hours}h {minutes}m"
    return f"{minutes}m"


@admin.register(Vessel)
class VesselAdmin(admin.ModelAdmin):
    list_display = ["name", "vessel_type", "created_at", "track_count"]
    search_fields = ["name", "vessel_type", "description"]
    list_filter = ["vessel_type", "created_at"]
    ordering = ["-created_at"]

    def track_count(self, obj):
        return obj.tracks.count()

    track_count.short_description = "Tracks"


@admin.register(Person)
class PersonAdmin(admin.ModelAdmin):
    list_display = ["last_name", "first_name", "email", "created_at"]
    search_fields = ["first_name", "last_name", "email"]
    ordering = ["last_name", "first_name"]
    list_filter = ["created_at"]

    def get_queryset(self, request):
        qs = super().get_queryset(request)
        return qs.prefetch_related("aboard_records")


@admin.register(Track)
class TrackAdmin(admin.ModelAdmin):
    list_display = [
        "name_or_id",
        "vessel",
        "started_at",
        "ended_at",
        "duration",
        "points_count",
    ]
    list_filter = ["vessel", "started_at"]
    search_fields = ["name", "description", "vessel__name"]
    date_hierarchy = "started_at"
    ordering = ["-started_at"]
    raw_id_fields = ["vessel"]
    list_select_related = ["vessel"]
    readonly_fields = [
        "imported_at",
        "points_summary",
        "track_statistics",
        "api_links",
    ]
    fieldsets = (
        (
            "Track Information",
            {"fields": ("name", "description", "vessel", "source_filename")},
        ),
        ("Timestamps", {"fields": ("started_at", "ended_at", "imported_at")}),
        (
            "Track Data",
            {
                "fields": ("points_summary", "track_statistics", "api_links"),
                "description": "Track points are managed via CSV upload. Use the API links below to view track data.",
            },
        ),
    )

    def points_summary(self, obj):
        if obj.pk:
            count = getattr(obj, "points_count_cache", None)
            if count is None:
                count = obj.points.count()

            if count > 0:
                from django.db.models import Max, Min

                ts = obj.points.aggregate(
                    first_ts=Min("timestamp"), last_ts=Max("timestamp")
                )
                return format_html(
                    "<strong>{}</strong> track points<br>"
                    "<strong>First:</strong> {}<br>"
                    "<strong>Last:</strong> {}",
                    f"{count:,}",
                    ts["first_ts"].strftime("%Y-%m-%d %H:%M:%S UTC") if ts["first_ts"] else "-",
                    ts["last_ts"].strftime("%Y-%m-%d %H:%M:%S UTC") if ts["last_ts"] else "-",
                )
            return format_html('<em style="color: #6b7280;">No points recorded</em>')
        return "-"

    points_summary.short_description = "Track Points"

    def track_statistics(self, obj):
        if obj.pk:
            # Use cached count if available from get_queryset annotation
            if hasattr(obj, "points_count_cache"):
                count = obj.points_count_cache
            else:
                count = obj.points.count()

            if count > 0:
                from django.db.models import Avg, Max, Min

                stats = obj.points.aggregate(
                    avg_speed=Avg("speed"),
                    max_speed=Max("speed"),
                    min_altitude=Min("altitude"),
                    max_altitude=Max("altitude"),
                    avg_satellites=Avg("satellites"),
                )
                return format_html(
                    "<strong>Speed:</strong> avg {}, max {}<br>"
                    "<strong>Altitude:</strong> {} to {} m<br>"
                    "<strong>Satellites:</strong> avg {}",
                    f"{stats['avg_speed']:.1f}" if stats["avg_speed"] else "0.0",
                    f"{stats['max_speed']:.1f}" if stats["max_speed"] else "0.0",
                    f"{stats['min_altitude']:.1f}"
                    if stats["min_altitude"] is not None
                    else "-",
                    f"{stats['max_altitude']:.1f}"
                    if stats["max_altitude"] is not None
                    else "-",
                    f"{stats['avg_satellites']:.1f}"
                    if stats["avg_satellites"]
                    else "0.0",
                )
            return "-"
        return "-"

    track_statistics.short_description = "Statistics"

    def api_links(self, obj):
        if obj.pk:
            return format_html(
                '<a href="/api/tracks/tracks/{}/" target="_blank" style="margin-right: 10px;">📋 JSON</a>'
                '<a href="/api/tracks/tracks/{}/points/" target="_blank" style="margin-right: 10px;">📍 Points</a>'
                '<a href="/api/tracks/tracks/{}/geojson/" target="_blank">🗺️ GeoJSON</a>',
                obj.pk,
                obj.pk,
                obj.pk,
            )
        return "-"

    api_links.short_description = "API Links"

    def get_queryset(self, request):
        qs = super().get_queryset(request)
        # Annotate with points count to avoid N+1 queries
        from django.db.models import Count

        return qs.annotate(points_count_cache=Count("points"))

    def get_object(self, request, object_id, from_field=None):
        """Override to ensure single object retrieval also has annotation."""
        obj = super().get_object(request, object_id, from_field)
        if obj and not hasattr(obj, "points_count_cache"):
            from django.db.models import Count

            annotated = (
                Track.objects.filter(pk=obj.pk)
                .annotate(points_count_cache=Count("points"))
                .first()
            )
            if annotated:
                obj.points_count_cache = annotated.points_count_cache
        return obj

    def name_or_id(self, obj):
        return obj.name if obj.name else f"Track {obj.pk}"

    name_or_id.short_description = "Name"
    name_or_id.admin_order_field = "name"

    def points_count(self, obj):
        # Use annotated count if available, otherwise query
        if hasattr(obj, "points_count_cache"):
            return obj.points_count_cache
        return obj.points.count()

    points_count.short_description = "Points"
    points_count.admin_order_field = "points_count_cache"

    def duration(self, obj):
        if obj.ended_at and obj.started_at:
            return _format_duration(obj.ended_at - obj.started_at)
        return "-"

    duration.short_description = "Duration"


@admin.register(TrackPoint)
class TrackPointAdmin(GISModelAdmin):
    list_display = [
        "track",
        "timestamp",
        "coordinates",
        "speed",
        "satellites",
        "altitude",
    ]
    list_filter = ["track__vessel", "timestamp"]
    search_fields = ["track__name", "track__vessel__name"]
    date_hierarchy = "timestamp"
    ordering = ["-timestamp"]
    raw_id_fields = ["track"]
    list_select_related = ["track", "track__vessel"]

    def get_queryset(self, request):
        qs = super().get_queryset(request)
        # Use select_related to avoid N+1 queries
        return qs.select_related("track", "track__vessel")

    def coordinates(self, obj):
        if obj.location:
            return f"({obj.location.y:.6f}, {obj.location.x:.6f})"
        return "-"

    coordinates.short_description = "Lat, Lng"


@admin.register(Mode)
class ModeAdmin(admin.ModelAdmin):
    list_display = [
        "vessel",
        "mode_display",
        "started_at",
        "ended_at_display",
        "duration_display",
    ]
    list_filter = ["vessel", "mode", "started_at"]
    search_fields = ["vessel__name", "notes"]
    date_hierarchy = "started_at"
    ordering = ["-started_at"]
    raw_id_fields = ["vessel"]

    def mode_display(self, obj):
        colors = {
            "SAIL": "#3b82f6",  # blue
            "MOTORSAIL": "#8b5cf6",  # purple
            "MOTOR": "#ef4444",  # red
            "ROW": "#10b981",  # green
            "UNPOWERED": "#6b7280",  # gray
            "ANCHOR": "#f59e0b",  # amber
            "ALONGSIDE": "#ec4899",  # pink
            "BEACHED": "#f97316",  # orange
            "TRAILERED": "#64748b",  # slate
        }
        color = colors.get(obj.mode, "#000000")
        return format_html(
            '<span style="color: {}; font-weight: bold;">●</span> {}',
            color,
            obj.get_mode_display(),
        )

    mode_display.short_description = "Mode"
    mode_display.admin_order_field = "mode"

    def ended_at_display(self, obj):
        ended = obj.get_ended_at()
        if ended:
            return ended.strftime("%Y-%m-%d %H:%M")
        return format_html('<em style="color: #10b981;">Ongoing</em>')

    ended_at_display.short_description = "Ended At"

    def duration_display(self, obj):
        return _format_duration(obj.get_duration())

    duration_display.short_description = "Duration"


@admin.register(Annotation)
class AnnotationAdmin(admin.ModelAdmin):
    list_display = ["type_display", "vessel", "timestamp", "notes_preview"]
    list_filter = ["vessel", "type", "timestamp"]
    search_fields = ["vessel__name", "notes", "metadata"]
    date_hierarchy = "timestamp"
    ordering = ["-timestamp"]
    raw_id_fields = ["vessel"]

    def type_display(self, obj):
        icons = {
            "TACK": "⛵",
            "GYBE": "⛵",
            "COURSE_CHANGE": "🧭",
            "NOTE": "📝",
        }
        icon = icons.get(obj.type, "•")
        return f"{icon} {obj.get_type_display()}"

    type_display.short_description = "Type"
    type_display.admin_order_field = "type"

    def notes_preview(self, obj):
        if obj.notes:
            return obj.notes[:50] + "..." if len(obj.notes) > 50 else obj.notes
        return "-"

    notes_preview.short_description = "Notes"


@admin.register(Aboard)
class AboardAdmin(admin.ModelAdmin):
    list_display = [
        "person",
        "vessel",
        "started_at",
        "ended_at",
        "duration_display",
        "status",
    ]
    list_filter = ["vessel", "person", "started_at"]
    search_fields = [
        "person__first_name",
        "person__last_name",
        "vessel__name",
        "notes",
    ]
    date_hierarchy = "started_at"
    ordering = ["-started_at"]
    raw_id_fields = ["vessel", "person"]
    list_select_related = ["vessel", "person"]

    def status(self, obj):
        if obj.is_currently_aboard():
            return format_html(
                '<span style="color: {}; font-weight: bold;">● Aboard</span>', "#10b981"
            )
        return format_html('<span style="color: {};">○ Ashore</span>', "#6b7280")

    status.short_description = "Status"

    def duration_display(self, obj):
        return _format_duration(obj.get_duration())

    duration_display.short_description = "Duration"


class LegInline(admin.TabularInline):
    model = Leg
    extra = 0
    fields = ["name", "started_at", "ended_at", "notes"]
    show_change_link = True


@admin.register(Journey)
class JourneyAdmin(admin.ModelAdmin):
    list_display = [
        "name",
        "vessel",
        "started_at",
        "ended_at",
        "duration_display",
        "legs_count",
    ]
    list_filter = ["vessel", "started_at"]
    search_fields = ["name", "description", "vessel__name"]
    date_hierarchy = "started_at"
    ordering = ["-started_at"]
    inlines = [LegInline]
    raw_id_fields = ["vessel"]

    def legs_count(self, obj):
        if hasattr(obj, "legs_count_cache"):
            return obj.legs_count_cache
        return obj.legs.count()

    legs_count.short_description = "Legs"

    def get_queryset(self, request):
        from django.db.models import Count

        return super().get_queryset(request).annotate(legs_count_cache=Count("legs"))

    def duration_display(self, obj):
        return _format_duration(obj.get_duration())

    duration_display.short_description = "Duration"


@admin.register(Leg)
class LegAdmin(admin.ModelAdmin):
    list_display = [
        "name",
        "journey",
        "started_at",
        "ended_at",
        "duration_display",
    ]
    list_filter = ["journey__vessel", "started_at"]
    search_fields = ["name", "notes", "journey__name"]
    date_hierarchy = "started_at"
    ordering = ["journey", "started_at"]
    raw_id_fields = ["journey"]

    def duration_display(self, obj):
        return _format_duration(obj.get_duration())

    duration_display.short_description = "Duration"


@admin.register(WeatherStation)
class WeatherStationAdmin(admin.ModelAdmin):
    list_display = ["name", "created_at"]
    list_filter = ["created_at"]
    ordering = ["name"]


@admin.register(WeatherObservation)
class WeatherObservationAdmin(admin.ModelAdmin):
    list_display = ["station", "timestamp", "wind_direction", "wind_speed", "gust_speed"]
    list_filter = ["station", "timestamp"]
    ordering = ["-timestamp"]


# Customize the admin site header and title
admin.site.site_header = "Moana Sailing Tracker Admin"
admin.site.site_title = "Moana Admin"
admin.site.index_title = "Sailing Data Management"
