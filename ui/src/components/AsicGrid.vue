<script setup lang="ts">
import { computed } from 'vue'
import type { ChipInfo } from '../stores/mining'

const props = defineProps<{
  chips: ChipInfo[]
}>()

const avgHashrate = computed(() => {
  if (props.chips.length === 0) return 0
  return props.chips.reduce((s, c) => s + c.hashrate_ghs, 0) / props.chips.length
})

const totalHashrate = computed(() => {
  return props.chips.reduce((s, c) => s + c.hashrate_ghs, 0)
})

function heatBorder(hashrate: number): string {
  if (avgHashrate.value === 0) return 'border-[#4b4b4b]'
  const ratio = hashrate / avgHashrate.value
  if (ratio < 0.7) return 'border-[#ef4444]'
  if (ratio < 0.9) return 'border-[#f97316]'
  if (ratio < 1.05) return 'border-[#22c55e]'
  return 'border-[#3b82f6]'
}

function heatBg(hashrate: number): string {
  if (avgHashrate.value === 0) return ''
  const ratio = hashrate / avgHashrate.value
  if (ratio < 0.7) return 'bg-[#ef4444]/5'
  if (ratio < 0.9) return 'bg-[#f97316]/5'
  if (ratio < 1.05) return 'bg-[#22c55e]/5'
  return 'bg-[#3b82f6]/5'
}

function perfPct(hashrate: number): string {
  if (totalHashrate.value === 0) return '0%'
  return ((hashrate / totalHashrate.value) * 100).toFixed(1) + '%'
}

function chipPctOfTotal(hashrate: number): string {
  if (totalHashrate.value === 0) return '0'
  return ((hashrate / totalHashrate.value) * 100).toFixed(1)
}

const distribution = computed(() => {
  if (props.chips.length === 0) return 0
  const avg = avgHashrate.value
  if (avg === 0) return 0
  const variance = props.chips.reduce((s, c) => s + Math.pow(c.hashrate_ghs - avg, 2), 0) / props.chips.length
  const cv = Math.sqrt(variance) / avg
  return Math.max(0, Math.min(100, (1 - cv) * 100))
})
</script>

<template>
  <div>
    <div class="text-[10px] font-mono text-[#6b7280] uppercase tracking-wider mb-2">ASIC Hashmap</div>

    <!-- Chip grid -->
    <div class="grid gap-2" :class="chips.length <= 4 ? 'grid-cols-2' : 'grid-cols-4'">
      <div
        v-for="chip in chips"
        :key="chip.id"
        class="border rounded p-2.5 transition-colors relative group"
        :class="[heatBorder(chip.hashrate_ghs), heatBg(chip.hashrate_ghs)]"
        :title="`Chip ${chip.id}: ${chip.hashrate_ghs.toFixed(1)} GH/s (${chipPctOfTotal(chip.hashrate_ghs)}% of total)`"
      >
        <div class="flex items-center justify-between mb-0.5">
          <span class="text-[9px] font-mono text-[#4b4b4b] uppercase">Chip {{ chip.id }}</span>
          <span class="text-[9px] font-mono text-[#4b4b4b]">{{ chipPctOfTotal(chip.hashrate_ghs) }}%</span>
        </div>
        <div class="font-mono font-bold text-lg text-[#e5e5e5] leading-tight">
          {{ chip.hashrate_ghs.toFixed(1) }}
        </div>
        <div class="text-[9px] font-mono text-[#4b4b4b]">GH/s</div>
      </div>
    </div>

    <!-- Distribution bar -->
    <div class="mt-3">
      <div class="flex items-center justify-between text-[10px] font-mono text-[#6b7280] mb-1">
        <span>Performance Distribution</span>
        <span>{{ distribution.toFixed(0) }}% even</span>
      </div>
      <div class="h-1.5 bg-[#1a1a1a] rounded-sm overflow-hidden flex">
        <div
          v-for="chip in chips"
          :key="'bar-' + chip.id"
          class="h-full transition-all duration-500"
          :class="chip.hashrate_ghs / avgHashrate < 0.8 ? 'bg-[#ef4444]' : 'bg-[#f97316]'"
          :style="{ width: perfPct(chip.hashrate_ghs) }"
        />
      </div>
    </div>
  </div>
</template>
