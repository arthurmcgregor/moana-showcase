"""Tests for the on-vessel device proxy (#92)."""

from unittest.mock import patch

import requests
from django.contrib.auth import get_user_model
from django.test import SimpleTestCase, override_settings
from django.urls import reverse
from rest_framework.test import APIClient

DEVICE_URL = "http://192.168.1.39"


class FakeResponse:
    """Minimal stand-in for requests.Response."""

    def __init__(self, status_code=200, content=b"", headers=None):
        self.status_code = status_code
        self.content = content
        self.headers = headers or {}
        self.ok = 200 <= status_code < 300
        self.closed = False

    def iter_content(self, chunk_size=1):
        yield self.content

    def close(self):
        self.closed = True


@override_settings(DEVICE_URL=DEVICE_URL)
class DeviceProxyTestCase(SimpleTestCase):
    def setUp(self):
        self.list_url = reverse("tracks:device-tracks-list")
        self.file_url = reverse("tracks:device-track")
        self.client = APIClient()

    # --- list ---------------------------------------------------------------

    @patch("tracks.device_proxy.requests.get")
    def test_list_proxies_device_response(self, mock_get):
        mock_get.return_value = FakeResponse(
            200, b'{"tracks":["a.csv"]}', {"Content-Type": "application/json"}
        )
        response = self.client.get(self.list_url)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json(), {"tracks": ["a.csv"]})
        mock_get.assert_called_once_with(
            f"{DEVICE_URL}/tracks/list", timeout=10
        )

    @patch("tracks.device_proxy.requests.get", side_effect=requests.ConnectionError())
    def test_list_returns_502_when_device_offline(self, _mock_get):
        response = self.client.get(self.list_url)
        self.assertEqual(response.status_code, 502)
        self.assertIn("error", response.json())

    @override_settings(DEVICE_URL="")
    def test_list_returns_503_when_not_configured(self):
        response = self.client.get(self.list_url)
        self.assertEqual(response.status_code, 503)

    # --- download -----------------------------------------------------------

    @patch("tracks.device_proxy.requests.get")
    def test_download_streams_csv(self, mock_get):
        mock_get.return_value = FakeResponse(
            200,
            b"lat,lng\n-43.5,172.5\n",
            {
                "Content-Type": "text/csv",
                "Content-Disposition": 'attachment; filename="a.csv"',
            },
        )
        response = self.client.get(self.file_url, {"file": "a.csv"})
        self.assertEqual(response.status_code, 200)
        self.assertEqual(b"".join(response.streaming_content), b"lat,lng\n-43.5,172.5\n")
        self.assertEqual(response["Content-Disposition"], 'attachment; filename="a.csv"')
        mock_get.assert_called_once_with(
            f"{DEVICE_URL}/track", params={"file": "a.csv"}, stream=True, timeout=60
        )

    def test_download_requires_file_param(self):
        response = self.client.get(self.file_url)
        self.assertEqual(response.status_code, 400)

    @patch("tracks.device_proxy.requests.get")
    def test_download_passes_through_device_404(self, mock_get):
        fake = FakeResponse(404, b'{"error":"Track not found"}', {"Content-Type": "application/json"})
        mock_get.return_value = fake
        response = self.client.get(self.file_url, {"file": "missing.csv"})
        self.assertEqual(response.status_code, 404)
        self.assertTrue(fake.closed)

    @patch("tracks.device_proxy.requests.get", side_effect=requests.Timeout())
    def test_download_returns_502_when_device_offline(self, _mock_get):
        response = self.client.get(self.file_url, {"file": "a.csv"})
        self.assertEqual(response.status_code, 502)

    # --- delete (staff only) ------------------------------------------------

    @patch("tracks.device_proxy.requests.delete")
    def test_delete_requires_staff(self, mock_delete):
        response = self.client.delete(self.file_url + "?file=a.csv")
        self.assertIn(response.status_code, (401, 403))
        mock_delete.assert_not_called()

    @patch("tracks.device_proxy.requests.delete")
    def test_delete_proxies_for_staff(self, mock_delete):
        mock_delete.return_value = FakeResponse(
            200, b'{"success":true}', {"Content-Type": "application/json"}
        )
        # Unsaved instance — IsAdminOrReadOnly only reads .is_staff, no DB needed.
        staff = get_user_model()(username="admin", is_staff=True)
        self.client.force_authenticate(user=staff)
        response = self.client.delete(self.file_url + "?file=a.csv")
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json(), {"success": True})
        mock_delete.assert_called_once_with(
            f"{DEVICE_URL}/track", params={"file": "a.csv"}, timeout=10
        )
