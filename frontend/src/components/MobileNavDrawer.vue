<script setup lang="ts">
import { nextTick, onBeforeUnmount, ref, watch } from "vue";
import { useRouter } from "vue-router";
import type { NavLink } from "./nav/links";

const props = defineProps<{
  open: boolean;
  links: NavLink[];
  username?: string;
}>();

const emit = defineEmits<{
  "update:open": [value: boolean];
}>();

const router = useRouter();
const asideRef = ref<HTMLElement | null>(null);
const closeBtnRef = ref<HTMLButtonElement | null>(null);

let prevBodyOverflow: string | null = null;
let desktopMql: MediaQueryList | null = null;

function close() {
  emit("update:open", false);
}

function go(path: string) {
  router.push(path);
  close();
}

function onKeydown(e: KeyboardEvent) {
  if (e.key === "Escape") close();
}

function onDesktopMatch(e: MediaQueryListEvent) {
  if (e.matches) close();
}

function lockScroll() {
  if (typeof document === "undefined") return;
  prevBodyOverflow = document.body.style.overflow;
  document.body.style.overflow = "hidden";
}

function unlockScroll() {
  if (typeof document === "undefined") return;
  if (prevBodyOverflow !== null) {
    document.body.style.overflow = prevBodyOverflow;
    prevBodyOverflow = null;
  }
}

watch(
  () => props.open,
  async (open) => {
    if (open) {
      lockScroll();
      if (typeof window !== "undefined") {
        desktopMql = window.matchMedia("(min-width: 768px)");
        desktopMql.addEventListener("change", onDesktopMatch);
        if (desktopMql.matches) {
          close();
          return;
        }
      }
      await nextTick();
      closeBtnRef.value?.focus();
    } else {
      unlockScroll();
      desktopMql?.removeEventListener("change", onDesktopMatch);
      desktopMql = null;
    }
  },
);

onBeforeUnmount(() => {
  unlockScroll();
  desktopMql?.removeEventListener("change", onDesktopMatch);
  desktopMql = null;
});
</script>

<template>
  <Teleport to="body">
    <Transition name="drawer-fade">
      <div
        v-if="open"
        class="fixed inset-0 z-[200] bg-black/40 md:hidden"
        @click="close"
      />
    </Transition>
    <Transition name="drawer-slide">
      <aside
        v-if="open"
        ref="asideRef"
        class="fixed inset-y-0 right-0 z-[201] flex w-[min(320px,85vw)] flex-col bg-base-100 shadow-2xl md:hidden"
        role="dialog"
        aria-modal="true"
        aria-label="Navigation"
        tabindex="-1"
        @keydown="onKeydown"
      >
        <div class="flex items-center justify-between border-b border-base-300 px-4 py-3">
          <span class="font-display text-lg">menu</span>
          <button
            ref="closeBtnRef"
            class="btn btn-ghost btn-square btn-sm"
            aria-label="Close menu"
            @click="close"
          >
            <svg
              xmlns="http://www.w3.org/2000/svg"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              stroke-width="2"
              stroke-linecap="round"
              stroke-linejoin="round"
              class="size-5"
            >
              <line
                x1="18"
                y1="6"
                x2="6"
                y2="18"
              />
              <line
                x1="6"
                y1="6"
                x2="18"
                y2="18"
              />
            </svg>
          </button>
        </div>

        <nav class="flex-1 overflow-y-auto px-2 py-2">
          <template
            v-for="link in links"
            :key="link.label"
          >
            <button
              v-if="link.path"
              class="flex w-full items-center rounded-lg px-3 py-3 text-left text-base text-base-content/80 hover:bg-base-200 active:bg-base-300"
              @click="go(link.path)"
            >
              {{ link.label }}
            </button>
            <div
              v-else
              class="mt-2 px-3 pb-1 text-xs font-medium uppercase tracking-wide text-base-content/50"
            >
              {{ link.label }}
            </div>
            <template v-if="link.children">
              <button
                v-for="child in link.children"
                :key="child.path"
                class="flex w-full items-center rounded-lg px-6 py-3 text-left text-sm text-base-content/70 hover:bg-base-200 active:bg-base-300"
                @click="go(child.path)"
              >
                {{ child.label }}
              </button>
            </template>
          </template>
        </nav>

        <div
          v-if="username"
          class="border-t border-base-300 px-4 py-3 text-sm text-base-content/60"
        >
          {{ username }}
        </div>
      </aside>
    </Transition>
  </Teleport>
</template>

<style scoped>
.drawer-fade-enter-active,
.drawer-fade-leave-active {
  transition: opacity 180ms ease;
}
.drawer-fade-enter-from,
.drawer-fade-leave-to {
  opacity: 0;
}

.drawer-slide-enter-active,
.drawer-slide-leave-active {
  transition: transform 220ms cubic-bezier(0.2, 0.8, 0.2, 1);
}
.drawer-slide-enter-from,
.drawer-slide-leave-to {
  transform: translateX(100%);
}
</style>
