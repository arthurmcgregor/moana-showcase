import pytest


# -- OpenAPI schema snapshot --


@pytest.mark.django_db
def test_openapi_schema(api_client, snapshot):
    response = api_client.get("/api/schema/", HTTP_ACCEPT="application/json")
    assert response.status_code == 200
    assert response.json() == snapshot


# -- Vessel snapshots --


@pytest.mark.django_db
def test_vessel_list(api_client, vessel, sanitize, snapshot):
    response = api_client.get("/api/tracks/vessels/")
    assert response.status_code == 200
    assert sanitize(response.json()) == snapshot


@pytest.mark.django_db
def test_vessel_detail(api_client, vessel, sanitize, snapshot):
    response = api_client.get(f"/api/tracks/vessels/{vessel.id}/")
    assert response.status_code == 200
    assert sanitize(response.json()) == snapshot


# -- Person snapshots --


@pytest.mark.django_db
def test_person_list(api_client, person, sanitize, snapshot):
    response = api_client.get("/api/tracks/people/")
    assert response.status_code == 200
    assert sanitize(response.json()) == snapshot


@pytest.mark.django_db
def test_person_detail(api_client, person, sanitize, snapshot):
    response = api_client.get(f"/api/tracks/people/{person.id}/")
    assert response.status_code == 200
    assert sanitize(response.json()) == snapshot


# -- Journey snapshots (complex, high-value) --


@pytest.mark.django_db
def test_journey_detail(api_client, journey_with_full_data, sanitize, snapshot):
    journey = journey_with_full_data
    response = api_client.get(f"/api/tracks/journeys/{journey.id}/")
    assert response.status_code == 200
    assert sanitize(response.json()) == snapshot


@pytest.mark.django_db
def test_journey_modes(api_client, journey_with_full_data, sanitize, snapshot):
    journey = journey_with_full_data
    response = api_client.get(f"/api/tracks/journeys/{journey.id}/modes/")
    assert response.status_code == 200
    assert sanitize(response.json()) == snapshot


@pytest.mark.django_db
def test_journey_annotations(api_client, journey_with_full_data, sanitize, snapshot):
    journey = journey_with_full_data
    response = api_client.get(f"/api/tracks/journeys/{journey.id}/annotations/")
    assert response.status_code == 200
    assert sanitize(response.json()) == snapshot


@pytest.mark.django_db
def test_journey_aboard(api_client, journey_with_full_data, sanitize, snapshot):
    journey = journey_with_full_data
    response = api_client.get(f"/api/tracks/journeys/{journey.id}/aboard/")
    assert response.status_code == 200
    assert sanitize(response.json()) == snapshot


@pytest.mark.django_db
def test_journey_tracks_geojson(api_client, journey_with_full_data, sanitize, snapshot):
    journey = journey_with_full_data
    response = api_client.get(f"/api/tracks/journeys/{journey.id}/tracks/")
    assert response.status_code == 200
    assert sanitize(response.json()) == snapshot


# -- Track GeoJSON snapshot --


@pytest.mark.django_db
def test_track_geojson(api_client, journey_with_full_data, sanitize, snapshot):
    track = journey_with_full_data.track
    response = api_client.get(f"/api/tracks/tracks/{track.id}/geojson/")
    assert response.status_code == 200
    assert sanitize(response.json()) == snapshot
