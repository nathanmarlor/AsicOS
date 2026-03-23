<script setup lang="ts">
import { computed } from 'vue'
import Sparkline from './Sparkline.vue'

const props = defineProps<{
  accepted: number
  rejected: number
  bestDiff: string
  poolDiff: string
  duplicates: number
  shareRate: number
  shareRateHistory?: number[]
}>()

const total = computed(() => props.accepted + props.rejected)
const acceptPct = computed(() => total.value === 0 ? 100 : (props.accepted / total.value) * 100)
const rejectPct = computed(() => total.value === 0 ? 0 : (props.rejected / total.value) * 100)
</script>

<template>
  <div class="flex flex-col gap-3">
    <div class="text-[10px] font-mono text-[var(--text-secondary)] uppercase tracking-wider">Share Performance</div>

    <!-- Accepted / Rejected bars -->
    <div class="space-y-2">
      <div class="flex items-center gap-2">
        <span class="text-[11px] font-mono text-[var(--text-secondary)] w-[68px] shrink-0">Accepted</span>
        <span class="text-[11px] font-mono text-[var(--text)] w-[56px] text-right shrink-0">{{ accepted.toLocaleString() }}</span>
        <div class="flex-1 h-[8px] bg-[var(--surface-light)] rounded-sm overflow-hidden">
          <div
            class="h-full bg-[#22c55e] rounded-sm transition-all duration-500"
            :style="{ width: acceptPct + '%' }"
          />
        </div>
      </div>
      <div class="flex items-center gap-2">
        <span class="text-[11px] font-mono text-[var(--text-secondary)] w-[68px] shrink-0">Rejected</span>
        <span class="text-[11px] font-mono text-[var(--text)] w-[56px] text-right shrink-0">{{ rejected.toLocaleString() }}</span>
        <div class="flex-1 h-[8px] bg-[var(--surface-light)] rounded-sm overflow-hidden">
          <div
            class="h-full bg-[#ef4444] rounded-sm transition-all duration-500"
            :style="{ width: rejectPct + '%' }"
          />
        </div>
      </div>
      <div class="text-right">
        <span class="font-mono font-bold text-lg text-[var(--text)]">{{ acceptPct.toFixed(1) }}%</span>
      </div>
    </div>

    <!-- Stats grid -->
    <div class="grid grid-cols-2 gap-x-4 gap-y-1.5 text-[11px] font-mono">
      <div class="flex items-center justify-between">
        <span class="text-[var(--text-secondary)]">Share Rate</span>
        <div class="flex items-center gap-1.5">
          <span class="text-[var(--text)]">{{ shareRate.toFixed(1) }} /min</span>
        </div>
      </div>
      <div class="flex items-center justify-between">
        <span class="text-[var(--text-secondary)]">Best Diff</span>
        <span class="text-[#f97316]">{{ bestDiff }}</span>
      </div>
      <div class="flex items-center justify-between">
        <span class="text-[var(--text-secondary)]">Pool Diff</span>
        <span class="text-[var(--text)]">{{ poolDiff }}</span>
      </div>
      <div class="flex items-center justify-between">
        <span class="text-[var(--text-secondary)]">Duplicates</span>
        <span :class="duplicates > 0 ? 'text-[#eab308]' : 'text-[var(--text)]'">{{ duplicates }}</span>
      </div>
    </div>

    <!-- Share rate sparkline -->
    <div v-if="shareRateHistory && shareRateHistory.length > 1">
      <Sparkline :data="shareRateHistory" color="#3b82f6" :width="200" :height="20" />
    </div>
  </div>
</template>
