<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from "vue";
import { useRouter } from "vue-router";
import { useAuthStore } from "@/stores";
import { filterNavLinks, navLinks } from "./nav/links";

const router = useRouter();
const authStore = useAuthStore();
const appVersion = import.meta.env.VITE_APP_VERSION;

const headerRef = ref<HTMLElement | null>(null);
const mobileOpen = ref(false);

function closeAllDropdowns() {
  headerRef.value?.querySelectorAll("details[open]").forEach((el) => {
    el.removeAttribute("open");
  });
}

function onClickOutside(e: MouseEvent) {
  if (!headerRef.value?.contains(e.target as Node)) {
    closeAllDropdowns();
  }
}

function navigateAndClose(path: string) {
  closeAllDropdowns();
  router.push(path);
}

const visibleLinks = computed(() => filterNavLinks(navLinks, authStore.isAdmin));

onMounted(() => document.addEventListener("click", onClickOutside));
onUnmounted(() => document.removeEventListener("click", onClickOutside));
</script>

<template>
  <header
    ref="headerRef"
    class="nav-bar"
  >
    <div class="nav-inner">
      <a
        class="nav-brand"
        href="/"
        @click.prevent="router.push('/')"
      >
        <svg
          xmlns="http://www.w3.org/2000/svg"
          viewBox="0 0 64 64"
          class="nav-brand-mark"
          aria-hidden="true"
        >
          <line
            x1="38"
            y1="6"
            x2="38"
            y2="42"
            stroke="#b2ebf2"
            stroke-width="2"
            stroke-linecap="round"
          />
          <line
            x1="44"
            y1="14"
            x2="16"
            y2="6"
            stroke="#b2ebf2"
            stroke-width="1.5"
            stroke-linecap="round"
          />
          <line
            x1="42"
            y1="36"
            x2="18"
            y2="32"
            stroke="#b2ebf2"
            stroke-width="1.5"
            stroke-linecap="round"
          />
          <polygon
            points="44,14 16,6 18,32 42,36"
            fill="#e0f7fa"
            opacity="0.85"
          />
          <line
            x1="43.5"
            y1="20"
            x2="16.5"
            y2="13"
            stroke="#b2ebf2"
            stroke-width="0.5"
            opacity="0.5"
          />
          <line
            x1="43"
            y1="25.5"
            x2="17"
            y2="19.5"
            stroke="#b2ebf2"
            stroke-width="0.5"
            opacity="0.5"
          />
          <line
            x1="42.5"
            y1="31"
            x2="17.5"
            y2="25.5"
            stroke="#b2ebf2"
            stroke-width="0.5"
            opacity="0.5"
          />
          <path
            d="M16 44h36l-4 6H20z"
            fill="currentColor"
            opacity="0.9"
          />
          <path
            d="M22 47h28"
            stroke="#b2ebf2"
            stroke-width="0.7"
            opacity="0.5"
          />
          <path
            d="M4 54c4-4 8-6 12-6s8 4 12 4 8-4 12-4 8 4 12 4c2 0 4-0.5 6-1.5"
            fill="none"
            stroke="#4dd0e1"
            stroke-width="3"
            stroke-linecap="round"
          />
          <path
            d="M4 60c4-4 8-6 12-6s8 4 12 4 8-4 12-4 8 4 12 4c2 0 4-0.5 6-1.5"
            fill="none"
            stroke="#0288d1"
            stroke-width="3"
            stroke-linecap="round"
            opacity="0.6"
          />
        </svg>
        <span class="nav-brand-name">moana</span>
        <span
          v-if="appVersion"
          class="nav-version"
        >{{ appVersion }}</span>
      </a>

      <button
        class="nav-hamburger inline-flex md:hidden"
        aria-label="Open menu"
        @click="mobileOpen = true"
      >
        <svg
          xmlns="http://www.w3.org/2000/svg"
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          stroke-width="2"
          stroke-linecap="round"
          stroke-linejoin="round"
          aria-hidden="true"
        >
          <line
            x1="3"
            y1="6"
            x2="21"
            y2="6"
          />
          <line
            x1="3"
            y1="12"
            x2="21"
            y2="12"
          />
          <line
            x1="3"
            y1="18"
            x2="21"
            y2="18"
          />
        </svg>
      </button>

      <nav class="nav-right hidden md:flex">
        <template
          v-for="link in visibleLinks"
          :key="link.label"
        >
          <a
            v-if="link.path"
            class="nav-tab"
            :href="link.path"
            @click.prevent="router.push(link.path)"
          >{{ link.label }}</a>
          <details
            v-else
            class="nav-dropdown"
          >
            <summary class="nav-tab">
              {{ link.label }}
            </summary>
            <div class="nav-dropdown-panel">
              <a
                v-for="child in link.children"
                :key="child.path"
                class="nav-dropdown-item"
                :href="child.path"
                @click.prevent="navigateAndClose(child.path)"
              >{{ child.label }}</a>
            </div>
          </details>
        </template>
        <span
          v-if="authStore.username"
          class="nav-user"
        >
          {{ authStore.username }}
        </span>
      </nav>
    </div>
    <MobileNavDrawer
      v-model:open="mobileOpen"
      :links="visibleLinks"
      :username="authStore.username"
    />
  </header>
</template>

<style scoped>
.nav-bar {
  background: linear-gradient(180deg, var(--color-base-100) 0%, var(--color-base-200) 100%);
  border-bottom: 1px solid var(--color-base-300);
  padding: 10px 0;
  position: sticky;
  top: 0;
  z-index: 50;
  backdrop-filter: saturate(140%);
}

.nav-inner {
  margin: 0 auto;
  padding: 0 16px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.nav-brand {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  color: var(--color-base-content);
  text-decoration: none;
  cursor: pointer;
}

.nav-brand-mark {
  width: 26px;
  height: 26px;
  display: block;
  filter: drop-shadow(0 1px 1px rgba(0, 0, 0, 0.35));
}

.nav-brand-name {
  font-family: var(--font-sans);
  font-size: 16px;
  font-weight: 600;
  letter-spacing: -0.01em;
  position: relative;
  padding-bottom: 2px;
}

.nav-brand-name::after {
  content: "";
  position: absolute;
  left: 0;
  right: 0;
  bottom: -1px;
  height: 2px;
  background: oklch(0.75 0.12 194); /* teal accent */
  border-radius: 1px;
}

.nav-version {
  font-size: 11px;
  font-weight: 400;
  opacity: 0.45;
}

.nav-right {
  align-items: center;
  gap: 6px;
}

.nav-tab {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 5px 10px;
  border-radius: 999px;
  color: var(--color-base-content, #c9d1d9);
  opacity: 0.75;
  text-decoration: none;
  font-size: 13px;
  font-weight: 500;
  border: 1px solid transparent;
  cursor: pointer;
  transition: background-color 120ms ease, border-color 120ms ease, opacity 120ms ease;
  list-style: none;
}

.nav-tab:hover {
  opacity: 1;
  background: color-mix(in oklch, var(--color-base-content) 8%, transparent);
  border-color: color-mix(in oklch, var(--color-base-content) 12%, transparent);
}

.nav-hamburger {
  align-items: center;
  justify-content: center;
  width: 44px;
  height: 44px;
  padding: 0;
  background: transparent;
  border: 1px solid transparent;
  border-radius: 10px;
  color: var(--color-base-content);
  opacity: 0.8;
  cursor: pointer;
  transition: background-color 120ms ease, border-color 120ms ease, opacity 120ms ease;
}

.nav-hamburger:hover,
.nav-hamburger:active {
  opacity: 1;
  background: color-mix(in oklch, var(--color-base-content) 8%, transparent);
  border-color: color-mix(in oklch, var(--color-base-content) 12%, transparent);
}

.nav-hamburger svg {
  width: 22px;
  height: 22px;
}

.nav-user {
  font-size: 13px;
  opacity: 0.5;
  margin-left: 4px;
}

.nav-dropdown {
  position: relative;
}

.nav-dropdown summary::-webkit-details-marker { display: none; }
.nav-dropdown summary { list-style: none; }

.nav-dropdown-panel {
  position: absolute;
  inset-inline-end: 0;
  top: calc(100% + 6px);
  background: var(--color-base-100);
  border: 1px solid var(--color-base-300);
  border-radius: 8px;
  padding: 4px;
  min-width: 160px;
  max-width: calc(100vw - 24px);
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.15);
  animation: dropIn 140ms cubic-bezier(0.2, 0.8, 0.2, 1);
  z-index: 100;
}

@keyframes dropIn {
  from { opacity: 0; transform: translateY(-4px); }
}

.nav-dropdown-item {
  display: block;
  padding: 6px 10px;
  border-radius: 6px;
  color: var(--color-base-content);
  opacity: 0.75;
  text-decoration: none;
  font-size: 13px;
  cursor: pointer;
  transition: background-color 100ms ease, opacity 100ms ease;
}

.nav-dropdown-item:hover {
  opacity: 1;
  background: color-mix(in oklch, var(--color-base-content) 8%, transparent);
}
</style>
