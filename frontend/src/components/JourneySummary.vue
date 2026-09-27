<script setup lang="ts">
import { ref, computed, watch } from "vue";
import { formatDateTime, formatDurationSeconds } from "@/utils/formatters";
import { formatJourneyDistance, isTraceOnlyJourney } from "@/utils/listHelpers";
import type { Journey, Annotation } from "../api/web";

const props = withDefaults(
  defineProps<{
    journey: Journey;
    annotations: Annotation[];
    notes?: string;
    isEditing?: boolean;
  }>(),
  { notes: undefined, isEditing: false },
);

const emit = defineEmits<{
  save: [payload: { name: string; description: string; notes: string }];
}>();

const legsCount = computed(() => props.journey.legs_count ?? props.journey.legs?.length ?? 0);
const hasLegs = computed(() => legsCount.value > 0);
const traceOnly = computed(() => isTraceOnlyJourney(props.journey));
const distanceText = computed(() => formatJourneyDistance(props.journey));

const editName = ref(props.journey.name ?? "");
const editDescription = ref(props.journey.description ?? "");
const editNotes = ref(props.notes ?? "");

watch(
  () => [props.journey.name, props.journey.description, props.notes],
  ([name, desc, notes]) => {
    editName.value = name ?? "";
    editDescription.value = desc ?? "";
    editNotes.value = notes ?? "";
  },
);

const isDirty = computed(() =>
  editName.value !== (props.journey.name ?? "") ||
  editDescription.value !== (props.journey.description ?? "") ||
  editNotes.value !== (props.notes ?? ""),
);

function revert() {
  editName.value = props.journey.name ?? "";
  editDescription.value = props.journey.description ?? "";
  editNotes.value = props.notes ?? "";
}

function save() {
  emit("save", {
    name: editName.value,
    description: editDescription.value,
    notes: editNotes.value,
  });
}
</script>

<template>
  <div class="space-y-6">
    <div class="flex flex-wrap gap-x-8 gap-y-4">
      <div>
        <div class="text-[11px] font-mono uppercase tracking-wider text-base-content/40">
          Vessel
        </div>
        <div class="text-sm font-medium">
          {{ journey.vessel_name ?? "Unknown" }}
        </div>
      </div>
      <div>
        <div class="text-[11px] font-mono uppercase tracking-wider text-base-content/40">
          Started
        </div>
        <div class="text-sm font-medium">
          {{ formatDateTime(journey.started_at) }}
        </div>
      </div>
      <div v-if="journey.ended_at">
        <div class="text-[11px] font-mono uppercase tracking-wider text-base-content/40">
          Ended
        </div>
        <div class="text-sm font-medium">
          {{ formatDateTime(journey.ended_at) }}
        </div>
      </div>
      <div v-if="!traceOnly">
        <div class="text-[11px] font-mono uppercase tracking-wider text-base-content/40">
          Duration
        </div>
        <div class="text-sm font-medium">
          {{ journey.duration != null ? formatDurationSeconds(journey.duration) : "Ongoing" }}
        </div>
      </div>
      <div>
        <div class="text-[11px] font-mono uppercase tracking-wider text-base-content/40">
          Distance
        </div>
        <div class="text-sm font-medium">
          {{ distanceText }}
        </div>
      </div>
      <div v-if="hasLegs">
        <div class="text-[11px] font-mono uppercase tracking-wider text-base-content/40">
          Legs
        </div>
        <div class="text-sm font-medium">
          {{ legsCount }}
        </div>
      </div>
      <div v-if="annotations.length">
        <div class="text-[11px] font-mono uppercase tracking-wider text-base-content/40">
          Annotations
        </div>
        <div class="text-sm font-medium">
          {{ annotations.length }}
        </div>
      </div>
    </div>

    <!-- Crew. Lives here rather than above the leg tree: it is read once a
         journey, and in the rail it cost permanent height that the tree and the
         statistics both wanted. -->
    <JourneyCrewBar
      :journey-id="journey.id"
      :vessel-id="journey.vessel"
      :started-at="journey.started_at"
      :ended-at="journey.ended_at ?? null"
    />

    <!-- Editing mode -->
    <form
      v-if="isEditing"
      class="edit-form"
      @submit.prevent="save"
    >
      <div class="edit-form__header">
        <span class="edit-form__eyebrow">Editing</span>
        <span
          v-if="isDirty"
          class="edit-form__dirty"
        >
          <span class="edit-form__dirty-dot" />
          Unsaved changes
        </span>
      </div>

      <AppField label="Name">
        <input
          v-model="editName"
          type="text"
          class="ui-input"
          placeholder="Journey name"
        >
      </AppField>

      <AppField label="Description">
        <input
          v-model="editDescription"
          type="text"
          class="ui-input"
          placeholder="Short one-line summary"
        >
      </AppField>

      <AppField label="Notes">
        <textarea
          v-model="editNotes"
          class="ui-input ui-input--area"
          rows="5"
          placeholder="Conditions, crew, anything worth remembering…"
        />
      </AppField>

      <div class="edit-form__actions">
        <BtnText
          :disabled="!isDirty"
          @click="revert"
        >
          Discard
        </BtnText>
        <BtnSave :disabled="!isDirty">
          Save changes
        </BtnSave>
      </div>
    </form>

    <!-- Read-only notes -->
    <div
      v-else-if="notes"
      class="border-t border-base-300 pt-5"
    >
      <div class="text-[11px] font-mono uppercase tracking-wider text-base-content/40 mb-1">
        Notes
      </div>
      <p class="whitespace-pre-wrap text-sm text-base-content/60">
        {{ notes }}
      </p>
    </div>
  </div>
</template>

<style scoped>
.edit-form {
  display: flex;
  flex-direction: column;
  gap: 20px;
  padding-top: 24px;
  border-top: 1px solid var(--color-base-300);
}

.edit-form__header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.edit-form__eyebrow {
  font-family: var(--font-mono);
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.12em;
  text-transform: uppercase;
  color: var(--color-primary);
}

.edit-form__dirty {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  font-size: 11px;
  letter-spacing: 0.04em;
  color: color-mix(in oklch, var(--color-base-content) 55%, transparent);
}

.edit-form__dirty-dot {
  width: 6px;
  height: 6px;
  border-radius: 999px;
  background: var(--color-warning, #f5a623);
  box-shadow: 0 0 0 3px color-mix(in oklch, var(--color-warning, #f5a623) 20%, transparent);
}

.edit-form__actions {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 8px;
  padding-top: 4px;
}
</style>
