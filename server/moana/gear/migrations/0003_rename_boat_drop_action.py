from django.db import migrations


STOWAGE_VALUES = [
    "forward-hatches",
    "lower-rear-hatches",
    "upper-rear-hatches",
    "cockpit",
    "spare-bedroom",
    "master-wardrobe",
    "under-stairs",
]


def forwards(apps, schema_editor):
    Tag = apps.get_model("gear", "Tag")

    # Rename "boat" parent to "stowage" if present.
    boat = Tag.objects.filter(parent__isnull=True, name="boat").first()
    if boat:
        # If a "stowage" parent already exists, merge children into it.
        stowage = Tag.objects.filter(parent__isnull=True, name="stowage").first()
        if stowage:
            Tag.objects.filter(parent=boat).update(parent=stowage)
            boat.delete()
        else:
            boat.name = "stowage"
            boat.save()

    # Ensure stowage parent exists and has all desired values.
    stowage, _ = Tag.objects.get_or_create(name="stowage", parent=None)
    for value in STOWAGE_VALUES:
        Tag.objects.get_or_create(name=value, parent=stowage)

    # Drop the action category and its children (cascades remove M2M links).
    Tag.objects.filter(parent__name="action", parent__parent__isnull=True).delete()
    Tag.objects.filter(name="action", parent__isnull=True).delete()


def backwards(apps, schema_editor):
    Tag = apps.get_model("gear", "Tag")

    # Re-create action + children.
    action, _ = Tag.objects.get_or_create(name="action", parent=None)
    for value in ["stow", "charge"]:
        Tag.objects.get_or_create(name=value, parent=action)

    # Rename stowage back to boat, drop the new values that weren't there before.
    stowage = Tag.objects.filter(parent__isnull=True, name="stowage").first()
    if stowage:
        Tag.objects.filter(
            parent=stowage,
            name__in=["spare-bedroom", "master-wardrobe", "under-stairs"],
        ).delete()
        stowage.name = "boat"
        stowage.save()


class Migration(migrations.Migration):
    dependencies = [
        ("gear", "0002_seed_tags"),
    ]

    operations = [
        migrations.RunPython(forwards, backwards),
    ]
