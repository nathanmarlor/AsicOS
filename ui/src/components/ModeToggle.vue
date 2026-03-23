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
    class="flex items-center h-6 rounded-full bg-[#1a1a1a] border border-border text-[11px] font-mono overflow-hidden transition-colors"
  >
    <span
      class="px-2 py-0.5 transition-colors"
      :class="!isAdvanced ? 'text-accent bg-accent/10' : 'text-gray-500 hover:text-gray-400'"
    >simple</span>
    <span
      class="px-2 py-0.5 transition-colors"
      :class="isAdvanced ? 'text-accent bg-accent/10' : 'text-gray-500 hover:text-gray-400'"
    >advanced</span>
  </button>
</template>
