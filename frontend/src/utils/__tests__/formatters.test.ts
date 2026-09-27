import { describe, expect, it } from "vitest";
import { compassPoint, formatDateRange } from "@/utils/formatters";

describe("formatDateRange", () => {
  it("returns a placeholder when there is no start", () => {
    expect(formatDateRange(null, null)).toBe("No date");
  });

  // Local (no-Z) timestamps keep these assertions timezone-independent — the
  // formatter renders in local time, so a UTC instant could land on a different
  // calendar day depending on where the test runs.
  it("shows a single date when there is no end", () => {
    expect(formatDateRange("2025-07-12T09:00:00", null)).toBe("12 Jul 2025");
  });

  it("shows a single date when start and end fall on the same day", () => {
    expect(formatDateRange("2025-07-12T06:00:00", "2025-07-12T18:00:00")).toBe("12 Jul 2025");
  });

  it("collapses the month and year for a same-month range", () => {
    expect(formatDateRange("2025-07-12T09:00:00", "2025-07-14T09:00:00")).toBe("12–14 Jul 2025");
  });

  it("keeps both months for a same-year range", () => {
    expect(formatDateRange("2025-07-12T09:00:00", "2025-08-14T09:00:00")).toBe(
      "12 Jul – 14 Aug 2025",
    );
  });

  it("shows full dates for a range spanning years", () => {
    expect(formatDateRange("2024-12-28T09:00:00", "2025-01-02T09:00:00")).toBe(
      "28 Dec 2024 – 2 Jan 2025",
    );
  });
});

describe("compassPoint", () => {
  it("names the cardinals", () => {
    expect(compassPoint(0)).toBe("N");
    expect(compassPoint(90)).toBe("E");
    expect(compassPoint(180)).toBe("S");
    expect(compassPoint(270)).toBe("W");
  });

  it("wraps past north rather than rounding down to NNW", () => {
    expect(compassPoint(350)).toBe("N");
    expect(compassPoint(360)).toBe("N");
    expect(compassPoint(-10)).toBe("N");
  });

  it("rounds to the nearest of sixteen points", () => {
    expect(compassPoint(22.5)).toBe("NNE");
    expect(compassPoint(34)).toBe("NE");
    expect(compassPoint(112.5)).toBe("ESE");
  });

  it("has no name for a missing bearing", () => {
    expect(compassPoint(null)).toBeNull();
    expect(compassPoint(undefined)).toBeNull();
    expect(compassPoint(NaN)).toBeNull();
  });
});
