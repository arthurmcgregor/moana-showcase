<script setup lang="ts">
import { computed, type Component } from "vue";
import {
  InformationCircleIcon,
  CheckCircleIcon,
  ExclamationTriangleIcon,
} from "@heroicons/vue/24/outline";

type Variant = "info" | "success" | "warning" | "error";

const props = withDefaults(
  defineProps<{
    variant?: Variant;
  }>(),
  { variant: "info" },
);

const icon = computed<Component>(() => {
  switch (props.variant) {
    case "success": return CheckCircleIcon;
    case "warning":
    case "error": return ExclamationTriangleIcon;
    default: return InformationCircleIcon;
  }
});
</script>

<template>
  <div
    class="notice"
    :class="`notice--${variant}`"
    role="alert"
  >
    <component
      :is="icon"
      class="size-5 shrink-0"
    />
    <div class="notice__body">
      <slot />
    </div>
  </div>
</template>

<style scoped>
.notice {
  display: flex;
  align-items: flex-start;
  gap: 10px;
  padding: 12px 14px;
  font-size: 13px;
  font-weight: 500;
  border-radius: 10px;
  border: 1px solid transparent;
  line-height: 1.5;
}

.notice__body {
  flex: 1;
  min-width: 0;
}

.notice--error {
  color: color-mix(in oklch, var(--color-error) 85%, white);
  background: color-mix(in oklch, var(--color-error) 12%, transparent);
  border-color: color-mix(in oklch, var(--color-error) 35%, transparent);
}

.notice--info {
  color: var(--color-base-content);
  background: color-mix(in oklch, var(--color-info) 10%, transparent);
  border-color: color-mix(in oklch, var(--color-info) 30%, transparent);
}

.notice--success {
  color: var(--color-base-content);
  background: color-mix(in oklch, var(--color-success) 12%, transparent);
  border-color: color-mix(in oklch, var(--color-success) 35%, transparent);
}

.notice--warning {
  color: var(--color-base-content);
  background: color-mix(in oklch, var(--color-warning) 14%, transparent);
  border-color: color-mix(in oklch, var(--color-warning) 40%, transparent);
}
</style>
