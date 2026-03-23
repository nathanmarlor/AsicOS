<script setup lang="ts">
import { computed } from 'vue'
import type { ChipInfo } from '../stores/mining'

const props = defineProps<{
  chips: ChipInfo[]
}>()

const avgHashrate = computed(() => {
  if (props.chips.length === 0) return 0
  return props.chips.reduce((s, c) => s + c.hashrate, 0) / props.chips.length
})

const totalHashrate = computed(() => {
  return props.chips.reduce((s, c) => s + c.hashrate, 0)
})

function heatColor(hashrate: number): string {
  if (avgHashrate.value === 0) return 'border-gray-700'
  const ratio = hashrate / avgHashrate.value
  if (ratio < 0.7) return 'border-red-500 bg-red-500/5'
  if (ratio < 0.9) return 'border-orange-500 bg-orange-500/5'
  if (ratio < 1.05) return 'border-green-600 bg-green-600/5'
  return 'border-blue-500 bg-blue-500/5'
}

function perfPct(hashrate: number): string {
  if (totalHashrate.value === 0) return '0%'
  return ((hashrate / totalHashrate.value) * 100).toFixed(1) + '%'
}

const distribution = computed(() => {
  if (props.chips.length === 0) return 0
  const avg = avgHashrate.value
  if (avg === 0) return 0
  const variance = props.chips.reduce((s, c) => s + Math.pow(c.hashrate - avg, 2), 0) / props.chips.length
  const cv = Math.sqrt(variance) / avg
  return Math.max(0, Math.min(100, (1 - cv) * 100))
})
</script>

<template>
  <div>
    <div class="text-[10px] font-mono text-gray-600 uppercase tracking-wider mb-2">ASIC Hashmap</div>

    <!-- Chip grid -->
    <div class="grid gap-2" :class="chips.length <= 4 ? 'grid-cols-2' : 'grid-cols-4'">
      <div
        v-for="chip in chips"
        :key="chip.id"
        class="border rounded-lg p-3 transition-colors"
        :class="heatColor(chip.hashrate)"
      >
        <div class="flex items-center justify-between mb-1">
          <span class="text-[10px] font-mono text-gray-600">CHIP {{ chip.id }}</span>
          <span class="text-[10px] font-mono text-gray-600">{{ chip.temp.toFixed(0) }}&deg;C</span>
        </div>
        <div class="font-mono font-bold text-lg text-white">
          {{ chip.hashrate.toFixed(1) }}
        </div>
        <div class="text-[10px] font-mono text-gray-500">GH/s</div>
        <div class="text-[10px] font-mono text-gray-600 mt-1">
          {{ chip.frequency }}MHz / {{ chip.voltage }}mV
        </div>
      </div>
    </div>

    <!-- Distribution bar -->
    <div class="mt-3">
      <div class="flex items-center justify-between text-[10px] font-mono text-gray-600 mb-1">
        <span>Performance Distribution</span>
        <span>{{ distribution.toFixed(0) }}% even</span>
      </div>
      <div class="h-1.5 bg-[#1a1a1a] rounded-full overflow-hidden flex">
        <div
          v-for="chip in chips"
          :key="'bar-' + chip.id"
          class="h-full transition-all duration-500"
          :class="chip.hashrate / avgHashrate < 0.8 ? 'bg-red-500' : 'bg-accent'"
          :style="{ width: perfPct(chip.hashrate) }"
        />
      </div>
    </div>
  </div>
</template>
