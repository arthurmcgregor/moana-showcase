"""Bin a NIWA 10-minute wind CSV into the counts the wind rose draws.

NIWA's licence forbids republishing the observations themselves, but allows
derived outputs with attribution — so the repo ships these counts, not the CSV.

    python3 tools/wind/aggregate.py wind.csv > frontend/public/wind-rose.json

Input columns: AGENT_NO, OBS_DATE (ISO 8601), MEAN_DIR10 (degrees), MEAN_SPD10 (m/s).
Output: {"source": ..., "months": {"01": [[count per speed band] per direction], ...}}
with 16 directions from N and the speed bands of frontend/src/views/WindRoseView.vue.
"""

import csv
import json
import sys

# 5, 10, 15, 20, 25 kn in m/s
BAND_EDGES_MS = [2.572, 5.144, 7.716, 10.288, 12.860]


def speed_band(ms):
    return next((i for i, edge in enumerate(BAND_EDGES_MS) if ms < edge), len(BAND_EDGES_MS))


def main(path):
    months = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            try:
                direction, speed = float(row["MEAN_DIR10"]), float(row["MEAN_SPD10"])
            except ValueError:
                continue
            if speed < 0:
                continue
            bins = months.setdefault(row["OBS_DATE"][5:7], [[0] * 6 for _ in range(16)])
            bins[int(direction % 360 / 22.5 + 0.5) % 16][speed_band(speed)] += 1
    json.dump(
        {
            "source": "NIWA National Climate Database, Diamond Harbour station (agent 40985)",
            "months": dict(sorted(months.items())),
        },
        sys.stdout,
        separators=(",", ":"),
    )


if __name__ == "__main__":
    main(sys.argv[1])
