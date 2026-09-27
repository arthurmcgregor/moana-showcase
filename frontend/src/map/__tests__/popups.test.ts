import { describe, it, expect } from "vitest";
import { renderAnnotationPopup, renderModePopup } from "../popups";

describe("popup rendering", () => {
  it("escapes HTML-significant characters in notes", () => {
    const html = renderAnnotationPopup({
      type: "TACK",
      timestamp: "2024-01-01T00:00:00Z",
      notes: `<script>alert("xss")</script> & 'injected'`,
    });
    expect(html).not.toContain("<script>");
    expect(html).toContain("&lt;script&gt;");
    expect(html).toContain("&amp;");
    expect(html).toContain("&quot;");
    expect(html).toContain("&#39;");
  });

  it("escapes HTML in the title field", () => {
    const html = renderAnnotationPopup({
      type: "<img src=x onerror=alert(1)>",
      timestamp: "2024-01-01T00:00:00Z",
      notes: "",
    });
    expect(html).not.toContain("<img");
    expect(html).toContain("&lt;img");
  });

  it("renders a pending annotation with the (Pending) suffix", () => {
    const html = renderAnnotationPopup({
      type: "TACK",
      timestamp: "2024-01-01T00:00:00Z",
      notes: "",
      isPending: true,
    });
    expect(html).toContain("(Pending)");
  });

  it("omits the notes paragraph when notes are empty", () => {
    const html = renderAnnotationPopup({
      type: "TACK",
      timestamp: "2024-01-01T00:00:00Z",
      notes: "",
    });
    expect(html).not.toContain("<strong>Notes:");
  });

  it("renders mode popups with the Started label", () => {
    const html = renderModePopup({
      mode: "SAIL",
      started_at: "2024-01-01T00:00:00Z",
      notes: "hello",
    });
    expect(html).toContain("<strong>Started:");
    expect(html).toContain("SAIL");
    expect(html).toContain("hello");
  });

  it("escapes notes on mode popups as well", () => {
    const html = renderModePopup({
      mode: "SAIL",
      started_at: "2024-01-01T00:00:00Z",
      notes: `<b>bold</b>`,
    });
    expect(html).not.toContain("<b>bold</b>");
    expect(html).toContain("&lt;b&gt;bold&lt;/b&gt;");
  });
});
