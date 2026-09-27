<script setup lang="ts" generic="T">
import { computed } from "vue";

type Status = "pending" | "success" | "error";
type EmptyContent = { eyebrow: string; title: string; subtitle: string };

const props = withDefaults(
  defineProps<{
    state: { status: Status; data: T | undefined; error: unknown };
    empty: EmptyContent;
    loadingLabel?: string;
    isEmpty?: (data: T) => boolean;
    recoverableError?: (error: unknown) => EmptyContent | null;
    // When provided, an error state shows a "Try again" button that calls this
    // (e.g. the query's refresh). The device drops off WiFi often, so a one-tap
    // retry beats a full page reload.
    onRetry?: () => void;
  }>(),
  {
    loadingLabel: "Loading…",
    isEmpty: (data: unknown) => Array.isArray(data) && data.length === 0,
    recoverableError: () => null,
    onRetry: undefined,
  },
);

defineSlots<{
  default(props: { data: T }): unknown;
}>();

const recoverable = computed(() =>
  props.state.status === "error" ? props.recoverableError(props.state.error) : null,
);

function formatError(error: unknown): string {
  if (error instanceof Error) return error.message;
  if (typeof error === "string") return error;
  return String(error);
}
</script>

<template>
  <div
    v-if="state.status === 'pending'"
    class="flex items-center justify-center gap-3 p-8"
  >
    <span class="loading loading-spinner loading-lg" />
    <span class="text-base-content/60">{{ loadingLabel }}</span>
  </div>

  <template v-else-if="state.status === 'error'">
    <EmptyState
      v-if="recoverable"
      v-bind="recoverable"
    >
      <button
        v-if="onRetry"
        class="btn btn-primary btn-sm"
        @click="onRetry"
      >
        <OutlineArrowPathIcon class="size-4" />
        Try again
      </button>
    </EmptyState>
    <div
      v-else
      role="alert"
      class="alert alert-error"
    >
      <OutlineXCircleIcon class="size-6 shrink-0 stroke-current" />
      <span>{{ formatError(state.error) }}</span>
      <button
        v-if="onRetry"
        class="btn btn-sm"
        @click="onRetry"
      >
        <OutlineArrowPathIcon class="size-4" />
        Try again
      </button>
    </div>
  </template>

  <EmptyState
    v-else-if="isEmpty(state.data as T)"
    v-bind="empty"
  />

  <slot
    v-else
    :data="state.data as T"
  />
</template>
