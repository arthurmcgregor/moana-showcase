import { getModeColor, getAnnotationColor } from "@/constants/journeyTypes";
import type { AnnotationType } from "@/api/web";
import { formatDateTime } from "@/utils/formatters";

type Props = Record<string, unknown>;

function esc(value: unknown): string {
  return String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function renderPopup(opts: {
  title: string;
  color: string;
  isPending: boolean;
  timeLabel: string;
  timeValue: unknown;
  notes: unknown;
}): string {
  const notes = opts.notes ? String(opts.notes) : "";
  return `
    <div style="font-family: system-ui, -apple-system, sans-serif;">
      <h3 style="margin: 0 0 8px 0; color: ${opts.color}; font-size: 16px;">
        ${esc(opts.title)}${opts.isPending ? " (Pending)" : ""}
      </h3>
      <p style="margin: 4px 0; font-size: 14px; color: #666;">
        <strong>${esc(opts.timeLabel)}:</strong> ${esc(formatDateTime(opts.timeValue as string | null | undefined))}
      </p>
      ${notes ? `<p style="margin: 4px 0; font-size: 14px; color: #666;"><strong>Notes:</strong> ${esc(notes)}</p>` : ""}
    </div>
  `;
}

export function renderAnnotationPopup(p: Props): string {
  const type = (p.type as string) || "Unknown";
  return renderPopup({
    title: type,
    color: getAnnotationColor((type || "NOTE") as AnnotationType),
    isPending: Boolean(p.isPending),
    timeLabel: "Time",
    timeValue: p.timestamp,
    notes: p.notes,
  });
}

export function renderModePopup(p: Props): string {
  const mode = (p.mode as string) || "Unknown";
  return renderPopup({
    title: mode,
    color: getModeColor(mode || "SAIL"),
    isPending: Boolean(p.isPending),
    timeLabel: "Started",
    timeValue: p.started_at,
    notes: p.notes,
  });
}
