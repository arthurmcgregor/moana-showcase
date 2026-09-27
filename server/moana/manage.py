#!/usr/bin/env python
"""Django's command-line utility for administrative tasks."""

import os
import sys

# Set GIS library paths for macOS Homebrew (Linux finds them automatically)
_HOMEBREW_LIB = "/opt/homebrew/lib"
if os.path.isdir(_HOMEBREW_LIB):
    os.environ.setdefault("GDAL_LIBRARY_PATH", f"{_HOMEBREW_LIB}/libgdal.dylib")
    os.environ.setdefault("GEOS_LIBRARY_PATH", f"{_HOMEBREW_LIB}/libgeos_c.dylib")
    os.environ.setdefault("DYLD_LIBRARY_PATH", _HOMEBREW_LIB)


def main():
    """Run administrative tasks."""
    os.environ.setdefault("DJANGO_SETTINGS_MODULE", "moana.settings")
    try:
        from django.core.management import execute_from_command_line
    except ImportError as exc:
        raise ImportError(
            "Couldn't import Django. Are you sure it's installed and "
            "available on your PYTHONPATH environment variable? Did you "
            "forget to activate a virtual environment?"
        ) from exc
    execute_from_command_line(sys.argv)


if __name__ == "__main__":
    main()
