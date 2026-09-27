import { defineStore } from "pinia";
import { ref } from "vue";
import { fetchMe } from "@/api/web";

export const useAuthStore = defineStore("auth", () => {
  const username = ref("");
  const isAdmin = ref(false);
  const isLoaded = ref(false);

  let inflight: Promise<void> | null = null;

  function loadUser() {
    if (isLoaded.value) return Promise.resolve();
    if (inflight) return inflight;
    inflight = (async () => {
      try {
        const user = await fetchMe();
        username.value = user.username;
        isAdmin.value = user.is_admin;
      } catch (e) {
        console.error("Failed to load user info:", e);
        username.value = "";
        isAdmin.value = false;
      } finally {
        isLoaded.value = true;
        inflight = null;
      }
    })();
    return inflight;
  }

  return {
    username,
    isAdmin,
    isLoaded,
    loadUser,
  };
});
