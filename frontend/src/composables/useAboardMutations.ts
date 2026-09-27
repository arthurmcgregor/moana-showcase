import { ref } from "vue";
import { useMutation, useQueryCache } from "@pinia/colada";
import {
  createAboardRecord,
  updateAboardRecord,
  deleteAboardRecord,
  createPerson,
  type Person,
} from "@/api/web";
import {
  journeyAboardQueryKey,
  peopleQueryKey,
} from "@/composables/useQueries";
import type { Op } from "@/composables/aboardOps";

export function useAboardMutations(journeyId: () => number, vesselId: () => number) {
  const queryCache = useQueryCache();
  const aboardKey = () => journeyAboardQueryKey(journeyId());

  async function runOp(op: Op): Promise<void> {
    if (op.kind === "create") {
      await createAboardRecord({
        vessel: vesselId(),
        person: op.person,
        started_at: op.started_at,
        ended_at: op.ended_at,
        notes: "",
      });
    } else if (op.kind === "update") {
      await updateAboardRecord(op.id, {
        started_at: op.started_at,
        ended_at: op.ended_at,
      });
    } else {
      await deleteAboardRecord(op.id);
    }
  }

  const { mutate: runOps, isLoading: isRunningOps } = useMutation({
    mutation: async (ops: Op[]) => {
      for (const op of ops) await runOp(op);
    },
    onSettled: () => queryCache.invalidateQueries({ key: aboardKey() }),
  });

  const isCreatingPerson = ref(false);

  async function createNewPerson(payload: { first_name: string; last_name?: string }): Promise<Person> {
    isCreatingPerson.value = true;
    try {
      return await createPerson({
        first_name: payload.first_name,
        last_name: payload.last_name ?? "",
      });
    } finally {
      isCreatingPerson.value = false;
      queryCache.invalidateQueries({ key: peopleQueryKey() });
    }
  }

  return {
    runOps,
    isRunningOps,
    createNewPerson,
    isCreatingPerson,
  };
}
