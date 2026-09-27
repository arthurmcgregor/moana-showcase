<script setup lang="ts">
import { computed } from "vue";
import type { AboardRecord } from "@/api/web";
import { useJourneyAboardQuery, usePeopleQuery } from "@/composables/useQueries";
import { useAboardMutations } from "@/composables/useAboardMutations";
import { planAdd, planRemove, computeCrewForRange, type Bounds } from "@/composables/aboardOps";
import { useAuthStore } from "@/stores/auth";
import CrewBar from "./CrewBar.vue";

// The journey's crew, wired to its own queries and mutations, so it can sit
// wherever the journey is described rather than only inside the leg tree — which
// is where it used to live, costing the rail permanent height for something read
// once a journey.

const props = defineProps<{
  journeyId: number;
  vesselId: number;
  startedAt: string;
  endedAt: string | null;
}>();

const authStore = useAuthStore();
const aboardQuery = useJourneyAboardQuery(() => props.journeyId);
const peopleQuery = usePeopleQuery();

const aboardRecords = computed<AboardRecord[]>(() => aboardQuery.data.value ?? []);
const people = computed(() => peopleQuery.data.value ?? []);

const bounds = computed<Bounds>(() => ({
  started_at: props.startedAt,
  ended_at: props.endedAt,
}));

const crew = computed(() => computeCrewForRange(aboardRecords.value, bounds.value));

const { runOps, isRunningOps, createNewPerson, isCreatingPerson } = useAboardMutations(
  () => props.journeyId,
  () => props.vesselId,
);
const isBusy = computed(() => isRunningOps.value || isCreatingPerson.value);

type AddPayload =
  | { kind: "existing"; personId: number }
  | { kind: "new"; firstName: string; lastName: string };

function applyAdd(personId: number) {
  const ops = planAdd(aboardRecords.value, personId, bounds.value);
  if (ops.length) runOps(ops);
}

async function handleAdd(payload: AddPayload) {
  if (payload.kind === "existing") {
    applyAdd(payload.personId);
    return;
  }
  const person = await createNewPerson({
    first_name: payload.firstName,
    last_name: payload.lastName,
  });
  applyAdd(person.id);
}

function handleRemove(record: AboardRecord) {
  const ops = planRemove(aboardRecords.value, record.person, bounds.value);
  if (ops.length) runOps(ops);
}

const isVisible = computed(() => authStore.isAdmin || crew.value.length > 0);
</script>

<template>
  <CrewBar
    v-if="isVisible"
    label="Crew"
    :records="crew"
    :people="people"
    :can-edit="authStore.isAdmin"
    :busy="isBusy"
    @add="handleAdd"
    @remove="handleRemove"
  />
</template>
