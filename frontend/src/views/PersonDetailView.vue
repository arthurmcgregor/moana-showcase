<script setup lang="ts">
import { computed, ref, watch } from "vue";
import { useRoute, useRouter } from "vue-router";
import { useQueryCache } from "@pinia/colada";
import {
  deletePerson,
  updatePerson,
  type AboardRecord,
  type Journey,
} from "@/api/web";
import {
  peopleQueryKey,
  personQueryKey,
  useJourneysQuery,
  usePersonHistoryQuery,
  usePersonQuery,
} from "@/composables/useQueries";
import { useAuthStore } from "@/stores/auth";

const route = useRoute();
const router = useRouter();
const queryCache = useQueryCache();
const authStore = useAuthStore();

const personId = computed(() => Number(route.params.id));
const { state: personState, data: person } = usePersonQuery(personId);
const { state: historyState } = usePersonHistoryQuery(personId);
const { state: journeysState } = useJourneysQuery();

const firstName = ref("");
const lastName = ref("");
const email = ref("");
const notes = ref("");
const isSaving = ref(false);
const errorMessage = ref<string | null>(null);

watch(
  person,
  (p) => {
    if (!p) return;
    firstName.value = p.first_name;
    lastName.value = p.last_name;
    email.value = p.email;
    notes.value = p.notes;
  },
  { immediate: true },
);

async function save() {
  isSaving.value = true;
  errorMessage.value = null;
  try {
    await updatePerson(personId.value, {
      first_name: firstName.value.trim(),
      last_name: lastName.value.trim(),
      email: email.value.trim(),
      notes: notes.value,
    });
    queryCache.invalidateQueries({ key: personQueryKey(personId.value) });
    queryCache.invalidateQueries({ key: peopleQueryKey() });
  } catch (err) {
    errorMessage.value = err instanceof Error ? err.message : "Failed to save.";
  } finally {
    isSaving.value = false;
  }
}

async function remove() {
  const records = historyState.value.data?.aboard_records ?? [];
  if (!window.confirm(`Delete this person? This will remove ${records.length} aboard record(s).`)) {
    return;
  }
  try {
    await deletePerson(personId.value);
    queryCache.invalidateQueries({ key: peopleQueryKey() });
    router.push("/people");
  } catch (err) {
    errorMessage.value = err instanceof Error ? err.message : "Failed to delete.";
  }
}

interface JourneyEntry {
  journey: Journey;
  records: AboardRecord[];
  hours: number;
}

function recordHours(r: AboardRecord): number {
  if (!r.ended_at) return 0;
  return (Date.parse(r.ended_at) - Date.parse(r.started_at)) / 3_600_000;
}

const groupedHistory = computed<JourneyEntry[]>(() => {
  const records = historyState.value.data?.aboard_records ?? [];
  const journeys = journeysState.value.data ?? [];
  const entries: JourneyEntry[] = journeys.map((journey) => {
    const journeyStart = Date.parse(journey.started_at);
    const journeyEnd = journey.ended_at ? Date.parse(journey.ended_at) : Number.POSITIVE_INFINITY;
    const overlapping = records.filter((r) => {
      const rStart = Date.parse(r.started_at);
      const rEnd = r.ended_at ? Date.parse(r.ended_at) : Number.POSITIVE_INFINITY;
      return rStart < journeyEnd && rEnd > journeyStart;
    });
    const hours = overlapping.reduce((sum, r) => sum + recordHours(r), 0);
    return { journey, records: overlapping, hours };
  });
  return entries.filter((e) => e.records.length > 0);
});

const totalHours = computed(() => groupedHistory.value.reduce((s, e) => s + e.hours, 0));
const totalJourneys = computed(() => groupedHistory.value.length);

function formatHours(h: number): string {
  if (h < 1) return `${Math.round(h * 60)} min`;
  if (h < 10) return `${h.toFixed(1)} h`;
  return `${Math.round(h)} h`;
}
</script>

<template>
  <div>
    <PageHeader
      :title="person ? `${person.first_name} ${person.last_name}`.trim() || 'Person' : 'Person'"
    />

    <QueryStateWrapper
      :state="personState"
      loading-label="Loading person…"
      :empty="{ eyebrow: 'Not found', title: 'Person not found.', subtitle: '' }"
    >
      <template #default>
        <div class="space-y-6">
          <section class="rounded-lg border border-base-300 bg-base-100 p-4">
            <div class="grid gap-3 sm:grid-cols-2">
              <AppField label="First name">
                <input
                  v-model="firstName"
                  class="ui-input"
                  :disabled="!authStore.isAdmin"
                >
              </AppField>
              <AppField label="Last name">
                <input
                  v-model="lastName"
                  class="ui-input"
                  :disabled="!authStore.isAdmin"
                >
              </AppField>
              <AppField label="Email">
                <input
                  v-model="email"
                  type="email"
                  class="ui-input"
                  :disabled="!authStore.isAdmin"
                >
              </AppField>
              <AppField label="Notes">
                <textarea
                  v-model="notes"
                  class="ui-input ui-input--area"
                  rows="2"
                  :disabled="!authStore.isAdmin"
                />
              </AppField>
            </div>
            <AppNotice
              v-if="errorMessage"
              variant="error"
              class="mt-3"
            >
              {{ errorMessage }}
            </AppNotice>
            <div
              v-if="authStore.isAdmin"
              class="mt-3 flex justify-between"
            >
              <BtnText @click="remove">
                Delete
              </BtnText>
              <BtnSave
                :loading="isSaving"
                @click="save"
              >
                Save
              </BtnSave>
            </div>
          </section>

          <section>
            <div class="mb-3 flex flex-wrap items-baseline gap-x-6 gap-y-1">
              <h3 class="font-display text-lg tracking-tight">
                History
              </h3>
              <span class="font-mono text-[11px] uppercase tracking-wider text-base-content/50">
                {{ totalJourneys }} journey{{ totalJourneys === 1 ? '' : 's' }} · {{ formatHours(totalHours) }}
              </span>
            </div>

            <QueryStateWrapper
              :state="historyState"
              loading-label="Loading history…"
              :empty="{ eyebrow: 'No history', title: 'No aboard records yet.', subtitle: '' }"
            >
              <template #default>
                <ul
                  v-if="groupedHistory.length"
                  class="space-y-2"
                >
                  <li
                    v-for="entry in groupedHistory"
                    :key="entry.journey.id"
                    class="rounded-lg border border-base-300 bg-base-100 p-3"
                  >
                    <div class="flex items-baseline justify-between gap-3">
                      <RouterLink
                        :to="`/journeys/${entry.journey.id}`"
                        class="font-display text-base tracking-tight text-primary hover:underline"
                      >
                        {{ entry.journey.name }}
                      </RouterLink>
                      <span class="font-mono text-[11px] tabular-nums text-base-content/50">
                        {{ formatHours(entry.hours) }}
                      </span>
                    </div>
                    <div class="mt-0.5 font-mono text-[11px] text-base-content/40">
                      {{ entry.records.length }} record{{ entry.records.length === 1 ? '' : 's' }}
                    </div>
                  </li>
                </ul>
                <p
                  v-else
                  class="text-sm text-base-content/50"
                >
                  No aboard records yet.
                </p>
              </template>
            </QueryStateWrapper>
          </section>
        </div>
      </template>
    </QueryStateWrapper>
  </div>
</template>
