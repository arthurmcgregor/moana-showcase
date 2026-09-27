import type { AboardRecord } from "@/api/web";

export type Bounds = { started_at: string; ended_at: string | null };

export type Op =
  | { kind: "create"; person: number; started_at: string; ended_at: string | null }
  | { kind: "update"; id: number; started_at: string; ended_at: string | null }
  | { kind: "delete"; id: number };

const PLUS_INF = Number.POSITIVE_INFINITY;

function ms(t: string | null): number {
  return t === null ? PLUS_INF : Date.parse(t);
}

function covers(record: AboardRecord, target: Bounds): boolean {
  return ms(record.started_at) <= ms(target.started_at) && ms(record.ended_at) >= ms(target.ended_at);
}

export function planAdd(
  existing: AboardRecord[],
  person: number,
  target: Bounds,
): Op[] {
  const own = existing.filter((r) => r.person === person);
  if (own.some((r) => covers(r, target))) return [];

  const adjacentBefore = own.find((r) => r.ended_at === target.started_at);
  const adjacentAfter = own.find((r) => r.started_at === target.ended_at);

  if (adjacentBefore && adjacentAfter) {
    return [
      {
        kind: "update",
        id: adjacentBefore.id,
        started_at: adjacentBefore.started_at,
        ended_at: adjacentAfter.ended_at,
      },
      { kind: "delete", id: adjacentAfter.id },
    ];
  }

  if (adjacentBefore) {
    return [
      {
        kind: "update",
        id: adjacentBefore.id,
        started_at: adjacentBefore.started_at,
        ended_at: target.ended_at,
      },
    ];
  }

  if (adjacentAfter) {
    return [
      {
        kind: "update",
        id: adjacentAfter.id,
        started_at: target.started_at,
        ended_at: adjacentAfter.ended_at,
      },
    ];
  }

  return [{ kind: "create", person, ...target }];
}

export function computeCrewForRange(
  records: AboardRecord[],
  range: Bounds,
): AboardRecord[] {
  const rStart = ms(range.started_at);
  const rEnd = ms(range.ended_at);
  return records.filter((r) => ms(r.started_at) < rEnd && ms(r.ended_at) > rStart);
}

export function isSubset(legCrewIds: number[], journeyCrewIds: number[]): boolean {
  const journeySet = new Set(journeyCrewIds);
  return legCrewIds.every((id) => journeySet.has(id));
}

export function planRemove(
  existing: AboardRecord[],
  person: number,
  target: Bounds,
): Op[] {
  const own = existing.filter((r) => r.person === person);
  const ops: Op[] = [];
  const tStart = ms(target.started_at);
  const tEnd = ms(target.ended_at);

  for (const r of own) {
    const rStart = ms(r.started_at);
    const rEnd = ms(r.ended_at);

    if (tEnd <= rStart || tStart >= rEnd) continue; // no overlap

    const coversStart = tStart <= rStart;
    const coversEnd = tEnd >= rEnd;

    if (coversStart && coversEnd) {
      ops.push({ kind: "delete", id: r.id });
    } else if (!coversStart && !coversEnd) {
      ops.push({
        kind: "update",
        id: r.id,
        started_at: r.started_at,
        ended_at: target.started_at,
      });
      ops.push({
        kind: "create",
        person,
        started_at: target.ended_at!,
        ended_at: r.ended_at,
      });
    } else if (coversStart) {
      ops.push({
        kind: "update",
        id: r.id,
        started_at: target.ended_at!,
        ended_at: r.ended_at,
      });
    } else {
      ops.push({
        kind: "update",
        id: r.id,
        started_at: r.started_at,
        ended_at: target.started_at,
      });
    }
  }

  return ops;
}
