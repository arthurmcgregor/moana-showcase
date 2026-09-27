<script setup lang="ts">
import { useRouter, type RouteLocationRaw } from "vue-router";
import type { MetaItem } from "@/utils/listHelpers";

const props = defineProps<{
  to: RouteLocationRaw;
  title: string;
  description?: string;
  meta: MetaItem[];
}>();

const router = useRouter();

function navigate() {
  router.push(props.to);
}
</script>

<template>
  <li
    class="list-row group cursor-pointer transition-colors hover:bg-base-content/3"
    @click="navigate"
  >
    <div
      v-if="$slots.media"
      class="relative aspect-square w-24 shrink-0 overflow-hidden rounded-box sm:size-32 md:size-40"
    >
      <slot name="media" />
    </div>

    <div class="list-col-grow">
      <div class="flex flex-wrap items-center gap-2">
        <div class="font-display text-xl tracking-tight">
          {{ title }}
        </div>
        <slot name="cross-ref" />
      </div>
      <div
        v-if="description"
        class="text-sm text-base-content/50"
      >
        {{ description }}
      </div>
      <div class="mt-3 grid grid-cols-2 gap-x-4 gap-y-2 sm:flex sm:gap-5">
        <div
          v-for="item in meta"
          :key="item.label"
        >
          <div class="text-[11px] font-mono uppercase tracking-wider text-base-content/40">
            {{ item.label }}
          </div>
          <div class="text-sm font-medium">
            {{ item.value }}
          </div>
        </div>
      </div>
    </div>
  </li>
</template>
