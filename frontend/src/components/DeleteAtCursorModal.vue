<script setup lang="ts">
import { computed } from "vue";
import type { Annotation, Mode } from "@/api/web";
import { annotationTypes, modeTypes } from "@/constants/journeyTypes";
import { formatTimestampMs } from "@/utils/formatters";

const props = defineProps<{
  show: boolean;
  timestamp: number;
  coordinates: [number, number] | null;
  annotations: Annotation[];
  mode: Mode | null;
  modeStartsHere: boolean;
  precedingMode: Mode | null;
}>();

const emit = defineEmits<{
  deleteAnnotation: [id: number];
  deleteMode: [id: number];
  deletePoint: [];
  cancel: [];
}>();

const formattedCoords = computed(() => {
  if (!props.coordinates) return null;
  const [lng, lat] = props.coordinates;
  const latHem = lat >= 0 ? "N" : "S";
  const lngHem = lng >= 0 ? "E" : "W";
  return `${Math.abs(lat).toFixed(6)}° ${latHem}   ${Math.abs(lng).toFixed(6)}° ${lngHem}`;
});

const modeRevertLabel = computed(() => {
  if (!props.mode) return "";
  const prev = props.precedingMode;
  if (!prev) return "leaves journey with no mode";
  if (prev.mode === props.mode.mode) return "reverts split";
  return `reverts to ${modeTypes[prev.mode].label}`;
});
</script>

<template>
  <BaseModal
    :show="show"
    size="lg"
    @dismiss="emit('cancel')"
  >
    <ModalHeader
      eyebrow="Delete at cursor"
      :title="formatTimestampMs(timestamp)"
    />

    <ModalSection v-if="formattedCoords">
      <ModalReadout :rows="[{ label: 'Pos', value: formattedCoords }]" />
    </ModalSection>

    <ModalSection v-if="annotations.length > 0">
      <ModalSectionLabel label="Annotations">
        <template #trailing>
          <span class="tabular-nums">{{ annotations.length }}</span>
        </template>
      </ModalSectionLabel>
      <ul class="divide-y divide-base-300/50 overflow-hidden rounded border border-base-300/60 bg-base-100/40">
        <li
          v-for="ann in annotations"
          :key="ann.id"
          class="group flex items-center gap-3 py-1.5 pl-0 pr-2"
        >
          <span
            class="h-7 w-1 shrink-0 rounded-r"
            :style="{ backgroundColor: annotationTypes[ann.type].color }"
          />
          <span
            class="inline-flex h-6 min-w-6 shrink-0 items-center justify-center rounded px-1.5 font-mono text-[11px]"
            :style="{
              backgroundColor: `${annotationTypes[ann.type].color}22`,
              color: annotationTypes[ann.type].color,
            }"
          >
            {{ annotationTypes[ann.type].icon }}
          </span>
          <span class="min-w-0 grow truncate text-sm">
            <span>{{ annotationTypes[ann.type].label }}</span>
            <span
              v-if="ann.notes"
              class="ml-1.5 text-base-content/50"
            >· {{ ann.notes }}</span>
          </span>
          <button
            class="btn btn-ghost btn-xs btn-square text-base-content/40 hover:bg-error/15 hover:text-error"
            title="Delete annotation"
            @click="emit('deleteAnnotation', ann.id)"
          >
            <OutlineTrashIcon class="size-3.5" />
          </button>
        </li>
      </ul>
    </ModalSection>

    <ModalSection v-if="mode && modeStartsHere">
      <ModalSectionLabel label="Mode transition" />
      <div class="group flex items-center gap-3 overflow-hidden rounded border border-base-300/60 bg-base-100/40 py-1.5 pl-0 pr-2">
        <span
          class="h-7 w-1 shrink-0 rounded-r"
          :style="{ backgroundColor: modeTypes[mode.mode].color }"
        />
        <span
          class="inline-flex h-6 min-w-6 shrink-0 items-center justify-center rounded px-1.5 font-mono text-[11px]"
          :style="{
            backgroundColor: `${modeTypes[mode.mode].color}22`,
            color: modeTypes[mode.mode].color,
          }"
        >
          {{ modeTypes[mode.mode].icon }}
        </span>
        <span class="min-w-0 grow truncate text-sm">
          <span>{{ modeTypes[mode.mode].label }}</span>
          <span class="ml-1.5 text-base-content/50">→ {{ modeRevertLabel }}</span>
        </span>
        <button
          class="btn btn-ghost btn-xs btn-square text-base-content/40 hover:bg-error/15 hover:text-error"
          title="Delete mode transition"
          @click="emit('deleteMode', mode.id)"
        >
          <OutlineTrashIcon class="size-3.5" />
        </button>
      </div>
    </ModalSection>

    <ModalSection>
      <ModalSectionLabel
        label="GPS point"
        tone="danger"
      >
        <template #trailing>
          hard
        </template>
      </ModalSectionLabel>
      <div class="flex items-center gap-3 rounded border border-error/40 bg-error/5 px-3 py-2">
        <p class="grow text-xs text-base-content/75">
          Removes this point from its track <span class="text-base-content/50">and</span>
          all annotations <span class="text-base-content/50">&</span> mode transitions at this timestamp.
        </p>
        <button
          class="btn btn-error btn-sm shrink-0 gap-1.5"
          @click="emit('deletePoint')"
        >
          <OutlineTrashIcon class="size-3.5" />
          Delete point
        </button>
      </div>
    </ModalSection>

    <ModalFooter>
      <ModalButton @click="emit('cancel')">
        Close
      </ModalButton>
    </ModalFooter>
  </BaseModal>
</template>
