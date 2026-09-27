import { describe, it, expect, beforeEach, vi } from "vitest";
import { useJourneySelection } from "../useJourneySelection";

// A stand-in router whose replace() writes straight back into the route, so the
// composable is exercised through the same round trip the app makes.
vi.mock("vue-router", async () => {
  const { reactive } = await import("vue");
  const route = reactive({ query: {} as Record<string, string> });
  const router = {
    replace: ({ query }: { query: Record<string, string> }) => {
      route.query = { ...query };
    },
  };
  return { useRoute: () => route, useRouter: () => router, __route: route };
});

const { __route: route } = (await import("vue-router")) as unknown as {
  __route: { query: Record<string, string> };
};

describe("useJourneySelection", () => {
  beforeEach(() => {
    route.query = {};
  });

  it("defaults to the whole journey", () => {
    const { scope } = useJourneySelection();
    expect(scope.value).toEqual({ kind: "journey" });
  });

  it("reads a leg and a segment out of the query", () => {
    const { scope } = useJourneySelection();

    route.query = { leg: "2" };
    expect(scope.value).toEqual({ kind: "leg", id: 2 });

    route.query = { seg: "4" };
    expect(scope.value).toEqual({ kind: "segments", ids: [4] });
  });

  it("reads a run of segments out of one parameter", () => {
    const { scope } = useJourneySelection();

    route.query = { seg: "4,5,6" };
    expect(scope.value).toEqual({ kind: "segments", ids: [4, 5, 6] });
  });

  it("drops junk and duplicates from a run rather than failing the whole scope", () => {
    const { scope } = useJourneySelection();

    route.query = { seg: "4, abc ,5,4" };
    expect(scope.value).toEqual({ kind: "segments", ids: [4, 5] });
  });

  it("lets a segment win over a leg left in the query", () => {
    const { scope } = useJourneySelection();
    route.query = { leg: "2", seg: "4" };
    expect(scope.value).toEqual({ kind: "segments", ids: [4] });
  });

  it("ignores ids that are not positive integers", () => {
    const { scope } = useJourneySelection();

    for (const seg of ["0", "-1", "abc", "1.5", ""]) {
      route.query = { seg };
      expect(scope.value).toEqual({ kind: "journey" });
    }
  });

  it("writes only the parameter that names the scope", () => {
    const { scope, select } = useJourneySelection();

    select({ kind: "leg", id: 2 });
    expect(route.query).toEqual({ leg: "2" });

    select({ kind: "segments", ids: [4] });
    expect(route.query).toEqual({ seg: "4" });
    expect(scope.value).toEqual({ kind: "segments", ids: [4] });
  });

  it("writes a run as one comma-separated parameter", () => {
    const { scope, select } = useJourneySelection();

    select({ kind: "segments", ids: [4, 5, 6] });
    expect(route.query).toEqual({ seg: "4,5,6" });
    expect(scope.value).toEqual({ kind: "segments", ids: [4, 5, 6] });
  });

  it("treats an emptied run as no selection at all", () => {
    const { scope, select } = useJourneySelection();

    select({ kind: "segments", ids: [4] });
    select({ kind: "segments", ids: [] });

    expect(route.query).toEqual({});
    expect(scope.value).toEqual({ kind: "journey" });
  });

  it("selectOnly sets the scope but never toggles it off", () => {
    const { scope, selectOnly } = useJourneySelection();

    selectOnly({ kind: "segments", ids: [4] });
    selectOnly({ kind: "segments", ids: [4] });

    expect(route.query).toEqual({ seg: "4" });
    expect(scope.value).toEqual({ kind: "segments", ids: [4] });
  });

  it("returns to the journey when the current selection is selected again", () => {
    const { scope, select } = useJourneySelection();

    select({ kind: "leg", id: 2 });
    select({ kind: "leg", id: 2 });

    expect(route.query).toEqual({});
    expect(scope.value).toEqual({ kind: "journey" });
  });

  it("keeps other query parameters", () => {
    route.query = { tab: "journey", v: "2" };
    const { select, clear } = useJourneySelection();

    select({ kind: "segments", ids: [7] });
    expect(route.query).toEqual({ tab: "journey", v: "2", seg: "7" });

    clear();
    expect(route.query).toEqual({ tab: "journey", v: "2" });
  });
});
