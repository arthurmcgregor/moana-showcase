from drf_spectacular.utils import extend_schema
from rest_framework import status, viewsets
from rest_framework.decorators import action
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from rest_framework.views import APIView

from .models import ChecklistState, Item, Tag
from .serializers import ChecklistStateSerializer, ItemSerializer, TagSerializer


class TagViewSet(viewsets.ModelViewSet):
    queryset = Tag.objects.all()
    serializer_class = TagSerializer
    permission_classes = [IsAuthenticated]


class ItemViewSet(viewsets.ModelViewSet):
    queryset = Item.objects.prefetch_related("tags").all()
    serializer_class = ItemSerializer
    permission_classes = [IsAuthenticated]

    @action(detail=False, methods=["post"], url_path="reset-done")
    def reset_done(self, request):
        updated = Item.objects.filter(done=True).update(done=False)
        return Response({"updated": updated}, status=status.HTTP_200_OK)


class ChecklistStateView(APIView):
    """Singleton endpoint: GET to read, PATCH to update selected tags."""

    permission_classes = [IsAuthenticated]
    serializer_class = ChecklistStateSerializer

    @extend_schema(responses=ChecklistStateSerializer)
    def get(self, request):
        state = ChecklistState.load()
        return Response(ChecklistStateSerializer(state).data)

    @extend_schema(request=ChecklistStateSerializer, responses=ChecklistStateSerializer)
    def patch(self, request):
        state = ChecklistState.load()
        serializer = ChecklistStateSerializer(state, data=request.data, partial=True)
        serializer.is_valid(raise_exception=True)
        serializer.save()
        return Response(serializer.data)
