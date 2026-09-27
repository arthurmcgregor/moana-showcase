from django.db import migrations
from django.db.models import F, Window
from django.db.models.functions import RowNumber


def dedupe(apps, schema_editor):
    Mode = apps.get_model("tracks", "Mode")
    Annotation = apps.get_model("tracks", "Annotation")

    mode_dupes = Mode.objects.annotate(
        rn=Window(
            RowNumber(),
            partition_by=[F("vessel_id"), F("started_at")],
            order_by=F("id").desc(),
        ),
    ).values("id", "rn")
    Mode.objects.filter(
        id__in=[r["id"] for r in mode_dupes if r["rn"] > 1]
    ).delete()

    annotation_dupes = Annotation.objects.annotate(
        rn=Window(
            RowNumber(),
            partition_by=[F("vessel_id"), F("type"), F("timestamp")],
            order_by=F("id").desc(),
        ),
    ).values("id", "rn")
    Annotation.objects.filter(
        id__in=[r["id"] for r in annotation_dupes if r["rn"] > 1]
    ).delete()


class Migration(migrations.Migration):

    dependencies = [
        ("tracks", "0008_journey_weather_station"),
    ]

    operations = [
        migrations.RunPython(dedupe, migrations.RunPython.noop),
        migrations.AlterUniqueTogether(
            name="mode",
            unique_together={("vessel", "started_at")},
        ),
        migrations.AlterUniqueTogether(
            name="annotation",
            unique_together={("vessel", "type", "timestamp")},
        ),
    ]
