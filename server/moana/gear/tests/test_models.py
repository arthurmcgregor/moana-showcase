import pytest
from django.db import IntegrityError

from gear.models import ChecklistState, Item, Tag


@pytest.mark.django_db
class TestTagSeed:
    def test_all_parents_seeded(self, seeded_tags):
        assert set(seeded_tags.keys()) == {
            "reminder",
            "stowage",
            "trip_length",
            "crew",
            "propulsion",
        }

    def test_children_seeded(self, seeded_tags):
        assert set(seeded_tags["reminder"].keys()) - {"_self"} == {
            "prep", "pack", "rig", "derig", "post"
        }
        assert set(seeded_tags["stowage"].keys()) - {"_self"} == {
            "forward-hatches",
            "lower-rear-hatches",
            "upper-rear-hatches",
            "cockpit",
            "spare-bedroom",
            "master-wardrobe",
            "under-stairs",
        }
        assert set(seeded_tags["trip_length"].keys()) - {"_self"} == {
            "short", "long", "overnight"
        }
        assert set(seeded_tags["crew"].keys()) - {"_self"} == {"solo", "friends", "kids"}
        assert set(seeded_tags["propulsion"].keys()) - {"_self"} == {"sail", "row", "motor"}


@pytest.mark.django_db
class TestTagModel:
    def test_unique_constraint_per_parent(self, seeded_tags):
        parent = seeded_tags["trip_length"]["_self"]
        with pytest.raises(IntegrityError):
            Tag.objects.create(name="short", parent=parent)

    def test_same_name_allowed_under_different_parents(self, seeded_tags):
        a = Tag.objects.create(name="bucket", parent=None)
        b = Tag.objects.create(name="bucket", parent=seeded_tags["crew"]["_self"])
        assert a.id != b.id

    def test_arbitrary_depth_allowed(self, seeded_tags):
        l1 = Tag.objects.create(name="weather", parent=None)
        l2 = Tag.objects.create(name="wet", parent=l1)
        l3 = Tag.objects.create(name="heavy", parent=l2)
        assert l3.parent.parent == l1


@pytest.mark.django_db
class TestChecklistStateSingleton:
    def test_load_creates_then_returns_same_row(self):
        a = ChecklistState.load()
        b = ChecklistState.load()
        assert a.pk == 1 == b.pk
        assert ChecklistState.objects.count() == 1

    def test_save_pins_pk(self):
        s = ChecklistState(pk=99)
        s.save()
        assert s.pk == 1
        assert ChecklistState.objects.count() == 1

    def test_delete_is_noop(self):
        s = ChecklistState.load()
        s.delete()
        assert ChecklistState.objects.filter(pk=1).exists()


@pytest.mark.django_db
class TestItemModel:
    def test_default_done_false(self):
        item = Item.objects.create(name="Anchor")
        assert item.done is False

    def test_tags_m2m(self, seeded_tags):
        item = Item.objects.create(name="Headtorch")
        item.tags.add(
            seeded_tags["crew"]["kids"],
            seeded_tags["stowage"]["forward-hatches"],
        )
        assert item.tags.count() == 2
