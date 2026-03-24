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

const hasDomains = computed(() => {
  return props.chips.some(c => c.domains && c.domains.length > 0)
})

function heatBorder(hashrate: number): string {
  if (avgHashrate.value === 0) return 'border-[var(--text-muted)]'
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

const totalNonces = computed(() => {
  return props.chips.reduce((s, c) => s + (c.nonces ?? 0), 0)
})

function noncePct(chip: ChipInfo): string {
  if (totalNonces.value === 0) return '0%'
  return (((chip.nonces ?? 0) / totalNonces.value) * 100).toFixed(1) + '%'
}

function formatNonces(n: number): string {
  return n.toLocaleString()
}

const distribution = computed(() => {
  if (props.chips.length === 0) return 0
  const nonces = props.chips.map(c => c.nonces ?? 0)
  const total = nonces.reduce((s, n) => s + n, 0)
  if (total === 0) {
    // Fall back to hashrate-based distribution
    const avg = avgHashrate.value
    if (avg === 0) return 0
    const variance = props.chips.reduce((s, c) => s + Math.pow(c.hashrate_ghs - avg, 2), 0) / props.chips.length
    const cv = Math.sqrt(variance) / avg
    return Math.max(0, Math.min(100, (1 - cv) * 100))
  }
  const avg = total / props.chips.length
  const variance = nonces.reduce((s, n) => s + Math.pow(n - avg, 2), 0) / props.chips.length
  const cv = Math.sqrt(variance) / avg
  return Math.max(0, Math.min(100, (1 - cv) * 100))
})
</script>

<template>
  <div>
    <div class="text-[10px] font-mono text-[var(--text-secondary)] uppercase tracking-wider mb-2">ASIC Hashmap</div>

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
          <span class="text-[9px] font-mono text-[var(--text-muted)] uppercase">Chip {{ chip.id }}</span>
          <span class="text-[9px] font-mono text-[var(--text-muted)]">{{ chipPctOfTotal(chip.hashrate_ghs) }}%</span>
        </div>

        <!-- Domain-level hashrate -->
        <template v-if="hasDomains && chip.domains && chip.domains.length > 0">
          <div
            v-for="(dhr, di) in chip.domains"
            :key="'d' + di"
            class="flex items-center justify-between text-[10px] font-mono leading-snug"
          >
            <span class="text-[var(--text-muted)]">D{{ di }}:</span>
            <span class="text-[var(--text)]">{{ dhr.toFixed(0) }} <span class="text-[var(--text-muted)]">GH/s</span></span>
          </div>
          <div class="flex items-center justify-between mt-0.5 pt-0.5 border-t border-[var(--border)]/50">
            <span class="text-[10px] font-mono font-medium text-[var(--text-secondary)]">Total:</span>
            <span class="font-mono font-bold text-sm text-[var(--text)]">{{ chip.hashrate_ghs.toFixed(0) }} <span class="text-[9px] text-[var(--text-muted)]">GH/s</span></span>
          </div>
          <div v-if="(chip.hw_errors ?? 0) > 0" class="flex items-center justify-between text-[9px] font-mono mt-0.5">
            <span class="text-[#ef4444]">HW Errors:</span>
            <span class="text-[#ef4444]">{{ (chip.hw_errors ?? 0).toLocaleString() }}</span>
          </div>
        </template>

        <!-- Fallback: chip-level only (no domain data) -->
        <template v-else>
          <div class="font-mono font-bold text-lg text-[var(--text)] leading-tight">
            {{ chip.hashrate_ghs.toFixed(1) }}
          </div>
          <div class="text-[9px] font-mono text-[var(--text-muted)]">GH/s</div>
        </template>
      </div>
    </div>

    <!-- Distribution bar -->
    <div class="mt-3">
      <div class="flex items-center justify-between text-[10px] font-mono text-[var(--text-secondary)] mb-1">
        <span>Nonce Distribution</span>
        <span>{{ distribution.toFixed(0) }}% even</span>
      </div>
      <div class="h-1.5 bg-[var(--surface-light)] rounded-sm overflow-hidden flex">
        <div
          v-for="chip in chips"
          :key="'bar-' + chip.id"
          class="h-full transition-all duration-500"
          :class="totalNonces > 0 && (chip.nonces ?? 0) / (totalNonces / chips.length) < 0.8 ? 'bg-[#ef4444]' : 'bg-[#f97316]'"
          :style="{ width: totalNonces > 0 ? noncePct(chip) : perfPct(chip.hashrate_ghs) }"
        />
      </div>
      <div v-if="totalNonces > 0" class="flex flex-wrap gap-x-3 gap-y-0.5 mt-1 text-[9px] font-mono text-[var(--text-muted)]">
        <span v-for="chip in chips" :key="'nonce-' + chip.id">
          Chip {{ chip.id }}: {{ formatNonces(chip.nonces ?? 0) }} nonces
        </span>
      </div>
    </div>
  </div>
</template>
