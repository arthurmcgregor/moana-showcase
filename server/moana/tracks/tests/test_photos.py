from datetime import datetime, timedelta, timezone
from unittest.mock import patch

import pytest
import requests
from django.test import override_settings
from django.urls import reverse
from rest_framework.test import APIClient

from tracks.immich import ImmichClient, ImmichError
from tracks.models import Journey, Track, Vessel
from tracks.photos import journey_photo_window


# ---------- journey_photo_window (pure) ----------


@pytest.fixture
def vessel(db):
    return Vessel.objects.create(name="Test")


def _journey_with_tracks(vessel, track_windows):
    journey = Journey.objects.create(
        vessel=vessel,
        name="J",
        started_at=datetime(2026, 1, 1, 9, 0, tzinfo=timezone.utc),
        ended_at=datetime(2026, 1, 1, 17, 0, tzinfo=timezone.utc),
    )
    for start, end in track_windows:
        Track.objects.create(vessel=vessel, started_at=start, ended_at=end)
    return journey


def test_window_spans_multiple_tracks(vessel):
    j = _journey_with_tracks(
        vessel,
        [
            (datetime(2026, 1, 1, 10, 0, tzinfo=timezone.utc),
             datetime(2026, 1, 1, 12, 0, tzinfo=timezone.utc)),
            (datetime(2026, 1, 1, 14, 0, tzinfo=timezone.utc),
             datetime(2026, 1, 1, 16, 0, tzinfo=timezone.utc)),
        ],
    )
    start, end = journey_photo_window(j)
    assert start == datetime(2026, 1, 1, 8, 0, tzinfo=timezone.utc)
    assert end == datetime(2026, 1, 1, 18, 0, tzinfo=timezone.utc)


def test_window_padding_is_two_hours(vessel):
    j = _journey_with_tracks(
        vessel,
        [(datetime(2026, 1, 1, 10, 0, tzinfo=timezone.utc),
          datetime(2026, 1, 1, 11, 0, tzinfo=timezone.utc))],
    )
    start, end = journey_photo_window(j)
    assert end - start == timedelta(hours=5)  # 1h raw + 2h each side


def test_window_falls_back_to_journey_when_no_tracks(vessel):
    j = Journey.objects.create(
        vessel=vessel,
        name="J",
        started_at=datetime(2026, 1, 1, 9, 0, tzinfo=timezone.utc),
        ended_at=datetime(2026, 1, 1, 10, 0, tzinfo=timezone.utc),
    )
    start, end = journey_photo_window(j)
    assert start == datetime(2026, 1, 1, 7, 0, tzinfo=timezone.utc)
    assert end == datetime(2026, 1, 1, 12, 0, tzinfo=timezone.utc)


# ---------- ImmichClient (mocked at HTTP boundary) ----------


class _FakeResponse:
    def __init__(self, status_code=200, json_body=None, content=b"", content_type="image/jpeg"):
        self.status_code = status_code
        self.ok = 200 <= status_code < 300
        self._json = json_body or {}
        self.content = content
        self.headers = {"Content-Type": content_type}
        self.text = ""

    def json(self):
        return self._json


def test_search_assets_happy_path():
    client = ImmichClient("http://immich", "k")
    body = {"assets": {"items": [{"id": "a1"}, {"id": "a2"}], "nextPage": None}}
    with patch("requests.Session.post", return_value=_FakeResponse(json_body=body)) as post:
        assets = client.search_assets(
            datetime(2026, 1, 1, tzinfo=timezone.utc),
            datetime(2026, 1, 2, tzinfo=timezone.utc),
        )
    assert [a["id"] for a in assets] == ["a1", "a2"]
    payload = post.call_args.kwargs["json"]
    assert payload["takenAfter"].startswith("2026-01-01")
    assert payload["takenBefore"].startswith("2026-01-02")


def test_search_assets_raises_on_timeout():
    client = ImmichClient("http://immich", "k")
    with patch("requests.Session.post", side_effect=requests.Timeout()):
        with pytest.raises(ImmichError):
            client.search_assets(
                datetime(2026, 1, 1, tzinfo=timezone.utc),
                datetime(2026, 1, 2, tzinfo=timezone.utc),
            )


def test_search_assets_raises_on_500():
    client = ImmichClient("http://immich", "k")
    with patch("requests.Session.post", return_value=_FakeResponse(status_code=500)):
        with pytest.raises(ImmichError):
            client.search_assets(
                datetime(2026, 1, 1, tzinfo=timezone.utc),
                datetime(2026, 1, 2, tzinfo=timezone.utc),
            )


@override_settings(IMMICH_URL=None, IMMICH_API_KEY=None)
def test_from_settings_returns_none_when_unconfigured():
    assert ImmichClient.from_settings() is None


# ---------- View tests ----------


@pytest.fixture
def journey(vessel):
    j = Journey.objects.create(
        vessel=vessel,
        name="J",
        started_at=datetime(2026, 1, 1, 10, 0, tzinfo=timezone.utc),
        ended_at=datetime(2026, 1, 1, 14, 0, tzinfo=timezone.utc),
    )
    Track.objects.create(
        vessel=vessel,
        started_at=datetime(2026, 1, 1, 10, 0, tzinfo=timezone.utc),
        ended_at=datetime(2026, 1, 1, 14, 0, tzinfo=timezone.utc),
    )
    return j


@override_settings(IMMICH_URL=None, IMMICH_API_KEY=None, IMMICH_PUBLIC_URL=None)
def test_photos_endpoint_empty_when_unconfigured(journey):
    url = reverse("tracks:journey-photos", kwargs={"pk": journey.id})
    response = APIClient().get(url)
    assert response.status_code == 200
    assert response.json() == {"photos": [], "immichPublicUrl": None, "window": None}


@override_settings(
    IMMICH_URL="http://immich",
    IMMICH_API_KEY="k",
    IMMICH_PUBLIC_URL="https://photos.example.com",
)
def test_photos_endpoint_maps_assets(journey):
    assets_body = {
        "assets": {
            "items": [
                {
                    "id": "with-gps",
                    "type": "IMAGE",
                    "fileCreatedAt": "2026-01-01T11:00:00Z",
                    "exifInfo": {"latitude": -43.5, "longitude": 172.6},
                },
                {
                    "id": "no-gps",
                    "type": "VIDEO",
                    "fileCreatedAt": "2026-01-01T12:00:00Z",
                    "exifInfo": {},
                },
            ],
            "nextPage": None,
        }
    }
    url = reverse("tracks:journey-photos", kwargs={"pk": journey.id})
    with patch("requests.Session.post", return_value=_FakeResponse(json_body=assets_body)) as post:
        response = APIClient().get(url)

    assert response.status_code == 200
    body = response.json()
    assert body["immichPublicUrl"] == "https://photos.example.com"
    assert body["window"]["start"].startswith("2026-01-01T08:00:00")
    assert body["window"]["end"].startswith("2026-01-01T16:00:00")
    assert len(body["photos"]) == 2

    gps_photo = next(p for p in body["photos"] if p["id"] == "with-gps")
    assert gps_photo["lat"] == -43.5
    assert gps_photo["lng"] == 172.6
    assert gps_photo["type"] == "IMAGE"
    assert gps_photo["immichUrl"] == "https://photos.example.com/photos/with-gps"
    assert "/photos/with-gps/thumb" in gps_photo["thumbUrl"]
    assert "/photos/with-gps/preview" in gps_photo["previewUrl"]

    no_gps = next(p for p in body["photos"] if p["id"] == "no-gps")
    assert "lat" not in no_gps
    assert "lng" not in no_gps
    assert no_gps["type"] == "VIDEO"

    # Window passed to Immich is padded ±2h around the tracks
    payload = post.call_args.kwargs["json"]
    assert payload["takenAfter"].startswith("2026-01-01T08:00:00")
    assert payload["takenBefore"].startswith("2026-01-01T16:00:00")


@override_settings(IMMICH_URL="http://immich", IMMICH_API_KEY="k", IMMICH_PUBLIC_URL="")
def test_photo_thumb_proxies_bytes(journey):
    url = reverse("tracks:journey-photo-thumb", kwargs={"pk": journey.id, "asset_id": "x"})
    fake = _FakeResponse(content=b"JPEGBYTES", content_type="image/jpeg")
    with patch("requests.Session.get", return_value=fake):
        response = APIClient().get(url)
    assert response.status_code == 200
    assert response.content == b"JPEGBYTES"
    assert response["Content-Type"] == "image/jpeg"
    assert "immutable" in response["Cache-Control"]


@override_settings(IMMICH_URL=None, IMMICH_API_KEY=None)
def test_photo_thumb_404_when_unconfigured(journey):
    url = reverse("tracks:journey-photo-thumb", kwargs={"pk": journey.id, "asset_id": "x"})
    response = APIClient().get(url)
    assert response.status_code == 404


@override_settings(IMMICH_URL="http://immich", IMMICH_API_KEY="k", IMMICH_PUBLIC_URL="")
def test_photo_preview_proxies_bytes(journey):
    url = reverse("tracks:journey-photo-preview", kwargs={"pk": journey.id, "asset_id": "x"})
    fake = _FakeResponse(content=b"PREVIEWBYTES", content_type="image/jpeg")
    with patch("requests.Session.get", return_value=fake) as get:
        response = APIClient().get(url)
    assert response.status_code == 200
    assert response.content == b"PREVIEWBYTES"
    assert "immutable" in response["Cache-Control"]
    assert get.call_args.kwargs["params"] == {"size": "preview"}


@override_settings(IMMICH_URL="http://immich", IMMICH_API_KEY="k", IMMICH_PUBLIC_URL="")
def test_photos_endpoint_returns_502_when_immich_unavailable(journey):
    url = reverse("tracks:journey-photos", kwargs={"pk": journey.id})
    with patch("requests.Session.post", side_effect=requests.Timeout()):
        response = APIClient().get(url)
    assert response.status_code == 502
    body = response.json()
    assert body["error"]
    assert body["window"]["start"].startswith("2026-01-01T08:00:00")


def test_search_assets_pages_through_results():
    client = ImmichClient("http://immich", "k")
    page1 = _FakeResponse(json_body={"assets": {"items": [{"id": "a1"}], "nextPage": "2"}})
    page2 = _FakeResponse(json_body={"assets": {"items": [{"id": "a2"}, {"id": "a3"}], "nextPage": None}})
    with patch("requests.Session.post", side_effect=[page1, page2]) as post:
        assets = client.search_assets(
            datetime(2026, 1, 1, tzinfo=timezone.utc),
            datetime(2026, 1, 2, tzinfo=timezone.utc),
        )
    assert [a["id"] for a in assets] == ["a1", "a2", "a3"]
    assert post.call_count == 2
    assert post.call_args_list[0].kwargs["json"]["page"] == 1
    assert post.call_args_list[1].kwargs["json"]["page"] == 2
