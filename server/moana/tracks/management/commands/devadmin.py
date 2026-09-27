"""Create (or refresh) the local development admin account.

Users themselves arrive via `RemoteUserMiddleware` — from Authelia deployed, from
`DevRemoteUserMiddleware`'s fake header locally — which flips `is_staff` on
`moana_admin` group membership but sets no password and no superuser bit. This
command adds those two: a known password for `/admin/` with `DEV_AUTO_LOGIN=0`,
and superuser so the admin site shows every model. Deliberately idempotent — run
it on every `mise run dev` and it creates the user or resets it to a known state.
"""

from django.conf import settings
from django.contrib.auth import get_user_model
from django.core.management.base import BaseCommand, CommandError


class Command(BaseCommand):
    help = "Create or refresh the local dev superuser (DEV_ADMIN_USERNAME/PASSWORD)."

    def add_arguments(self, parser):
        parser.add_argument(
            "--username",
            default=settings.DEV_ADMIN_USERNAME,
            help="Username to create or promote (default: DEV_ADMIN_USERNAME).",
        )
        parser.add_argument(
            "--password",
            default=settings.DEV_ADMIN_PASSWORD,
            help="Password to set (default: DEV_ADMIN_PASSWORD).",
        )
        parser.add_argument("--email", default="", help="Optional email address.")

    def handle(self, *args, **options):
        # The password is a known value out of server/.env, so this must never run
        # against a real deployment. settings_prod sets DEBUG = False.
        if not settings.DEBUG:
            raise CommandError(
                "devadmin only runs with DEBUG on — it sets a known password from "
                "server/.env. Use `manage.py createsuperuser` on a real deployment."
            )

        username = options["username"]
        user_model = get_user_model()

        user, created = user_model.objects.get_or_create(
            username=username,
            defaults={"email": options["email"]},
        )
        user.is_staff = True
        user.is_superuser = True
        user.is_active = True
        if options["email"]:
            user.email = options["email"]
        user.set_password(options["password"])
        user.save()

        verb = "Created" if created else "Refreshed"
        self.stdout.write(
            self.style.SUCCESS(
                f"{verb} dev admin '{username}' (password '{options['password']}') — "
                "staff and superuser."
            )
        )
