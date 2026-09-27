<script setup lang="ts">
import { computed, nextTick, ref } from "vue";
import type { Journey, Track } from "@/api/web";
import { formatDuration, formatTimestampMs } from "@/utils/formatters";

const props = withDefaults(
  defineProps<{
    show: boolean;
    track?: Track | null;
    journeys?: Journey[];
    currentJourneys?: Journey[];
    submitting: boolean;
    error: unknown;
    mode?: "track" | "manual";
  }>(),
  {
    track: null,
    journeys: () => [],
    currentJourneys: () => [],
    mode: "track",
  },
);

const emit = defineEmits<{
  cancel: [];
  discard: [];
  createJourney: [payload: { name: string; description: string; notes: string }];
  addToJourney: [payload: { journeyId: number }];
  createManualJourney: [
    payload: {
      name: string;
      description: string;
      notes: string;
      startedAt: string;
      endedAt: string;
    },
  ];
}>();

const nameInputRef = ref<HTMLInputElement | null>(null);
const tab = ref<"new" | "add">("new");
const name = ref("");
const description = ref("");
const notes = ref("");
const selectedJourneyId = ref<number | null>(null);

function todayIso(): string {
  const d = new Date();
  return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`;
}

const startDate = ref(todayIso());
const endDate = ref(todayIso());
const multiDay = ref(false);

async function onOpened() {
  if (props.mode === "manual") {
    name.value = "";
    description.value = "";
    notes.value = "";
    startDate.value = todayIso();
    endDate.value = todayIso();
    multiDay.value = false;
  } else {
    const track = props.track;
    tab.value = "new";
    name.value = track?.name ?? "";
    description.value = track?.description ?? "";
    notes.value = track ? `Created from track: ${track.name}` : "";
    selectedJourneyId.value = null;
  }
  await nextTick();
  nameInputRef.value?.focus();
  nameInputRef.value?.select();
}

function toggleMultiDay() {
  multiDay.value = !multiDay.value;
  if (multiDay.value) endDate.value = startDate.value;
}

// Convert local YYYY-MM-DD to a UTC ISO timestamp at the local day boundary.
function localDateToIso(dateStr: string, endOfDay: boolean): string {
  const [y, m, d] = dateStr.split("-").map(Number);
  const date = endOfDay
    ? new Date(y, m - 1, d, 23, 59, 59, 999)
    : new Date(y, m - 1, d, 0, 0, 0, 0);
  return date.toISOString();
}

function submit() {
  if (props.submitting) return;
  if (!name.value.trim()) return;

  if (props.mode === "manual") {
    if (!startDate.value) return;
    const end = multiDay.value && endDate.value ? endDate.value : startDate.value;
    emit("createManualJourney", {
      name: name.value.trim(),
      description: description.value,
      notes: notes.value,
      startedAt: localDateToIso(startDate.value, false),
      endedAt: localDateToIso(end, true),
    });
    return;
  }

  if (tab.value === "new") {
    emit("createJourney", {
      name: name.value.trim(),
      description: description.value,
      notes: notes.value,
    });
  } else if (selectedJourneyId.value !== null) {
    emit("addToJourney", { journeyId: selectedJourneyId.value });
  }
}

const readoutRows = computed(() => {
  const track = props.track;
  if (!track) return [];
  const start = formatTimestampMs(new Date(track.started_at).getTime());
  const end = track.ended_at
    ? formatTimestampMs(new Date(track.ended_at).getTime())
    : "ongoing";
  return [
    { label: "Source", value: track.name },
    { label: "Range", value: `${start} → ${end}` },
    { label: "Duration", value: formatDuration(track.started_at, track.ended_at) },
  ];
});

const submitDisabled = computed(() => {
  if (props.submitting) return true;
  if (props.mode === "manual") {
    return !name.value.trim() || !startDate.value;
  }
  if (tab.value === "new") return !name.value.trim();
  return selectedJourneyId.value === null;
});

const submitLabel = computed(() => {
  if (props.submitting) return props.mode === "manual" || tab.value === "new" ? "Creating" : "Appending";
  if (props.mode === "manual") return "Create journey";
  return tab.value === "new" ? "Create journey" : "Add to journey";
});
</script>

<template>
  <BaseModal
    :show="show"
    size="lg"
    @dismiss="emit('cancel')"
    @opened="onOpened"
  >
    <ModalHeader
      :eyebrow="mode === 'manual' ? 'Traced journey' : (tab === 'new' ? 'New journey' : 'Extend journey')"
      title="Journey"
    />

    <ModalSection v-if="track && mode !== 'manual'">
      <ModalReadout :rows="readoutRows" />
      <p
        v-if="currentJourneys.length > 0"
        class="mt-3 flex items-center gap-2 text-xs text-base-content/60"
      >
        <span class="inline-block size-1.5 rounded-full bg-accent" />
        Overlaps
        <strong class="font-semibold text-base-content">{{ currentJourneys.length }}</strong>
        existing {{ currentJourneys.length === 1 ? "journey" : "journeys" }}
      </p>
    </ModalSection>

    <nav
      v-if="mode !== 'manual'"
      role="tablist"
      class="flex gap-1 border-b border-base-300 px-5 md:px-7"
    >
      <button
        role="tab"
        :aria-selected="tab === 'new'"
        class="relative px-3 py-3 font-mono text-[11px] uppercase tracking-[0.28em] transition-colors"
        :class="tab === 'new' ? 'text-base-content' : 'text-base-content/40 hover:text-base-content/70'"
        @click="tab = 'new'"
      >
        New journey
        <span
          v-if="tab === 'new'"
          class="absolute inset-x-0 -bottom-px h-0.5 bg-accent"
        />
      </button>
      <button
        role="tab"
        :aria-selected="tab === 'add'"
        class="relative px-3 py-3 font-mono text-[11px] uppercase tracking-[0.28em] transition-colors"
        :class="tab === 'add' ? 'text-base-content' : 'text-base-content/40 hover:text-base-content/70'"
        @click="tab = 'add'"
      >
        Add to existing
        <span
          v-if="tab === 'add'"
          class="absolute inset-x-0 -bottom-px h-0.5 bg-accent"
        />
      </button>
    </nav>

    <ModalSection>
      <div
        v-if="mode === 'manual'"
        class="space-y-5"
      >
        <div>
          <label
            for="journey-name"
            class="mb-2 block font-mono text-[10px] uppercase tracking-[0.28em] text-base-content/50"
          >
            Name <span class="text-accent">*</span>
          </label>
          <input
            id="journey-name"
            ref="nameInputRef"
            v-model="name"
            class="input input-bordered w-full"
            type="text"
            placeholder="e.g. Marahau overnight"
            required
          >
        </div>
        <div>
          <label
            for="journey-start-date"
            class="mb-2 block font-mono text-[10px] uppercase tracking-[0.28em] text-base-content/50"
          >
            Date <span class="text-accent">*</span>
          </label>
          <div class="flex items-center gap-2">
            <input
              id="journey-start-date"
              v-model="startDate"
              class="input input-bordered flex-1"
              type="date"
              required
            >
            <input
              v-if="multiDay"
              v-model="endDate"
              class="input input-bordered flex-1"
              type="date"
              :min="startDate"
              aria-label="End date"
            >
          </div>
          <button
            type="button"
            class="mt-2 font-mono text-[10px] uppercase tracking-[0.28em] text-base-content/50 hover:text-accent"
            @click="toggleMultiDay"
          >
            {{ multiDay ? "— single day" : "+ multi-day" }}
          </button>
        </div>
        <div>
          <label
            for="journey-description"
            class="mb-2 block font-mono text-[10px] uppercase tracking-[0.28em] text-base-content/50"
          >
            Description
          </label>
          <textarea
            id="journey-description"
            v-model="description"
            class="textarea textarea-bordered w-full"
            rows="2"
            placeholder="Optional"
          />
        </div>
        <div>
          <label
            for="journey-notes"
            class="mb-2 block font-mono text-[10px] uppercase tracking-[0.28em] text-base-content/50"
          >
            Notes
          </label>
          <textarea
            id="journey-notes"
            v-model="notes"
            class="textarea textarea-bordered w-full leading-relaxed"
            rows="3"
          />
        </div>
      </div>

      <div
        v-else-if="tab === 'new'"
        class="space-y-5"
      >
        <div>
          <label
            for="journey-name"
            class="mb-2 block font-mono text-[10px] uppercase tracking-[0.28em] text-base-content/50"
          >
            Name <span class="text-accent">*</span>
          </label>
          <input
            id="journey-name"
            ref="nameInputRef"
            v-model="name"
            class="input input-bordered w-full"
            type="text"
            placeholder="e.g. Marahau overnight"
            required
          >
        </div>
        <div>
          <label
            for="journey-description"
            class="mb-2 block font-mono text-[10px] uppercase tracking-[0.28em] text-base-content/50"
          >
            Description
          </label>
          <textarea
            id="journey-description"
            v-model="description"
            class="textarea textarea-bordered w-full"
            rows="2"
            placeholder="Optional"
          />
        </div>
        <div>
          <label
            for="journey-notes"
            class="mb-2 block font-mono text-[10px] uppercase tracking-[0.28em] text-base-content/50"
          >
            Notes
          </label>
          <textarea
            id="journey-notes"
            v-model="notes"
            class="textarea textarea-bordered w-full leading-relaxed"
            rows="3"
          />
        </div>
      </div>

      <div
        v-else
        class="space-y-5"
      >
        <div>
          <label
            for="journey-picker"
            class="mb-2 block font-mono text-[10px] uppercase tracking-[0.28em] text-base-content/50"
          >
            Journey
          </label>
          <select
            id="journey-picker"
            v-model="selectedJourneyId"
            class="select select-bordered w-full"
          >
            <option
              :value="null"
              disabled
            >
              Select a journey
            </option>
            <option
              v-for="j in journeys"
              :key="j.id"
              :value="j.id"
            >
              {{ j.name }}
            </option>
          </select>
        </div>
        <p class="text-xs leading-relaxed text-base-content/60">
          Appends <span class="font-mono text-base-content/70">Created from track: {{ track?.name }}</span> to the journey's notes and expands its time bounds to include this track's range.
        </p>
      </div>

      <div
        v-if="error"
        role="alert"
        class="mt-5 alert alert-error"
      >
        <span>{{ error }}</span>
      </div>
    </ModalSection>

    <ModalFooter>
      <ModalButton
        v-if="mode === 'manual'"
        variant="danger"
        @click="emit('discard')"
      >
        Discard
      </ModalButton>
      <ModalButton @click="emit('cancel')">
        Cancel
      </ModalButton>
      <ModalButton
        variant="primary"
        :disabled="submitDisabled"
        @click="submit"
      >
        <span
          v-if="submitting"
          class="loading loading-spinner loading-xs mr-1 align-[-2px]"
        />
        {{ submitLabel }}
      </ModalButton>
    </ModalFooter>
  </BaseModal>
</template>
