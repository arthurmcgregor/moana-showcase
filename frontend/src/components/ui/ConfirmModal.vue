<script setup lang="ts">
// A yes/no on something irreversible. Built from the modal primitives rather
// than `window.confirm` so a destructive step reads in the app's own voice and
// can say what survives the delete.

withDefaults(
  defineProps<{
    show: boolean;
    eyebrow?: string;
    title: string;
    body: string;
    confirmLabel?: string;
  }>(),
  { eyebrow: "Confirm", confirmLabel: "Delete" },
);

const emit = defineEmits<{
  confirm: [];
  cancel: [];
}>();
</script>

<template>
  <BaseModal
    :show="show"
    size="sm"
    @dismiss="emit('cancel')"
  >
    <ModalHeader
      :eyebrow="eyebrow"
      :title="title"
    />
    <ModalSection>
      <p class="text-sm text-base-content/70">
        {{ body }}
      </p>
    </ModalSection>
    <ModalFooter>
      <ModalButton
        variant="ghost"
        @click="emit('cancel')"
      >
        Cancel
      </ModalButton>
      <ModalButton
        variant="danger"
        @click="emit('confirm')"
      >
        {{ confirmLabel }}
      </ModalButton>
    </ModalFooter>
  </BaseModal>
</template>
