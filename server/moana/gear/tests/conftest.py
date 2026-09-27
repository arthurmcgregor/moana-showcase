import pytest
from django.contrib.auth import get_user_model
from rest_framework.test import APIClient

from gear.models import Tag


@pytest.fixture
def user(db):
    User = get_user_model()
    return User.objects.create_user(username="alice", password="x")


@pytest.fixture
def auth_client(user):
    client = APIClient()
    client.force_authenticate(user=user)
    return client


@pytest.fixture
def anon_client():
    return APIClient()


@pytest.fixture
def seeded_tags(db):
    """Return {parent_name: {child_name: Tag}} from the migration seed."""
    result = {}
    for parent in Tag.objects.filter(parent__isnull=True):
        result[parent.name] = {c.name: c for c in parent.children.all()}
        result[parent.name]["_self"] = parent
    return result
