from django.urls import include, path
from rest_framework.routers import DefaultRouter

from .viewsets import ChecklistStateView, ItemViewSet, TagViewSet

app_name = "gear"

router = DefaultRouter()
router.register(r"items", ItemViewSet, basename="item")
router.register(r"tags", TagViewSet, basename="tag")

urlpatterns = [
    path("", include(router.urls)),
    path("checklist-state/", ChecklistStateView.as_view(), name="checklist-state"),
]
