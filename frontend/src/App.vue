<script setup lang="ts">
import { onMounted } from "vue";
import { useAuthStore } from "./stores";
import AppHeader from "./components/AppHeader.vue";

const authStore = useAuthStore();

onMounted(() => {
  authStore.loadUser();
});
</script>

<template>
  <div class="flex h-dvh flex-col">
    <AppHeader />

    <!-- 16px of chrome, not 32: the journey screen is height-bound and pays for
         the gap twice over. Phones keep none of it. -->
    <main
      class="
        flex min-h-0 flex-1 flex-col overflow-y-auto bg-base-200 p-4
        max-md:p-0
      "
    >
      <router-view />
    </main>
  </div>
</template>
