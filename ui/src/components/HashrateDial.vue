<script setup lang="ts">
import { computed, ref, watch } from 'vue'

const props = defineProps<{
  hashrate: number
  isMining: boolean
}>()

const displayValue = ref('0.0')
const isUpdating = ref(false)

watch(() => props.hashrate, (newVal) => {
  isUpdating.value = true
  displayValue.value = newVal >= 1000
    ? newVal.toFixed(1)
    : newVal >= 100
      ? newVal.toFixed(1)
      : newVal >= 10
        ? newVal.toFixed(2)
        : newVal.toFixed(2)
  setTimeout(() => { isUpdating.value = false }, 300)
}, { immediate: true })
</script>

<template>
  <div class="text-center">
    <div class="relative inline-block">
      <div
        class="font-mono font-bold text-5xl sm:text-6xl text-white tracking-tight transition-transform duration-300"
        :class="{ 'scale-[1.02]': isUpdating }"
      >
        {{ displayValue }}
      </div>
      <div class="text-sm font-mono text-gray-500 mt-1 tracking-widest">GH/s</div>
      <!-- Accent underline -->
      <div class="mt-2 mx-auto h-[2px] w-16 bg-accent rounded-full" :class="{ 'animate-pulse-subtle': isMining }" />
    </div>
    <div class="flex items-center justify-center gap-1.5 mt-3">
      <span
        class="w-1.5 h-1.5 rounded-full"
        :class="isMining ? 'bg-green-500 animate-pulse-subtle' : 'bg-gray-600'"
      />
      <span class="text-[11px] font-mono" :class="isMining ? 'text-green-500' : 'text-gray-600'">
        {{ isMining ? 'mining' : 'idle' }}
      </span>
    </div>
  </div>
</template>
