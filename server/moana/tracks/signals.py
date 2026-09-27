from django.db.models import Q
from django.db.models.signals import post_delete, post_save, pre_save
from django.dispatch import receiver

from .models import Journey, Track


@receiver(pre_save, sender=Journey)
def capture_journey_previous_times(sender, instance, **kwargs):
    """Stash the old started_at/ended_at so post_save can detect changes."""
    if instance.pk:
        try:
            old = Journey.objects.get(pk=instance.pk)
            instance._prev_started_at = old.started_at
            instance._prev_ended_at = old.ended_at
        except Journey.DoesNotExist:
            instance._prev_started_at = None
            instance._prev_ended_at = None
    else:
        instance._prev_started_at = None
        instance._prev_ended_at = None


@receiver(post_save, sender=Journey)
def generate_journey_preview_on_save(sender, instance, created, **kwargs):
    """Generate previews on creation or when time range changes."""
    from .previews import generate_all_previews

    if created:
        generate_all_previews(instance)
        return

    times_changed = (
        getattr(instance, "_prev_started_at", None) != instance.started_at
        or getattr(instance, "_prev_ended_at", None) != instance.ended_at
    )
    if times_changed:
        generate_all_previews(instance)


@receiver(post_delete, sender=Journey)
def clear_journey_preview_on_delete(sender, instance, **kwargs):
    from .previews import invalidate_previews

    invalidate_previews(instance.pk)


@receiver([post_save, post_delete], sender=Track)
def invalidate_track_preview(sender, instance, **kwargs):
    """Clear previews for the track itself and any overlapping journeys."""
    from .previews import invalidate_previews, invalidate_track_previews

    invalidate_track_previews(instance.pk)

    if not instance.vessel_id:
        return

    journeys = Journey.objects.filter(
        vessel_id=instance.vessel_id,
        started_at__lte=instance.ended_at if instance.ended_at else instance.started_at,
    ).filter(Q(ended_at__isnull=True) | Q(ended_at__gte=instance.started_at))

    for journey in journeys:
        invalidate_previews(journey.pk)


