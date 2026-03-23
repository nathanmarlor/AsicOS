<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{
  label: string
  value: number
  min?: number
  max?: number
}>()

const minVal = computed(() => props.min ?? 20)
const maxVal = computed(() => props.max ?? 120)

const pct = computed(() => {
  const range = maxVal.value - minVal.value
  return Math.min(100, Math.max(0, ((props.value - minVal.value) / range) * 100))
})

const barColor = computed(() => {
  if (props.value < 60) return 'bg-green-500'
  if (props.value < 80) return 'bg-yellow-500'
  if (props.value < 95) return 'bg-orange-500'
  return 'bg-red-500'
})
</script>

<template>
  <div class="flex items-center gap-3 text-xs">
    <span class="text-gray-500 font-mono w-20 shrink-0">{{ label }}</span>
    <div class="flex-1 h-1.5 bg-[var(--surface-light)] rounded-full overflow-hidden">
      <div
        class="h-full rounded-full transition-all duration-500"
        :class="barColor"
        :style="{ width: pct + '%' }"
      />
    </div>
    <span class="font-mono text-gray-300 w-12 text-right">{{ value.toFixed(0) }}&deg;C</span>
  </div>
</template>
