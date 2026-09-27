from django.urls import include, path
from rest_framework.routers import DefaultRouter

from . import device_proxy, viewsets

app_name = "tracks"

# Create router and register viewsets
router = DefaultRouter()
router.register(r"vessels", viewsets.VesselViewSet, basename="vessel")
router.register(r"people", viewsets.PersonViewSet, basename="person")
router.register(r"tracks", viewsets.TrackViewSet, basename="track")
router.register(r"track-points", viewsets.TrackPointViewSet, basename="trackpoint")
router.register(r"modes", viewsets.ModeViewSet, basename="mode")
router.register(r"annotations", viewsets.AnnotationViewSet, basename="annotation")
router.register(r"aboard", viewsets.AboardViewSet, basename="aboard")
router.register(r"journeys", viewsets.JourneyViewSet, basename="journey")
router.register(r"legs", viewsets.LegViewSet, basename="leg")
router.register(r"weather-stations", viewsets.WeatherStationViewSet, basename="weatherstation")
router.register(r"weather-observations", viewsets.WeatherObservationViewSet, basename="weatherobservation")

# URL patterns
urlpatterns = [
    # Proxy to the on-vessel device's HTTP API (#92) — declared before the router
    # so these fixed paths win over the router's API root.
    path("device/tracks/list", device_proxy.tracks_list, name="device-tracks-list"),
    path("device/track", device_proxy.track_file, name="device-track"),
    path("", include(router.urls)),
]
