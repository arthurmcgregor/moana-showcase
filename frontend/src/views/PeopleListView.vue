<script setup lang="ts">
import { ref } from "vue";
import { useQueryCache } from "@pinia/colada";
import { createPerson, type Person } from "@/api/web";
import { peopleQueryKey, usePeopleQuery } from "@/composables/useQueries";
import { useAuthStore } from "@/stores/auth";

const authStore = useAuthStore();
const queryCache = useQueryCache();
const { state } = usePeopleQuery();

const showForm = ref(false);
const firstName = ref("");
const lastName = ref("");
const email = ref("");
const isSaving = ref(false);
const errorMessage = ref<string | null>(null);

function openForm() {
  errorMessage.value = null;
  firstName.value = "";
  lastName.value = "";
  email.value = "";
  showForm.value = true;
}

async function submit() {
  if (!firstName.value.trim()) {
    errorMessage.value = "First name is required.";
    return;
  }
  isSaving.value = true;
  errorMessage.value = null;
  try {
    await createPerson({
      first_name: firstName.value.trim(),
      last_name: lastName.value.trim(),
      email: email.value.trim(),
    });
    queryCache.invalidateQueries({ key: peopleQueryKey() });
    showForm.value = false;
  } catch (err) {
    errorMessage.value = err instanceof Error ? err.message : "Failed to create person.";
  } finally {
    isSaving.value = false;
  }
}

function fullName(p: Person): string {
  return [p.first_name, p.last_name].filter(Boolean).join(" ") || "Unnamed";
}
</script>

<template>
  <div>
    <PageHeader title="People" />

    <div
      v-if="authStore.isAdmin && !showForm"
      class="mb-4 flex justify-end"
    >
      <BtnSave @click="openForm">
        Add person
      </BtnSave>
    </div>

    <div
      v-if="showForm"
      class="mb-4 rounded-lg border border-base-300 bg-base-100 p-4"
    >
      <div class="grid gap-3 sm:grid-cols-3">
        <AppField label="First name">
          <input
            v-model="firstName"
            class="ui-input"
            placeholder="First"
            @keydown.enter="submit"
          >
        </AppField>
        <AppField label="Last name">
          <input
            v-model="lastName"
            class="ui-input"
            placeholder="Last"
            @keydown.enter="submit"
          >
        </AppField>
        <AppField label="Email">
          <input
            v-model="email"
            type="email"
            class="ui-input"
            placeholder="optional"
            @keydown.enter="submit"
          >
        </AppField>
      </div>
      <AppNotice
        v-if="errorMessage"
        variant="error"
        class="mt-3"
      >
        {{ errorMessage }}
      </AppNotice>
      <div class="mt-3 flex justify-end gap-2">
        <BtnText @click="showForm = false">
          Cancel
        </BtnText>
        <BtnSave
          :loading="isSaving"
          @click="submit"
        >
          Save
        </BtnSave>
      </div>
    </div>

    <QueryStateWrapper
      :state="state"
      loading-label="Loading people…"
      :empty="{ eyebrow: 'No crew yet', title: 'No people recorded.', subtitle: 'Add someone to start tracking who has been aboard.' }"
    >
      <template #default="{ data }">
        <ul class="list rounded-box bg-base-100 shadow-md">
          <ListItem
            v-for="person in (data as Person[])"
            :key="person.id"
            :to="`/people/${person.id}`"
            :title="fullName(person)"
            :description="person.email || person.notes || ''"
            :meta="[]"
          />
        </ul>
      </template>
    </QueryStateWrapper>
  </div>
</template>
