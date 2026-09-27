"""
Events sidecar (JSONL) parser.

Parses the watch-event sidecar the device records alongside each track CSV
(issue #139): one JSON object per line, e.g.

    {"kind":"SAIL","utc":1753900000}

Keep the line shape in step with the device writer
(device/include/recording/event_jsonl.h).

Parsing and debouncing are two steps on purpose: `parse_events_jsonl` reports
what the watch actually recorded, `collapse_mode_bursts` decides which of those
presses meant something. The sidecar stays the raw record — nothing is
debounced on the wire, on the watch's queue or in the file, so a re-upload
re-derives the segments under whatever window is current (issue #161).
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone

# Sidecar kind -> Mode.MODE_CHOICES value. Identity today, but the sidecar
# vocabulary is the wire protocol's, not the Mode model's — map explicitly.
# Every mode the watch picker can set is here (issue #146); the names come from
# moana::sailingModeName in shared/espnow_protocol.h. Non-mode kinds (VOICE)
# are deliberately absent — they parse to a warning, not a Mode row.
MODE_KINDS = {
    "SAIL": "SAIL",
    "MOTORSAIL": "MOTORSAIL",
    "MOTOR": "MOTOR",
    "ROW": "ROW",
    "UNPOWERED": "UNPOWERED",
    "ANCHOR": "ANCHOR",
    "ALONGSIDE": "ALONGSIDE",
    "BEACHED": "BEACHED",
    "TRAILERED": "TRAILERED",
    "OTHER": "OTHER",
}

# The sidecar's utc comes from `uint32_t utc_seconds` on the wire
# (shared/espnow_protocol.h), so the device cannot write a larger value and a
# larger one is a corrupt line. It also keeps every timestamp inside what
# datetime.fromtimestamp accepts: unbounded, a big enough integer raises
# OverflowError out of the parser and turns a bad sidecar line into a failed
# track upload.
MAX_WIRE_UTC = 0xFFFFFFFF  # 2106-02-07T06:28:15Z

# Mis-press debounce window (issue #161). Presses inside this much of the one
# that opened a burst are corrections — tapping SAIL when MOTOR was meant, or
# mis-hitting a row in the picker — so the burst becomes a single mode change.
# 20 s because a correction happens within a few seconds while the shortest
# mode a human actually means lasts minutes.
#
# The watch's stats engine debounces its legs on the same rule and the same
# duration (`kMinLegMs` in watch/src/stats_engine.h): a mis-press must not draw
# a five-second leg on the wrist and a five-second segment on the web map.
# Change one and change the other.
MODE_DEBOUNCE_SECONDS = 20

# How far outside a track's own time range a sidecar event may fall before it
# looks like a corrupt timestamp rather than an ordinary one.
#
# A track starts and ends at its first and last GPS *fix*, while the sidecar
# covers the whole recording: a mode set while the fix is lost — under a
# bridge, antenna knocked, the boat's GPS still warming up — legitimately
# lands outside that window, and so does one set before recording started
# (issue #145). An hour swallows all of those and still catches the values a
# mangled line produces, which are years out, not minutes.
SIDECAR_WINDOW_TOLERANCE_SECONDS = 3600


@dataclass
class ParsedEvent:
    """A single validated mode event ready for Mode creation."""

    mode: str  # a Mode.MODE_CHOICES value
    started_at: datetime


@dataclass
class EventsParseResult:
    """Output of parsing an events sidecar."""

    events: list[ParsedEvent] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)


def parse_events_jsonl(content: str) -> EventsParseResult:
    """Parse an events sidecar into validated mode events.

    Never raises for bad rows — a mangled line becomes a warning, mirroring
    parse_gps_csv's row-level error handling. Notable policies:

    - utc == 0 is the device's "time unknown" sentinel (the watch had no GPS
      time reference when the button was pressed). A mode with no timestamp
      cannot segment anything, so it is skipped with a warning.
    - Unknown kinds (future watch event types) are skipped with a warning.
    - utc must fit the wire field it came from (`uint32_t utc_seconds` in
      shared/espnow_protocol.h). Anything larger is a corrupt line, and is
      also past what datetime.fromtimestamp accepts — left unbounded it
      raises out of here and 500s the whole track upload.
    - A JSON boolean is not a timestamp, though `isinstance(True, int)` is
      True in Python. Rejected explicitly, or `utc: true` lands a mode at
      1970-01-01T00:00:01Z.
    """
    result = EventsParseResult()

    for line_num, line in enumerate(content.splitlines(), start=1):
        line = line.strip()
        if not line:
            continue

        try:
            obj = json.loads(line)
        except json.JSONDecodeError as e:
            result.warnings.append(f"Events line {line_num}: invalid JSON ({e})")
            continue
        if not isinstance(obj, dict):
            result.warnings.append(f"Events line {line_num}: not a JSON object")
            continue

        kind = obj.get("kind")
        utc = obj.get("utc")

        if kind not in MODE_KINDS:
            result.warnings.append(
                f"Events line {line_num}: unknown kind {kind!r} — skipped"
            )
            continue

        if not isinstance(utc, int) or isinstance(utc, bool) or utc < 0:
            result.warnings.append(
                f"Events line {line_num}: invalid utc {utc!r} — skipped"
            )
            continue
        if utc > MAX_WIRE_UTC:
            result.warnings.append(
                f"Events line {line_num}: utc {utc} out of range "
                f"(the wire carries a uint32) — skipped"
            )
            continue
        if utc == 0:
            result.warnings.append(
                f"Events line {line_num}: utc=0 (watch had no time reference) "
                f"— {kind} event skipped"
            )
            continue

        result.events.append(
            ParsedEvent(
                mode=MODE_KINDS[kind],
                started_at=datetime.fromtimestamp(utc, tz=timezone.utc),
            )
        )

    return result


def collapse_mode_bursts(
    events: list[ParsedEvent],
    window_seconds: float = MODE_DEBOUNCE_SECONDS,
) -> tuple[list[ParsedEvent], list[str]]:
    """Collapse mis-press bursts into one mode change each (issue #161).

    A burst is a maximal run of events all falling within `window_seconds` of
    the run's FIRST event — measured from the first press rather than from each
    predecessor, so a slow drip of presses can't chain into one endless burst.
    Each burst collapses to **the first event's timestamp carrying the last
    event's mode**: the intent to change began at the first press, and what was
    meant is what was pressed last.

    Same-mode runs collapse too. A same-mode event is the watch's manual leg cut
    (issue #142), and one landing seconds after a mode change is noise like any
    other; two deliberate cuts inside the window are indistinguishable from it.

    Returns the kept events (in time order) and a warning per collapsed burst.
    """
    ordered = sorted(events, key=lambda e: e.started_at)
    window = timedelta(seconds=window_seconds)

    kept: list[ParsedEvent] = []
    warnings: list[str] = []

    i = 0
    while i < len(ordered):
        anchor = ordered[i]
        end = i + 1
        while end < len(ordered) and ordered[end].started_at - anchor.started_at < window:
            end += 1

        burst = ordered[i:end]
        kept.append(ParsedEvent(mode=burst[-1].mode, started_at=anchor.started_at))
        if len(burst) > 1:
            warnings.append(
                f"Debounced {len(burst)} mode events within {window_seconds:g}s of "
                f"{anchor.started_at.isoformat()} "
                f"({' -> '.join(e.mode for e in burst)}) — kept {burst[-1].mode}"
            )
        i = end

    return kept, warnings


def events_outside_window(
    events: list[ParsedEvent],
    start: datetime,
    end: datetime,
    tolerance_seconds: float = SIDECAR_WINDOW_TOLERANCE_SECONDS,
) -> list[ParsedEvent]:
    """The events too far outside [start, end] to be explained by a fix gap.

    Reported, never dropped. A Mode row is anchored to the vessel and a
    timestamp, so once written nothing ties it back to the upload it arrived
    with — a wrong one is easier to find while the track's range is still in
    hand than afterwards.
    """
    slack = timedelta(seconds=tolerance_seconds)
    return [e for e in events if not (start - slack <= e.started_at <= end + slack)]
