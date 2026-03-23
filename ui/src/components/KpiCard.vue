<script setup lang="ts">
import { computed } from 'vue'
import Sparkline from './Sparkline.vue'

const props = withDefaults(defineProps<{
  label: string
  value: string
  unit?: string
  history?: number[]
  status?: 'good' | 'warn' | 'danger' | 'neutral'
  trend?: number
  sparkColor?: string
}>(), {
  status: 'neutral',
})

const borderColor = computed(() => {
  switch (props.status) {
    case 'good': return 'border-l-[#22c55e]'
    case 'warn': return 'border-l-[#eab308]'
    case 'danger': return 'border-l-[#ef4444]'
    default: return 'border-l-[#4b4b4b]'
  }
})

const trendText = computed(() => {
  if (props.trend == null) return ''
  const sign = props.trend >= 0 ? '+' : ''
  return `${sign}${props.trend.toFixed(1)}%`
})

const trendArrow = computed(() => {
  if (props.trend == null) return ''
  return props.trend >= 0 ? '\u25B2' : '\u25BC'
})

const trendColor = computed(() => {
  if (props.trend == null) return ''
  return props.trend >= 0 ? 'text-[#22c55e]' : 'text-[#ef4444]'
})

const resolvedSparkColor = computed(() => {
  if (props.sparkColor) return props.sparkColor
  switch (props.status) {
    case 'good': return '#22c55e'
    case 'warn': return '#eab308'
    case 'danger': return '#ef4444'
    default: return '#f97316'
  }
})
</script>

<template>
  <div
    class="bg-[#111111] border border-[#1e1e1e] border-l-[3px] rounded p-3 flex flex-col gap-1 min-w-0"
    :class="borderColor"
  >
    <!-- Value row -->
    <div class="flex items-baseline justify-between gap-2 min-w-0">
      <span class="font-mono font-bold text-xl text-[#e5e5e5] truncate leading-tight">{{ value }}</span>
      <span
        v-if="trend != null"
        class="text-[10px] font-mono shrink-0 leading-tight"
        :class="trendColor"
      >{{ trendArrow }} {{ trendText }}</span>
    </div>

    <!-- Unit -->
    <div v-if="unit" class="text-[11px] font-mono text-[#6b7280] leading-tight">{{ unit }}</div>

    <!-- Sparkline -->
    <div v-if="history && history.length > 1" class="mt-1">
      <Sparkline :data="history" :color="resolvedSparkColor" :width="120" :height="20" />
    </div>

    <!-- Label -->
    <div class="text-[10px] font-mono text-[#6b7280] uppercase tracking-wider leading-tight mt-0.5">{{ label }}</div>
  </div>
</template>
