<script setup lang="ts">
import { computed, nextTick, onMounted, ref, watch } from "vue";

const props = withDefaults(
  defineProps<{
    show: boolean;
    size?: "sm" | "md" | "lg" | "xl";
  }>(),
  { size: "md" },
);

const emit = defineEmits<{
  dismiss: [];
  opened: [];
}>();

const dialogRef = ref<HTMLDialogElement | null>(null);
let closingProgrammatically = false;

const widthClass = computed(() => ({
  sm: "max-w-md",
  md: "max-w-lg",
  lg: "max-w-xl",
  xl: "max-w-2xl",
}[props.size]));

async function openDialog() {
  if (!dialogRef.value || dialogRef.value.open) return;
  dialogRef.value.showModal();
  await nextTick();
  emit("opened");
}

function closeDialog() {
  if (!dialogRef.value?.open) return;
  closingProgrammatically = true;
  dialogRef.value.close();
}

function onNativeClose() {
  if (closingProgrammatically) {
    closingProgrammatically = false;
    return;
  }
  emit("dismiss");
}

onMounted(() => {
  if (props.show) openDialog();
});

watch(() => props.show, (shown) => {
  if (shown) openDialog();
  else closeDialog();
});
</script>

<template>
  <dialog
    ref="dialogRef"
    class="modal"
    @close="onNativeClose"
  >
    <div
      :class="[
        widthClass,
        'modal-box w-[calc(100vw-1rem)] max-h-[calc(100dvh-2rem)]',
        'overflow-y-auto rounded-box border border-base-300 bg-base-100',
        'p-0 shadow-2xl',
      ]"
    >
      <slot />
    </div>
    <form
      method="dialog"
      class="modal-backdrop backdrop-blur-sm"
    >
      <button>close</button>
    </form>
  </dialog>
</template>
