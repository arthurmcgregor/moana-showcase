<script setup lang="ts">
defineProps<{
  regionSrc: string;
  detailSrc: string;
}>();
</script>

<template>
  <img
    crossorigin="use-credentials"
    loading="lazy"
    decoding="async"
    class="absolute inset-0 size-full scale-300 object-cover transition-opacity duration-300 group-hover:opacity-0"
    :src="regionSrc"
  >
  <img
    crossorigin="use-credentials"
    loading="lazy"
    decoding="async"
    class="absolute inset-0 size-full object-cover opacity-0 transition-opacity duration-300 group-hover:opacity-100"
    :src="detailSrc"
  >
</template>
