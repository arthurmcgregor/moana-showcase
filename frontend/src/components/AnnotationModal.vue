<script setup lang="ts">
import { ref } from "vue";
import { annotationTypes } from "@/constants/journeyTypes";
import type { AnnotationType } from "@/api/web";
import { formatTimestampMs } from "@/utils/formatters";

const props = defineProps<{
  show: boolean;
  type: AnnotationType;
  cursorTimestamp: number;
  initialNotes?: string;
  isEdit?: boolean;
}>();

const emit = defineEmits<{
  confirm: [notes: string];
  cancel: [];
  delete: [];
}>();

const annotationNotes = ref("");

function onOpened() {
  annotationNotes.value = props.initialNotes ?? "";
}
</script>

<template>
  <BaseModal
    :show="show"
    size="md"
    @dismiss="emit('cancel')"
    @opened="onOpened"
  >
    <ModalHeader
      :eyebrow="isEdit ? 'Edit annotation' : 'New annotation'"
      :title="annotationTypes[type]?.label ?? type"
    />
    <ModalSection>
      <label
        for="annotation-notes"
        class="mb-2 block font-mono text-[10px] uppercase tracking-[0.28em] text-base-content/50"
      >
        Notes
        <span
          v-if="type === 'NOTE'"
          class="text-accent"
        >*</span>
        <span
          v-else
          class="text-base-content/40"
        >(optional)</span>
      </label>
      <textarea
        id="annotation-notes"
        v-model="annotationNotes"
        class="textarea textarea-bordered w-full"
        rows="3"
        placeholder="Add any additional details..."
      />
    </ModalSection>
    <ModalSection>
      <ModalReadout :rows="[{ label: 'At', value: formatTimestampMs(cursorTimestamp) }]" />
    </ModalSection>
    <ModalFooter>
      <ModalButton
        v-if="isEdit"
        variant="danger"
        class="mr-auto"
        @click="emit('delete')"
      >
        Delete
      </ModalButton>
      <ModalButton @click="emit('cancel')">
        Cancel
      </ModalButton>
      <ModalButton
        variant="primary"
        @click="emit('confirm', annotationNotes)"
      >
        {{ isEdit ? "Save" : "Add" }}
      </ModalButton>
    </ModalFooter>
  </BaseModal>
</template>
