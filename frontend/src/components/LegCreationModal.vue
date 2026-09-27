<script setup lang="ts">
import { computed, nextTick, ref } from "vue";
import { formatTimestampMs } from "@/utils/formatters";

const props = defineProps<{
  show: boolean;
  startTime: string | null;
  cursorTimestamp: number;
}>();

const emit = defineEmits<{
  confirm: [name: string, notes: string];
  cancel: [];
}>();

const nameInput = ref<HTMLInputElement | null>(null);
const legNameInput = ref("");
const legNotesInput = ref("");

async function onOpened() {
  legNameInput.value = "";
  legNotesInput.value = "";
  await nextTick();
  nameInput.value?.focus();
}

function save() {
  const name = legNameInput.value.trim();
  if (!name) return;
  emit("confirm", name, legNotesInput.value.trim());
}

const readoutRows = computed(() => [
  {
    label: "Start",
    value: props.startTime ? formatTimestampMs(new Date(props.startTime).getTime()) : "—",
  },
  { label: "End", value: formatTimestampMs(props.cursorTimestamp) },
]);
</script>

<template>
  <BaseModal
    :show="show"
    size="md"
    @dismiss="emit('cancel')"
    @opened="onOpened"
  >
    <ModalHeader
      eyebrow="New leg"
      title="Name this leg"
    />
    <ModalSection>
      <label
        for="leg-name"
        class="mb-2 block font-mono text-[10px] uppercase tracking-[0.28em] text-base-content/50"
      >
        Leg name <span class="text-accent">*</span>
      </label>
      <input
        id="leg-name"
        ref="nameInput"
        v-model="legNameInput"
        class="input input-bordered w-full"
        type="text"
        placeholder="Enter leg name"
        required
        @keydown.enter="save"
      >
    </ModalSection>
    <ModalSection>
      <label
        for="leg-description"
        class="mb-2 block font-mono text-[10px] uppercase tracking-[0.28em] text-base-content/50"
      >
        Description
      </label>
      <!-- No enter-to-save here: the leg's account of itself is the one field
           worth more than one line. -->
      <textarea
        id="leg-description"
        v-model="legNotesInput"
        class="textarea textarea-bordered w-full"
        rows="2"
        placeholder="Optional"
      />
    </ModalSection>
    <ModalSection>
      <ModalReadout :rows="readoutRows" />
    </ModalSection>
    <ModalFooter>
      <ModalButton @click="emit('cancel')">
        Cancel
      </ModalButton>
      <ModalButton
        variant="primary"
        :disabled="!legNameInput.trim()"
        @click="save"
      >
        Save
      </ModalButton>
    </ModalFooter>
  </BaseModal>
</template>
