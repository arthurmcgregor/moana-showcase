from rest_framework import serializers

from .models import ChecklistState, Item, Tag


class TagSerializer(serializers.ModelSerializer):
    class Meta:
        model = Tag
        fields = ["id", "name", "parent"]


class ItemSerializer(serializers.ModelSerializer):
    tags = serializers.PrimaryKeyRelatedField(
        many=True, queryset=Tag.objects.all(), required=False
    )

    class Meta:
        model = Item
        fields = ["id", "name", "done", "tags", "created_at", "updated_at"]
        read_only_fields = ["created_at", "updated_at"]


class ChecklistStateSerializer(serializers.ModelSerializer):
    selected_tags = serializers.PrimaryKeyRelatedField(
        many=True, queryset=Tag.objects.all(), required=False
    )

    class Meta:
        model = ChecklistState
        fields = ["selected_tags", "updated_at"]
        read_only_fields = ["updated_at"]
