from datetime import datetime, timedelta, timezone

from tracks.events_jsonl import (
    MAX_WIRE_UTC,
    MODE_DEBOUNCE_SECONDS,
    MODE_KINDS,
    ParsedEvent,
    collapse_mode_bursts,
    events_outside_window,
    parse_events_jsonl,
)
from tracks.models import Mode


class TestModeVocabulary:
    """The sidecar vocabulary is the wire protocol's, but it has to land
    exactly on Mode.MODE_CHOICES — the watch picker offers every choice
    (issue #146), so a gap here silently drops modes on import."""

    def test_covers_every_mode_choice(self):
        assert set(MODE_KINDS.values()) == {c[0] for c in Mode.MODE_CHOICES}

    def test_maps_only_to_valid_mode_choices(self):
        valid = {c[0] for c in Mode.MODE_CHOICES}
        for kind, mode in MODE_KINDS.items():
            assert mode in valid, f"{kind} maps to unknown mode {mode}"


class TestParseEventsJsonl:
    def test_parses_every_mode_in_the_vocabulary(self):
        # The device writes these names from moana::sailingModeName
        # (shared/espnow_protocol.h); all ten must parse into Mode values.
        content = "".join(
            f'{{"kind":"{kind}","utc":{1753900000 + i}}}\n'
            for i, kind in enumerate(MODE_KINDS)
        )
        result = parse_events_jsonl(content)

        assert result.warnings == []
        assert [e.mode for e in result.events] == list(MODE_KINDS.values())
        assert result.events[0].started_at == datetime(
            2025, 7, 30, 18, 26, 40, tzinfo=timezone.utc
        )

    def test_parses_valid_lines(self):
        content = (
            '{"kind":"SAIL","utc":1753900000}\n'
            '{"kind":"MOTOR","utc":1753903600}\n'
            '{"kind":"OTHER","utc":1753907200}\n'
        )
        result = parse_events_jsonl(content)

        assert len(result.events) == 3
        assert result.warnings == []
        assert result.events[0].mode == "SAIL"
        assert result.events[0].started_at == datetime(
            2025, 7, 30, 18, 26, 40, tzinfo=timezone.utc
        )
        assert result.events[1].mode == "MOTOR"
        assert result.events[2].mode == "OTHER"

    def test_empty_content(self):
        result = parse_events_jsonl("")
        assert result.events == []
        assert result.warnings == []

    def test_blank_lines_ignored(self):
        result = parse_events_jsonl('\n{"kind":"SAIL","utc":100}\n\n')
        assert len(result.events) == 1
        assert result.warnings == []

    def test_utc_zero_skipped_with_warning(self):
        # utc=0 is the device's "time unknown" sentinel — a mode with no
        # timestamp cannot segment anything.
        content = '{"kind":"SAIL","utc":0}\n{"kind":"MOTOR","utc":1753900000}\n'
        result = parse_events_jsonl(content)

        assert len(result.events) == 1
        assert result.events[0].mode == "MOTOR"
        assert len(result.warnings) == 1
        assert "utc=0" in result.warnings[0]

    def test_unknown_kind_skipped_with_warning(self):
        content = '{"kind":"VOICE","utc":1753900000}\n'
        result = parse_events_jsonl(content)

        assert result.events == []
        assert len(result.warnings) == 1
        assert "VOICE" in result.warnings[0]

    def test_invalid_json_line_skipped_with_warning(self):
        content = 'not json at all\n{"kind":"SAIL","utc":1753900000}\n'
        result = parse_events_jsonl(content)

        assert len(result.events) == 1
        assert len(result.warnings) == 1
        assert "line 1" in result.warnings[0]

    def test_non_object_and_bad_utc_types(self):
        content = '[1,2]\n{"kind":"SAIL","utc":"soon"}\n{"kind":"SAIL"}\n'
        result = parse_events_jsonl(content)

        assert result.events == []
        assert len(result.warnings) == 3

    def test_parse_does_not_debounce(self):
        # The sidecar is the raw record: every press parses, and the debounce
        # is a separate decision on top of it (issue #161).
        content = '{"kind":"SAIL","utc":1753900000}\n{"kind":"MOTOR","utc":1753900002}\n'
        result = parse_events_jsonl(content)

        assert [e.mode for e in result.events] == ["SAIL", "MOTOR"]


T0 = datetime(2025, 7, 30, 18, 26, 40, tzinfo=timezone.utc)


def at(offset_seconds: float, mode: str) -> ParsedEvent:
    return ParsedEvent(mode=mode, started_at=T0 + timedelta(seconds=offset_seconds))


class TestCollapseModeBursts:
    """Mis-press debounce (issue #161): a correction within the window is not
    a mode change of its own."""

    def test_keeps_the_last_mode_at_the_first_timestamp(self):
        # The case from the issue: tapped SAIL, meant MOTOR, tapped again.
        kept, warnings = collapse_mode_bursts([at(0, "SAIL"), at(3, "MOTOR")])

        assert [(e.mode, e.started_at) for e in kept] == [("MOTOR", T0)]
        assert len(warnings) == 1
        assert "SAIL -> MOTOR" in warnings[0]
        assert "kept MOTOR" in warnings[0]

    def test_leaves_deliberate_changes_alone(self):
        events = [at(0, "SAIL"), at(600, "MOTOR"), at(1200, "ANCHOR")]
        kept, warnings = collapse_mode_bursts(events)

        assert [e.mode for e in kept] == ["SAIL", "MOTOR", "ANCHOR"]
        assert [e.started_at for e in kept] == [e.started_at for e in events]
        assert warnings == []

    def test_three_presses_in_one_burst_collapse_together(self):
        kept, _ = collapse_mode_bursts(
            [at(0, "SAIL"), at(2, "MOTORSAIL"), at(5, "MOTOR")]
        )

        assert [(e.mode, e.started_at) for e in kept] == [("MOTOR", T0)]

    def test_window_is_measured_from_the_first_press_not_the_previous_one(self):
        # A drip of presses one window apart must not chain into one endless
        # burst: the second press anchors a burst of its own.
        w = MODE_DEBOUNCE_SECONDS
        kept, _ = collapse_mode_bursts(
            [at(0, "SAIL"), at(w - 1, "MOTOR"), at(2 * w - 3, "ANCHOR")]
        )

        assert [(e.mode, e.started_at) for e in kept] == [
            ("MOTOR", T0),
            ("ANCHOR", T0 + timedelta(seconds=2 * w - 3)),
        ]

    def test_exactly_one_window_apart_is_a_real_change(self):
        kept, warnings = collapse_mode_bursts(
            [at(0, "SAIL"), at(MODE_DEBOUNCE_SECONDS, "MOTOR")]
        )

        assert [e.mode for e in kept] == ["SAIL", "MOTOR"]
        assert warnings == []

    def test_same_mode_run_collapses(self):
        # Two manual leg cuts (issue #142) inside the window are
        # indistinguishable from a mis-press pair.
        kept, warnings = collapse_mode_bursts([at(0, "SAIL"), at(4, "SAIL")])

        assert [(e.mode, e.started_at) for e in kept] == [("SAIL", T0)]
        assert len(warnings) == 1

    def test_out_of_order_input_is_sorted_first(self):
        kept, _ = collapse_mode_bursts([at(3, "MOTOR"), at(0, "SAIL")])

        assert [(e.mode, e.started_at) for e in kept] == [("MOTOR", T0)]

    def test_empty_and_single(self):
        assert collapse_mode_bursts([]) == ([], [])

        kept, warnings = collapse_mode_bursts([at(0, "SAIL")])
        assert [(e.mode, e.started_at) for e in kept] == [("SAIL", T0)]
        assert warnings == []

    def test_window_is_a_parameter(self):
        events = [at(0, "SAIL"), at(60, "MOTOR")]

        assert len(collapse_mode_bursts(events, window_seconds=90)[0]) == 1
        assert len(collapse_mode_bursts(events, window_seconds=30)[0]) == 2

    def test_input_events_are_not_mutated(self):
        events = [at(0, "SAIL"), at(3, "MOTOR")]
        collapse_mode_bursts(events)

        assert [(e.mode, e.started_at) for e in events] == [
            ("SAIL", T0),
            ("MOTOR", T0 + timedelta(seconds=3)),
        ]


class TestMalformedSidecars:
    """Every one of these has to be a warning rather than a raise.

    A sidecar is read off an SD card that may have been pulled mid-write, and
    it travels attached to the track CSV — losing a whole session's track to
    one bad line is a far worse trade than importing the good lines and
    saying what was dropped (`upload_csv` in viewsets.py).
    """

    def test_truncated_final_line(self):
        # The device appends to the sidecar while recording, so a track that
        # was never synced cleanly can end mid-line.
        result = parse_events_jsonl(
            '{"kind":"SAIL","utc":1753900000}\n{"kind":"MOTOR","ut'
        )

        assert [e.mode for e in result.events] == ["SAIL"]
        assert len(result.warnings) == 1
        assert "Events line 2" in result.warnings[0]
        assert "invalid JSON" in result.warnings[0]

    def test_boolean_utc_is_not_a_timestamp(self):
        # isinstance(True, int) is True in Python, so an unguarded int check
        # lets this through and lands a mode at 1970-01-01T00:00:01Z.
        result = parse_events_jsonl('{"kind":"SAIL","utc":true}\n')

        assert result.events == []
        assert "invalid utc True" in result.warnings[0]

    def test_float_utc_skipped(self):
        result = parse_events_jsonl('{"kind":"SAIL","utc":1753900000.5}\n')

        assert result.events == []
        assert "invalid utc" in result.warnings[0]

    def test_utc_beyond_the_wire_range_skipped_rather_than_raised(self):
        # datetime.fromtimestamp raises OverflowError long before this value.
        # Unbounded, that exception escapes the parser and 500s the upload.
        result = parse_events_jsonl('{"kind":"SAIL","utc":100000000000000000000}\n')

        assert result.events == []
        assert "out of range" in result.warnings[0]

    def test_largest_wire_utc_still_parses(self):
        # The wire field is a uint32, so its ceiling is a valid timestamp and
        # must not be rejected along with the corrupt ones.
        result = parse_events_jsonl(f'{{"kind":"SAIL","utc":{MAX_WIRE_UTC}}}\n')

        assert result.warnings == []
        assert result.events[0].started_at == datetime(
            2106, 2, 7, 6, 28, 15, tzinfo=timezone.utc
        )

    def test_missing_keys(self):
        result = parse_events_jsonl('{"utc":1753900000}\n{"kind":"SAIL"}\n')

        assert result.events == []
        assert "unknown kind None" in result.warnings[0]
        assert "invalid utc None" in result.warnings[1]

    def test_crlf_line_endings(self):
        result = parse_events_jsonl(
            '{"kind":"SAIL","utc":1753900000}\r\n'
            '{"kind":"MOTOR","utc":1753903600}\r\n'
        )

        assert [e.mode for e in result.events] == ["SAIL", "MOTOR"]
        assert result.warnings == []

    def test_duplicate_lines_all_parse(self):
        # The sidecar carries no seq, so the parser cannot dedupe and does not
        # try: the debounce collapses same-timestamp presses, and Mode's
        # (vessel, started_at) uniqueness is the backstop on import.
        result = parse_events_jsonl('{"kind":"SAIL","utc":1753900000}\n' * 3)

        assert len(result.events) == 3
        assert result.warnings == []


class TestEventsOutsideWindow:
    """Which sidecar timestamps are too far from the track to be believed."""

    START = datetime(2026, 1, 4, 13, 0, tzinfo=timezone.utc)
    END = datetime(2026, 1, 4, 15, 0, tzinfo=timezone.utc)

    def _at(self, *events):
        return [ParsedEvent(mode="SAIL", started_at=e) for e in events]

    def test_inside_the_window_is_not_reported(self):
        inside = self._at(self.START, self.START + timedelta(hours=1), self.END)

        assert events_outside_window(inside, self.START, self.END) == []

    def test_a_fix_gap_at_either_end_is_tolerated(self):
        # A mode set before the first fix or after the last one is ordinary:
        # the track spans fixes, the sidecar spans the whole recording.
        edges = self._at(
            self.START - timedelta(minutes=59), self.END + timedelta(minutes=59)
        )

        assert events_outside_window(edges, self.START, self.END) == []

    def test_beyond_the_tolerance_is_reported(self):
        late = self.END + timedelta(hours=2)

        outside = events_outside_window(
            self._at(self.START, late), self.START, self.END
        )

        assert [e.started_at for e in outside] == [late]

    def test_epoch_garbage_is_reported(self):
        # What a mangled line produces: years out, not minutes.
        epoch = datetime(1970, 1, 1, 0, 0, 1, tzinfo=timezone.utc)

        outside = events_outside_window(self._at(epoch), self.START, self.END)

        assert [e.started_at for e in outside] == [epoch]

    def test_empty(self):
        assert events_outside_window([], self.START, self.END) == []

    def test_tolerance_is_a_parameter(self):
        just_after = self.END + timedelta(seconds=1)

        assert events_outside_window(
            self._at(just_after), self.START, self.END, tolerance_seconds=0
        ) == self._at(just_after)
