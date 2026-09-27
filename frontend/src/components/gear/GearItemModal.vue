<script setup lang="ts">
import { computed, nextTick, ref, watch } from "vue";
import type { Item, Tag } from "@/api/gear";
import { MODAL_CATEGORY_ORDER } from "@/composables/useGearQueries";

const props = defineProps<{
  show: boolean;
  item: Item | null;
  categories: Map<string, { id: number; values: Tag[] }>;
  defaultTagIds?: number[];
}>();

const emit = defineEmits<{
  cancel: [];
  save: [payload: { id?: number; name: string; tags: number[] }];
  remove: [id: number];
}>();

const nameInputRef = ref<HTMLInputElement | null>(null);
const name = ref("");
const selectedTags = ref<Set<number>>(new Set());

const categoryEntries = computed(() => {
  const entries: [string, { id: number; values: Tag[] }][] = [];
  const seen = new Set<string>();
  for (const name of MODAL_CATEGORY_ORDER) {
    const cat = props.categories.get(name);
    if (cat) {
      entries.push([name, cat]);
      seen.add(name);
    }
  }
  for (const [name, cat] of props.categories) {
    if (!seen.has(name)) entries.push([name, cat]);
  }
  return entries;
});

watch(
  () => props.show,
  async (s) => {
    if (!s) return;
    name.value = props.item?.name ?? "";
    const initial = props.item?.tags ?? props.defaultTagIds ?? [];
    selectedTags.value = new Set(initial);
    await nextTick();
    nameInputRef.value?.focus();
    nameInputRef.value?.select();
  },
);

function toggleTag(id: number) {
  if (selectedTags.value.has(id)) selectedTags.value.delete(id);
  else selectedTags.value.add(id);
  selectedTags.value = new Set(selectedTags.value);
}

function submit() {
  const trimmed = name.value.trim();
  if (!trimmed) return;
  emit("save", {
    id: props.item?.id,
    name: trimmed,
    tags: Array.from(selectedTags.value),
  });
}

function remove() {
  if (props.item?.id && confirm(`Delete "${props.item.name}"?`)) {
    emit("remove", props.item.id);
  }
}
</script>

<template>
  <BaseModal
    :show="show"
    size="lg"
    @dismiss="emit('cancel')"
  >
    <ModalHeader
      :eyebrow="item ? 'Edit item' : 'New item'"
      :title="item ? item.name || 'Item' : 'New item'"
    />

    <ModalSection>
      <div class="space-y-5">
        <div>
          <label
            for="gear-item-name"
            class="mb-2 block font-mono text-[10px] uppercase tracking-[0.28em] text-base-content/50"
          >
            Name <span class="text-accent">*</span>
          </label>
          <input
            id="gear-item-name"
            ref="nameInputRef"
            v-model="name"
            class="input input-bordered w-full"
            type="text"
            placeholder="e.g. Headtorch"
            @keydown.enter="submit"
          >
        </div>

        <div
          v-for="[catName, cat] in categoryEntries"
          :key="catName"
        >
          <label class="mb-2 block font-mono text-[10px] uppercase tracking-[0.28em] text-base-content/50">
            {{ catName.replace("_", " ") }}
          </label>
          <div class="flex flex-wrap gap-2">
            <button
              v-for="tag in cat.values"
              :key="tag.id"
              type="button"
              class="modal-chip"
              :class="{ 'modal-chip--on': selectedTags.has(tag.id) }"
              @click="toggleTag(tag.id)"
            >
              {{ tag.name }}
            </button>
          </div>
        </div>
      </div>
    </ModalSection>

    <ModalFooter>
      <ModalButton
        v-if="item"
        variant="danger"
        @click="remove"
      >
        Delete
      </ModalButton>
      <ModalButton @click="emit('cancel')">
        Cancel
      </ModalButton>
      <ModalButton
        variant="primary"
        :disabled="!name.trim()"
        @click="submit"
      >
        Save
      </ModalButton>
    </ModalFooter>
  </BaseModal>
</template>

<style scoped>
.modal-chip {
  display: inline-flex;
  align-items: center;
  padding: 4px 10px;
  border-radius: 999px;
  border: 1px solid var(--color-base-300);
  background: var(--color-base-100);
  color: var(--color-base-content);
  font-size: 12px;
  opacity: 0.7;
  cursor: pointer;
  transition: background-color 120ms ease, border-color 120ms ease, opacity 120ms ease;
}

.modal-chip:hover {
  opacity: 1;
}

.modal-chip--on {
  opacity: 1;
  background: color-mix(in oklch, var(--color-accent) 18%, transparent);
  border-color: var(--color-accent);
}
</style>
