<script setup lang="ts">
import { computed } from 'vue'
import { useRoute, useRouter } from 'vue-router'

const route = useRoute()
const router = useRouter()

const isAdvanced = computed(() => route.path === '/advanced')

function toggle() {
  router.push(isAdvanced.value ? '/simple' : '/advanced')
}
</script>

<template>
  <button
    @click="toggle"
    class="flex items-center h-8 rounded-full bg-[var(--surface-light)] border border-border text-[11px] font-mono overflow-hidden transition-colors"
  >
    <span
      class="px-2 py-0.5 transition-colors"
      :class="!isAdvanced ? 'text-accent bg-accent/10' : 'text-[var(--text-secondary)] hover:text-[var(--text)]'"
    >simple</span>
    <span
      class="px-2 py-0.5 transition-colors"
      :class="isAdvanced ? 'text-accent bg-accent/10' : 'text-[var(--text-secondary)] hover:text-[var(--text)]'"
    >advanced</span>
  </button>
</template>
