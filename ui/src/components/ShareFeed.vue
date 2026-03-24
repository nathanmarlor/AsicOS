<script setup lang="ts">
import { computed } from 'vue'
import type { ShareEntry } from '../stores/mining'

const props = defineProps<{
  shares: ShareEntry[]
  poolDiff: number
}>()

const visible = computed(() => [...props.shares].sort((a, b) => b.ts - a.ts).slice(0, 30))

function formatTime(ts: number): string {
  const d = new Date(ts)
  return d.toTimeString().slice(0, 8)
}

function barWidth(diff: number): string {
  const LOG_MAX = 14  /* log10(100T) ≈ 14 */
  const logVal = Math.log10(Math.max(diff, 1))
  const pct = Math.max(2, (logVal / LOG_MAX) * 100)
  return Math.min(100, pct) + '%'
}
</script>

<template>
  <div class="flex flex-col h-full min-h-0">
    <div class="text-[10px] font-mono text-[var(--text-secondary)] uppercase tracking-wider mb-2 shrink-0">Submitted Shares</div>

    <div class="flex-1 overflow-y-auto space-y-0.5 min-h-0 max-h-[220px]">
      <div
        v-if="visible.length === 0"
        class="text-xs text-[var(--text-muted)] font-mono py-4 text-center"
      >
        waiting for shares...
      </div>
      <div
        v-for="(share, i) in visible"
        :key="share.ts + '-' + i"
        class="flex items-center gap-2 py-1 px-1 text-xs font-mono rounded"
        :class="i === 0 ? 'animate-slide-in bg-[#f97316]/5' : (i % 2 === 0 ? 'bg-transparent' : 'bg-[var(--surface)]/50')"
      >
        <span class="text-[var(--text-muted)] w-[60px] shrink-0 text-[11px]">{{ formatTime(share.ts) }}</span>
        <span class="text-[var(--text-secondary)] w-[48px] text-right shrink-0">{{ share.diff_str }}</span>
        <div class="flex-1 h-[6px] bg-[var(--surface-light)] rounded-sm overflow-hidden">
          <div
            class="h-full rounded-sm transition-all duration-300 bg-[#f97316]"
            :style="{ width: barWidth(share.diff) }"
          />
        </div>
        <span class="w-4 text-center shrink-0 text-[#f97316]" title="Submitted to pool">&#9733;</span>
      </div>
    </div>
  </div>
</template>
