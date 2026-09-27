export function formatNumber(
  v: number | null | undefined,
  digits = 1,
  suffix = "",
): string {
  if (v == null || Number.isNaN(v)) return "—";
  return `${v.toFixed(digits)}${suffix}`;
}

export function formatFileSize(bytes: number | null | undefined): string {
  if (bytes == null || Number.isNaN(bytes)) return "—";
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

export function formatTimestampMs(ms: number): string {
  return new Date(ms).toLocaleString(undefined, {
    month: "short",
    day: "numeric",
    hour: "2-digit",
    minute: "2-digit",
  });
}

export function formatDurationMs(ms: number): string {
  const seconds = Math.floor(ms / 1000);
  const minutes = Math.floor(seconds / 60);
  const hours = Math.floor(minutes / 60);
  const days = Math.floor(hours / 24);

  if (days > 0) {
    return `${days}d ${hours % 24}h`;
  } else if (hours > 0) {
    return `${hours}h ${minutes % 60}m`;
  } else {
    return `${minutes}m ${seconds % 60}s`;
  }
}

export function formatDateTime(dateString: string | null | undefined): string {
  if (!dateString) return "No date";
  try {
    return new Date(dateString).toLocaleString();
  } catch {
    return "Invalid date";
  }
}

export function formatDuration(
  start: string | null | undefined,
  end: string | null | undefined,
): string {
  if (!start) return "Unknown duration";
  if (!end) return "Ongoing";
  try {
    const startDate = new Date(start);
    const endDate = new Date(end);
    const diffMs = endDate.getTime() - startDate.getTime();
    const diffDays = Math.floor(diffMs / (1000 * 60 * 60 * 24));
    const diffHours = Math.floor((diffMs % (1000 * 60 * 60 * 24)) / (1000 * 60 * 60));

    if (diffDays === 0) {
      const hours = Math.floor(diffMs / (1000 * 60 * 60));
      const mins = Math.floor((diffMs % (1000 * 60 * 60)) / (1000 * 60));
      return `${hours}h ${mins}m`;
    } else {
      return `${diffDays}d ${diffHours}h`;
    }
  } catch {
    return "Invalid duration";
  }
}

export function formatDurationSeconds(seconds: number | null | undefined): string {
  if (seconds == null) return "Unknown";
  if (seconds < 60) return "<1 min";
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const mins = Math.floor((seconds % 3600) / 60);
  if (days > 0) return `${days}d ${hours} hr`;
  if (hours > 0) return `${hours} hr ${mins} min`;
  return `${mins} min`;
}

export function formatDateRange(
  start: string | null | undefined,
  end: string | null | undefined,
): string {
  if (!start) return "No date";
  try {
    const startDate = new Date(start);
    const full: Intl.DateTimeFormatOptions = { day: "numeric", month: "short", year: "numeric" };
    if (!end) return startDate.toLocaleDateString("en-GB", full);

    const endDate = new Date(end);
    if (startDate.toDateString() === endDate.toDateString()) {
      return startDate.toLocaleDateString("en-GB", full);
    }

    const sameYear = startDate.getFullYear() === endDate.getFullYear();
    const sameMonth = sameYear && startDate.getMonth() === endDate.getMonth();
    if (sameMonth) {
      // 12–14 Jul 2025
      return `${startDate.getDate()}–${endDate.toLocaleDateString("en-GB", full)}`;
    }
    if (sameYear) {
      // 12 Jul – 14 Aug 2025
      return `${startDate.toLocaleDateString("en-GB", { day: "numeric", month: "short" })} – ${endDate.toLocaleDateString("en-GB", full)}`;
    }
    // 28 Dec 2024 – 2 Jan 2025
    return `${startDate.toLocaleDateString("en-GB", full)} – ${endDate.toLocaleDateString("en-GB", full)}`;
  } catch {
    return "Invalid date";
  }
}

export function formatAbsoluteDate(dateString: string | null | undefined): string {
  if (!dateString) return "No date";
  try {
    return new Date(dateString).toLocaleDateString("en-GB", {
      weekday: "short",
      day: "numeric",
      month: "long",
      year: "numeric",
    });
  } catch {
    return "Invalid date";
  }
}

// ── Timeline spans (a leg or a mode segment) ─────────────────────────────────

/** Wall-clock time of day, "14:05". */
export function formatClockTime(dateString: string): string {
  const d = new Date(dateString);
  return `${d.getHours().toString().padStart(2, "0")}:${d.getMinutes().toString().padStart(2, "0")}`;
}

/** How long a span ran; null means it is still running. */
export function formatSpanDuration(ms: number | null): string {
  if (ms === null) return "Ongoing";
  if (ms < 60000) return "<1 min";
  const hours = Math.floor(ms / (1000 * 60 * 60));
  const mins = Math.floor((ms % (1000 * 60 * 60)) / (1000 * 60));
  if (hours === 0) return `${mins} min`;
  return `${hours} hr ${mins} min`;
}

/** A span's start → end, dropping the date when both ends fall on one day. */
export function formatSpanRange(start: string, end: string | null): string {
  const s = new Date(start);
  const e = end ? new Date(end) : null;
  const timeOpts: Intl.DateTimeFormatOptions = { hour: "numeric", minute: "2-digit" };
  const fullOpts: Intl.DateTimeFormatOptions = { day: "numeric", month: "short", hour: "numeric", minute: "2-digit" };

  if (!e) return s.toLocaleTimeString(undefined, timeOpts);
  if (s.toDateString() === e.toDateString()) {
    return `${s.toLocaleTimeString(undefined, timeOpts)} → ${e.toLocaleTimeString(undefined, timeOpts)}`;
  }
  return `${s.toLocaleString(undefined, fullOpts)} → ${e.toLocaleString(undefined, fullOpts)}`;
}

/** Distance in nautical miles, precise while it is small enough to matter. */
export function formatDistanceNm(nm: number): string {
  return nm < 10 ? `${nm.toFixed(1)} nm` : `${Math.round(nm)} nm`;
}

/** Speed in knots. */
export function formatSpeedKnots(knots: number): string {
  return `${knots.toFixed(1)} kts`;
}

/**
 * The 16-point compass name for a bearing. Wind directions are reported in
 * degrees but read as letters, and 350° is "N", not a rounding of "NNW" — so
 * the lookup wraps rather than clamping.
 */
export function compassPoint(deg: number | null | undefined): string | null {
  if (deg == null || Number.isNaN(deg)) return null;
  const points = [
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
  ];
  const index = Math.round((((deg % 360) + 360) % 360) / 22.5) % 16;
  return points[index];
}
