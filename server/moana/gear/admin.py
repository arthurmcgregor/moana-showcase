from django.contrib import admin

from .models import ChecklistState, Item, Tag


@admin.register(Tag)
class TagAdmin(admin.ModelAdmin):
    list_display = ["name", "parent"]
    list_filter = ["parent"]
    search_fields = ["name"]


@admin.register(Item)
class ItemAdmin(admin.ModelAdmin):
    list_display = ["name", "done"]
    list_filter = ["done", "tags"]
    search_fields = ["name"]
    filter_horizontal = ["tags"]


@admin.register(ChecklistState)
class ChecklistStateAdmin(admin.ModelAdmin):
    filter_horizontal = ["selected_tags"]

    def has_add_permission(self, request):
        return not ChecklistState.objects.exists()

    def has_delete_permission(self, request, obj=None):
        return False
