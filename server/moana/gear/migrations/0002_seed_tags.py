from django.db import migrations


SEED = {
    "reminder": ["prep", "pack", "rig", "derig", "post"],
    "boat": [
        "forward-hatches",
        "lower-rear-hatches",
        "upper-rear-hatches",
        "cockpit",
    ],
    "trip_length": ["short", "long", "overnight"],
    "crew": ["solo", "friends", "kids"],
    "propulsion": ["sail", "row", "motor"],
    "action": ["stow", "charge"],
}


def seed_tags(apps, schema_editor):
    Tag = apps.get_model("gear", "Tag")
    for parent_name, children in SEED.items():
        parent, _ = Tag.objects.get_or_create(name=parent_name, parent=None)
        for child_name in children:
            Tag.objects.get_or_create(name=child_name, parent=parent)


def unseed_tags(apps, schema_editor):
    Tag = apps.get_model("gear", "Tag")
    parent_names = list(SEED.keys())
    Tag.objects.filter(parent__name__in=parent_names).delete()
    Tag.objects.filter(name__in=parent_names, parent__isnull=True).delete()


class Migration(migrations.Migration):
    dependencies = [
        ("gear", "0001_initial"),
    ]

    operations = [
        migrations.RunPython(seed_tags, unseed_tags),
    ]
