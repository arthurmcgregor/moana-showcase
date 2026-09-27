"""The sailing-mode vocabulary spans C++ and Python, and nothing else checks it.

`shared/espnow_protocol.h` is compiled into both firmwares; `MODE_KINDS` and
`Mode.MODE_CHOICES` live here. The watch picker offers every mode, the device
writes `moana::sailingModeName()` into the events sidecar, and this server
parses those strings back — so a mode added on one side of the language
boundary reaches the server as an unknown kind and is silently skipped on
import (issue #151). `test_events_jsonl.TestModeVocabulary` covers the two
Python halves; these tests read the header itself.

The header is parsed rather than generated because it is hand-written on the
firmware side and the C++ has no build-time view of Django's choices.
"""

import re
from pathlib import Path

import pytest

from tracks.events_jsonl import MODE_KINDS
from tracks.models import Mode

# <repo>/server/moana/tracks/tests/this_file.py -> <repo>/shared/...
HEADER = Path(__file__).resolve().parents[4] / "shared" / "espnow_protocol.h"


@pytest.fixture(scope="module")
def header_text():
    if not HEADER.is_file():
        pytest.fail(
            f"{HEADER} not found. The wire header is the source of this "
            "vocabulary; running the suite without it hides drift rather "
            "than reporting it."
        )
    return HEADER.read_text()


def _block(text, opening):
    """The body of the first `{...}` block after `opening`."""
    start = text.index(opening) + len(opening)
    return text[start : text.index("}", start)]


@pytest.fixture(scope="module")
def enum_modes(header_text):
    """`SailingMode` enumerators in declaration order: [(name, value), ...]."""
    body = _block(header_text, "enum SailingMode : uint8_t {")
    modes = re.findall(r"(MODE_[A-Z0-9_]+)\s*=\s*(\d+)", body)
    assert modes, "could not parse the SailingMode enum"
    return [(name, int(value)) for name, value in modes]


@pytest.fixture(scope="module")
def wire_names(header_text):
    """`sailingModeName()` mapping in switch order: [(MODE_X, "STRING"), ...]."""
    body = _block(header_text, "inline const char* sailingModeName(uint8_t mode) {")
    pairs = re.findall(r"case\s+(MODE_[A-Z0-9_]+):\s*return\s+\"([A-Z0-9_]+)\";", body)
    assert pairs, "could not parse sailingModeName()"
    return pairs


class TestSailingModeVocabulary:
    def test_wire_names_match_mode_kinds(self, wire_names):
        assert {name for _, name in wire_names} == set(MODE_KINDS)

    def test_wire_names_match_mode_choices_in_order(self, wire_names):
        # The enum comment claims one value per MODE_CHOICES entry, in the same
        # order; the watch renders the picker in that order.
        assert [name for _, name in wire_names] == [c[0] for c in Mode.MODE_CHOICES]

    def test_every_selectable_mode_has_a_name(self, enum_modes, wire_names):
        # MODE_UNKNOWN is an echo state, never a selection, and has no server
        # counterpart; every other enumerator must map to a name.
        selectable = [name for name, _ in enum_modes if name != "MODE_UNKNOWN"]
        assert [enumerator for enumerator, _ in wire_names] == selectable

    def test_enum_values_are_contiguous_from_zero(self, enum_modes):
        # isSelectableSailingMode() range-checks MODE_SAIL..MODE_OTHER, which
        # only excludes non-modes while the values have no gaps.
        assert [value for _, value in enum_modes] == list(range(len(enum_modes)))

    def test_selectable_range_spans_the_named_modes(self, header_text, wire_names):
        bounds = re.search(
            r"mode >= (MODE_[A-Z0-9_]+) && mode <= (MODE_[A-Z0-9_]+)", header_text
        )
        assert bounds, "could not parse isSelectableSailingMode()"
        assert bounds.groups() == (wire_names[0][0], wire_names[-1][0])
