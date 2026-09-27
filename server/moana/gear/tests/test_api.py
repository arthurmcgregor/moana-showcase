import pytest

from gear.models import ChecklistState, Item, Tag


@pytest.mark.django_db
class TestAuth:
    """All gear endpoints require authentication."""

    @pytest.mark.parametrize(
        "method,url",
        [
            ("get", "/api/gear/items/"),
            ("post", "/api/gear/items/"),
            ("get", "/api/gear/tags/"),
            ("get", "/api/gear/checklist-state/"),
            ("patch", "/api/gear/checklist-state/"),
            ("post", "/api/gear/items/reset-done/"),
        ],
    )
    def test_anon_blocked(self, anon_client, method, url):
        response = getattr(anon_client, method)(url, {}, format="json")
        assert response.status_code in (401, 403)


@pytest.mark.django_db
class TestTagAPI:
    def test_list_seeded(self, auth_client):
        response = auth_client.get("/api/gear/tags/")
        assert response.status_code == 200
        # 5 parents + 21 children
        assert response.data["count"] == 26

    def test_create_child(self, auth_client, seeded_tags):
        parent = seeded_tags["crew"]["_self"]
        response = auth_client.post(
            "/api/gear/tags/",
            {"name": "partner", "parent": parent.id},
            format="json",
        )
        assert response.status_code == 201
        assert response.data["parent"] == parent.id

    def test_update(self, auth_client, seeded_tags):
        tag = seeded_tags["crew"]["solo"]
        response = auth_client.patch(
            f"/api/gear/tags/{tag.id}/", {"name": "alone"}, format="json"
        )
        assert response.status_code == 200
        tag.refresh_from_db()
        assert tag.name == "alone"

    def test_delete(self, auth_client, seeded_tags):
        tag = seeded_tags["crew"]["solo"]
        response = auth_client.delete(f"/api/gear/tags/{tag.id}/")
        assert response.status_code == 204
        assert not Tag.objects.filter(id=tag.id).exists()


@pytest.mark.django_db
class TestItemAPI:
    def test_create_with_tags(self, auth_client, seeded_tags):
        response = auth_client.post(
            "/api/gear/items/",
            {
                "name": "Lifejacket",
                "tags": [
                    seeded_tags["stowage"]["cockpit"].id,
                    seeded_tags["crew"]["kids"].id,
                ],
            },
            format="json",
        )
        assert response.status_code == 201
        assert response.data["done"] is False
        assert set(response.data["tags"]) == {
            seeded_tags["stowage"]["cockpit"].id,
            seeded_tags["crew"]["kids"].id,
        }

    def test_list(self, auth_client, seeded_tags):
        Item.objects.create(name="A")
        Item.objects.create(name="B")
        response = auth_client.get("/api/gear/items/")
        assert response.status_code == 200
        assert response.data["count"] == 2

    def test_toggle_done(self, auth_client):
        item = Item.objects.create(name="Compass")
        response = auth_client.patch(
            f"/api/gear/items/{item.id}/", {"done": True}, format="json"
        )
        assert response.status_code == 200
        item.refresh_from_db()
        assert item.done is True

    def test_delete(self, auth_client):
        item = Item.objects.create(name="Old")
        response = auth_client.delete(f"/api/gear/items/{item.id}/")
        assert response.status_code == 204

    def test_reset_done(self, auth_client):
        Item.objects.create(name="A", done=True)
        Item.objects.create(name="B", done=True)
        Item.objects.create(name="C", done=False)
        response = auth_client.post("/api/gear/items/reset-done/")
        assert response.status_code == 200
        assert response.data["updated"] == 2
        assert Item.objects.filter(done=True).count() == 0
        assert Item.objects.count() == 3


@pytest.mark.django_db
class TestChecklistStateAPI:
    def test_get_initial_empty(self, auth_client):
        response = auth_client.get("/api/gear/checklist-state/")
        assert response.status_code == 200
        assert response.data["selected_tags"] == []

    def test_patch_sets_tags(self, auth_client, seeded_tags):
        ids = [
            seeded_tags["trip_length"]["short"].id,
            seeded_tags["crew"]["solo"].id,
        ]
        response = auth_client.patch(
            "/api/gear/checklist-state/",
            {"selected_tags": ids},
            format="json",
        )
        assert response.status_code == 200
        assert set(response.data["selected_tags"]) == set(ids)
        state = ChecklistState.load()
        assert set(state.selected_tags.values_list("id", flat=True)) == set(ids)

    def test_patch_clears_tags(self, auth_client, seeded_tags):
        state = ChecklistState.load()
        state.selected_tags.add(seeded_tags["crew"]["solo"])
        response = auth_client.patch(
            "/api/gear/checklist-state/",
            {"selected_tags": []},
            format="json",
        )
        assert response.status_code == 200
        assert response.data["selected_tags"] == []

    def test_remains_singleton_after_patch(self, auth_client, seeded_tags):
        auth_client.patch(
            "/api/gear/checklist-state/",
            {"selected_tags": [seeded_tags["crew"]["solo"].id]},
            format="json",
        )
        assert ChecklistState.objects.count() == 1
