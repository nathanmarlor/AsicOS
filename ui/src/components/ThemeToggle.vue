<script setup lang="ts">
import { ref, onMounted, watch } from 'vue'

const isDark = ref(true)

onMounted(() => {
  const saved = localStorage.getItem('asicos-theme')
  isDark.value = saved ? saved === 'dark' : true
  applyTheme()
})

watch(isDark, () => {
  localStorage.setItem('asicos-theme', isDark.value ? 'dark' : 'light')
  applyTheme()
})

function applyTheme() {
  document.documentElement.classList.toggle('dark', isDark.value)
  document.documentElement.classList.toggle('light', !isDark.value)
}

function toggle() {
  isDark.value = !isDark.value
}
</script>

<template>
  <button @click="toggle" class="p-1.5 rounded hover:bg-surface-light transition-colors min-w-[44px] min-h-[44px]" title="Toggle theme">
    <!-- Sun icon (shown in dark mode) -->
    <svg v-if="isDark" class="w-4 h-4 text-[var(--text-secondary)]" viewBox="0 0 20 20" fill="currentColor">
      <path fill-rule="evenodd" d="M10 2a1 1 0 011 1v1a1 1 0 11-2 0V3a1 1 0 011-1zm4 8a4 4 0 11-8 0 4 4 0 018 0zm-.464 4.95l.707.707a1 1 0 001.414-1.414l-.707-.707a1 1 0 00-1.414 1.414zm2.12-10.607a1 1 0 010 1.414l-.706.707a1 1 0 11-1.414-1.414l.707-.707a1 1 0 011.414 0zM17 11a1 1 0 100-2h-1a1 1 0 100 2h1zm-7 4a1 1 0 011 1v1a1 1 0 11-2 0v-1a1 1 0 011-1zM5.05 6.464A1 1 0 106.465 5.05l-.708-.707a1 1 0 00-1.414 1.414l.707.707zm1.414 8.486l-.707.707a1 1 0 01-1.414-1.414l.707-.707a1 1 0 011.414 1.414zM4 11a1 1 0 100-2H3a1 1 0 000 2h1z" clip-rule="evenodd"/>
    </svg>
    <!-- Moon icon (shown in light mode) -->
    <svg v-else class="w-4 h-4 text-[var(--text-secondary)]" viewBox="0 0 20 20" fill="currentColor">
      <path d="M17.293 13.293A8 8 0 016.707 2.707a8.001 8.001 0 1010.586 10.586z"/>
    </svg>
  </button>
</template>
