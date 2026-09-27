from django.db import models


class Tag(models.Model):
    name = models.CharField(max_length=100)
    parent = models.ForeignKey(
        "self",
        on_delete=models.CASCADE,
        null=True,
        blank=True,
        related_name="children",
    )

    class Meta:
        ordering = ["parent_id", "name"]
        constraints = [
            models.UniqueConstraint(
                fields=["parent", "name"],
                name="gear_tag_unique_name_per_parent",
            ),
        ]

    def __str__(self):
        if self.parent_id:
            return f"{self.parent.name}/{self.name}"
        return self.name


class Item(models.Model):
    name = models.CharField(max_length=255)
    done = models.BooleanField(default=False)
    tags = models.ManyToManyField(Tag, related_name="items", blank=True)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        ordering = ["name"]

    def __str__(self):
        return self.name


class ChecklistState(models.Model):
    """Singleton holding the currently-selected tag filters for the checklist."""

    selected_tags = models.ManyToManyField(Tag, related_name="+", blank=True)
    updated_at = models.DateTimeField(auto_now=True)

    @classmethod
    def load(cls):
        obj, _ = cls.objects.get_or_create(pk=1)
        return obj

    def save(self, *args, **kwargs):
        self.pk = 1
        super().save(*args, **kwargs)

    def delete(self, *args, **kwargs):
        pass
