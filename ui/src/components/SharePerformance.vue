<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{
  accepted: number
  rejected: number
  bestDiff: string
  alltimeBestDiff?: string
  poolDiff: string
  poolUrl?: string
  poolConnected?: boolean
  duplicates: number
  shareRate: number
  rttMs?: number
  blockHeight?: number
  blocksFound?: number
}>()

const total = computed(() => props.accepted + props.rejected)
const acceptPct = computed(() => total.value === 0 ? 100 : (props.accepted / total.value) * 100)
</script>

<template>
  <div class="flex flex-col gap-2.5">
    <div class="text-[10px] font-mono text-[var(--text-secondary)] uppercase tracking-wider">Pool & Shares</div>

    <!-- Pool connection -->
    <div v-if="poolUrl" class="flex items-center gap-2 text-[11px] font-mono">
      <span class="w-2 h-2 rounded-full shrink-0" :class="poolConnected ? 'bg-[#22c55e]' : 'bg-[#ef4444]'" />
      <span class="text-[var(--text-muted)] truncate">{{ poolUrl }}</span>
    </div>

    <!-- Accept/Reject ratio bar -->
    <div>
      <div class="flex items-center justify-between text-[11px] font-mono mb-1">
        <span class="text-[#22c55e]">{{ accepted.toLocaleString() }} accepted</span>
        <span class="text-[var(--text)]">{{ acceptPct.toFixed(1) }}%</span>
        <span :class="rejected > 0 ? 'text-[#ef4444]' : 'text-[var(--text-muted)]'">{{ rejected }} rejected</span>
      </div>
      <div class="h-[6px] bg-[var(--surface-light)] rounded-sm overflow-hidden flex">
        <div class="h-full bg-[#22c55e] transition-all duration-500" :style="{ width: acceptPct + '%' }" />
        <div v-if="rejected > 0" class="h-full bg-[#ef4444] transition-all duration-500" :style="{ width: (100 - acceptPct) + '%' }" />
      </div>
    </div>

    <!-- Key stats in clean rows -->
    <div class="grid grid-cols-2 gap-x-4 gap-y-1 text-[11px] font-mono">
      <div class="text-[var(--text-muted)]">Pool Diff</div>
      <div class="text-right text-[var(--text)]">{{ poolDiff }}</div>
      <div class="text-[var(--text-muted)]">Session Best</div>
      <div class="text-right text-[#f97316]">{{ bestDiff }}</div>
      <div class="text-[var(--text-muted)]">All-Time Best</div>
      <div class="text-right text-[#eab308]">{{ alltimeBestDiff ?? '--' }}</div>
      <div class="text-[var(--text-muted)]">Share Rate</div>
      <div class="text-right text-[var(--text)]">{{ shareRate.toFixed(1) }} /min</div>
      <template v-if="rttMs != null && rttMs > 0">
        <div class="text-[var(--text-muted)]">Pool RTT</div>
        <div class="text-right text-[var(--text)]">{{ rttMs.toFixed(0) }} ms</div>
      </template>
      <template v-if="blockHeight != null && blockHeight > 0">
        <div class="text-[var(--text-muted)]">Block Height</div>
        <div class="text-right text-[var(--text)]">{{ blockHeight.toLocaleString() }}</div>
      </template>
      <template v-if="blocksFound != null && blocksFound > 0">
        <div class="text-[var(--text-muted)]">New Blocks</div>
        <div class="text-right text-[var(--text)]">{{ blocksFound }}</div>
      </template>
    </div>
  </div>
</template>
