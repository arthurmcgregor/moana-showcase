<script setup lang="ts">
import { computed, nextTick, ref, watch } from "vue";
import type { AboardRecord, Person } from "@/api/web";

type AddPayload =
  | { kind: "existing"; personId: number }
  | { kind: "new"; firstName: string; lastName: string };

const props = defineProps<{
  label: string;
  records: AboardRecord[];
  people: Person[];
  canEdit: boolean;
  journeyPersonIds?: number[];
  busy?: boolean;
}>();

const emit = defineEmits<{
  add: [payload: AddPayload];
  remove: [record: AboardRecord];
}>();

const isOpen = ref(false);
const query = ref("");
const inputRef = ref<HTMLInputElement | null>(null);
const highlight = ref(0);

const journeyPersonSet = computed(() => new Set(props.journeyPersonIds ?? []));

function personLabel(p: Person): string {
  return p.last_name ? `${p.first_name} ${p.last_name}` : p.first_name;
}

const matches = computed(() => {
  const present = new Set(props.records.map((r) => r.person));
  const q = query.value.trim().toLowerCase();
  return props.people
    .filter((p) => !present.has(p.id))
    .filter((p) => !q || personLabel(p).toLowerCase().includes(q))
    .slice(0, 8);
});

const trimmedQuery = computed(() => query.value.trim());
const showCreateRow = computed(() => {
  const q = trimmedQuery.value;
  if (!q) return false;
  return !props.people.some((p) => personLabel(p).toLowerCase() === q.toLowerCase());
});

const optionCount = computed(() => matches.value.length + (showCreateRow.value ? 1 : 0));

watch(matches, () => {
  if (highlight.value >= optionCount.value) highlight.value = 0;
});

async function openComboBox() {
  isOpen.value = true;
  query.value = "";
  highlight.value = 0;
  await nextTick();
  inputRef.value?.focus();
}

function closeComboBox() {
  isOpen.value = false;
  query.value = "";
}

function commitAt(index: number) {
  if (index < matches.value.length) {
    emit("add", { kind: "existing", personId: matches.value[index].id });
  } else if (showCreateRow.value) {
    const [first, ...rest] = trimmedQuery.value.split(/\s+/);
    emit("add", { kind: "new", firstName: first, lastName: rest.join(" ") });
  }
  query.value = "";
  highlight.value = 0;
  nextTick(() => inputRef.value?.focus());
}

function onKeydown(e: KeyboardEvent) {
  if (e.key === "ArrowDown") {
    e.preventDefault();
    highlight.value = Math.min(highlight.value + 1, Math.max(optionCount.value - 1, 0));
  } else if (e.key === "ArrowUp") {
    e.preventDefault();
    highlight.value = Math.max(highlight.value - 1, 0);
  } else if (e.key === "Enter") {
    e.preventDefault();
    if (optionCount.value === 0) return;
    commitAt(highlight.value);
  } else if (e.key === "Escape") {
    e.preventDefault();
    closeComboBox();
  }
}
</script>

<template>
  <div class="flex flex-wrap items-center gap-1.5">
    <span class="font-mono text-[11px] uppercase tracking-wider text-base-content/40">
      {{ label }}
    </span>

    <span
      v-for="record in records"
      :key="record.id"
      class="inline-flex items-center gap-1 rounded-full px-2 py-0.5 text-[11px]"
      :class="
        journeyPersonSet.has(record.person)
          ? 'border border-base-content/20 text-base-content/60'
          : 'bg-primary/10 text-primary'
      "
    >
      {{ record.person_name || `Person #${record.person}` }}
      <button
        v-if="canEdit"
        type="button"
        class="opacity-60 transition-opacity hover:opacity-100"
        :title="`Remove ${record.person_name}`"
        :disabled="busy"
        @click="emit('remove', record)"
      >
        ×
      </button>
    </span>

    <template v-if="canEdit">
      <button
        v-if="!isOpen"
        type="button"
        class="rounded-full border border-dashed border-base-content/30 px-2 py-0.5 text-[11px] text-base-content/60 transition-colors hover:border-primary hover:text-primary"
        :disabled="busy"
        @click="openComboBox"
      >
        + add
      </button>
      <div
        v-else
        class="relative"
        @keydown="onKeydown"
      >
        <input
          ref="inputRef"
          v-model="query"
          type="text"
          class="input input-bordered input-xs w-40"
          placeholder="Type a name…"
          @blur="closeComboBox"
        >
        <ul
          v-if="matches.length || showCreateRow"
          class="absolute left-0 top-full z-10 mt-1 max-h-64 w-56 overflow-auto rounded-md border border-base-300 bg-base-100 py-1 text-sm shadow-lg"
        >
          <li
            v-for="(person, idx) in matches"
            :key="person.id"
            class="cursor-pointer px-2 py-1"
            :class="idx === highlight ? 'bg-primary/10 text-primary' : ''"
            @mousedown.prevent="commitAt(idx)"
            @mouseenter="highlight = idx"
          >
            {{ personLabel(person) }}
          </li>
          <li
            v-if="showCreateRow"
            :key="'create'"
            class="cursor-pointer border-t border-base-300 px-2 py-1 font-mono text-[12px]"
            :class="highlight === matches.length ? 'bg-primary/10 text-primary' : ''"
            @mousedown.prevent="commitAt(matches.length)"
            @mouseenter="highlight = matches.length"
          >
            + Create &ldquo;{{ trimmedQuery }}&rdquo;
          </li>
        </ul>
      </div>
    </template>
  </div>
</template>
