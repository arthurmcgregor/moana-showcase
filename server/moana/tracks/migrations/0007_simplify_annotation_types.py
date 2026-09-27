from django.db import migrations, models


REMOVED_TYPE_LABELS = {
    "REEF": "Reef Sail",
    "SHAKE_REEF": "Shake Out Reef",
    "LOAD_PASSENGERS": "Load Passengers",
    "UNLOAD_PASSENGERS": "Unload Passengers",
    "LOAD_GEAR": "Load Gear",
    "UNLOAD_GEAR": "Unload Gear",
    "WILDLIFE": "Wildlife Sighting",
    "WEATHER": "Weather Observation",
    "WAYPOINT": "Waypoint Reached",
    "MOB": "Man Overboard",
    "HEAVY_WEATHER_PREP": "Prepare for Heavy Weather",
}


def convert_removed_types_to_note(apps, schema_editor):
    """Convert annotations of removed types to NOTE, preserving original
    label as a prefix in the notes field."""
    Annotation = apps.get_model("tracks", "Annotation")
    for annotation in Annotation.objects.filter(type__in=REMOVED_TYPE_LABELS.keys()):
        label = REMOVED_TYPE_LABELS[annotation.type]
        prefix = f"[{label}]"
        if annotation.notes:
            annotation.notes = f"{prefix} {annotation.notes}"
        else:
            annotation.notes = prefix
        annotation.type = "NOTE"
        annotation.save(update_fields=["type", "notes"])


def noop_reverse(apps, schema_editor):
    """Reverse is a no-op: original type information is preserved in the
    notes prefix but not mechanically recoverable."""
    pass


class Migration(migrations.Migration):

    dependencies = [
        ("tracks", "0006_track_tracks_trac_vessel__b696c7_idx_and_more"),
    ]

    operations = [
        migrations.RunPython(convert_removed_types_to_note, noop_reverse),
        migrations.AlterField(
            model_name="annotation",
            name="type",
            field=models.CharField(
                choices=[
                    ("TACK", "Tack"),
                    ("GYBE", "Gybe"),
                    ("COURSE_CHANGE", "Course Change"),
                    ("NOTE", "General Note"),
                ],
                max_length=30,
            ),
        ),
    ]
