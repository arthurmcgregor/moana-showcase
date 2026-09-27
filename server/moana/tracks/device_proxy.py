"""Server-side proxy to the on-vessel ESP32 device HTTP API (#92).

The device serves plain HTTP on the local network and cannot present a TLS
certificate, so a browser blocks direct calls from the HTTPS web app as mixed
active content. The hosted frontend instead calls these same-origin
``/api/tracks/device/*`` endpoints and Django forwards them to the device over
the LAN (``settings.DEVICE_URL``). Permissions mirror the rest of the API via
``IsAdminOrReadOnly`` — anyone may list/download, only staff may delete.
"""

from __future__ import annotations

import logging

import requests
from django.conf import settings
from django.http import HttpResponse, JsonResponse, StreamingHttpResponse
from rest_framework.decorators import api_view, permission_classes
from rest_framework.request import Request

from .permissions import IsAdminOrReadOnly

log = logging.getLogger(__name__)

# The device briefly blocks list/delete while syncing to the SD card; keep these
# short so an offline or unreachable device fails fast instead of pinning a
# gunicorn worker.
CONTROL_TIMEOUT = 10
# A ~1 h track streams off flash at ~640 KB; allow generous time for the body.
DOWNLOAD_TIMEOUT = 60
STREAM_CHUNK = 16 * 1024


def _device_base() -> str | None:
    base = (settings.DEVICE_URL or "").rstrip("/")
    return base or None


def _passthrough(resp: requests.Response) -> HttpResponse:
    """Relay an upstream JSON/text response verbatim (status + content type)."""
    return HttpResponse(
        resp.content,
        status=resp.status_code,
        content_type=resp.headers.get("Content-Type", "application/json"),
    )


def _unreachable() -> JsonResponse:
    return JsonResponse({"error": "Device unreachable"}, status=502)


def _not_configured() -> JsonResponse:
    return JsonResponse({"error": "Device URL not configured"}, status=503)


@api_view(["GET"])
@permission_classes([IsAdminOrReadOnly])
def tracks_list(request: Request) -> HttpResponse:
    """Proxy ``GET /tracks/list`` on the device."""
    base = _device_base()
    if not base:
        return _not_configured()
    try:
        resp = requests.get(f"{base}/tracks/list", timeout=CONTROL_TIMEOUT)
    except requests.RequestException as exc:
        log.warning("device tracks/list failed: %s", exc)
        return _unreachable()
    return _passthrough(resp)


@api_view(["GET", "DELETE"])
@permission_classes([IsAdminOrReadOnly])
def track_file(request: Request) -> HttpResponse:
    """Proxy ``GET /track?file=`` (download) and ``DELETE /track?file=`` on the device."""
    base = _device_base()
    if not base:
        return _not_configured()

    filename = request.query_params.get("file")
    if not filename:
        return JsonResponse({"error": "No file parameter specified"}, status=400)

    if request.method == "DELETE":
        try:
            resp = requests.delete(
                f"{base}/track", params={"file": filename}, timeout=CONTROL_TIMEOUT
            )
        except requests.RequestException as exc:
            log.warning("device track delete failed: %s", exc)
            return _unreachable()
        return _passthrough(resp)

    # GET — stream the CSV straight through so Django never buffers a whole track.
    try:
        upstream = requests.get(
            f"{base}/track",
            params={"file": filename},
            stream=True,
            timeout=DOWNLOAD_TIMEOUT,
        )
    except requests.RequestException as exc:
        log.warning("device track download failed: %s", exc)
        return _unreachable()

    if not upstream.ok:
        try:
            return _passthrough(upstream)
        finally:
            upstream.close()

    response = StreamingHttpResponse(
        upstream.iter_content(chunk_size=STREAM_CHUNK),
        content_type=upstream.headers.get("Content-Type", "text/csv"),
    )
    for header in ("Content-Disposition", "Content-Length"):
        if header in upstream.headers:
            response[header] = upstream.headers[header]
    return response
