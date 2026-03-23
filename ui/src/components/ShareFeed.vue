<script setup lang="ts">
import { ref, computed } from 'vue'
import type { ShareEntry } from '../stores/mining'

const props = defineProps<{
  allShares: ShareEntry[]
  submittedShares: ShareEntry[]
  bestDiff: number
}>()

const filter = ref<'all' | 'submitted'>('all')

const visible = computed(() => {
  const src = filter.value === 'all' ? props.allShares : props.submittedShares
  return src.slice(0, 30)
})

const maxDiff = computed(() => {
  if (visible.value.length === 0) return 1
  return Math.max(...visible.value.map(s => s.diff), 1)
})

function formatTime(ts: number): string {
  const d = new Date(ts)
  return d.toTimeString().slice(0, 8)
}

function barWidth(diff: number): string {
  const pct = Math.max(3, (Math.log(diff) / Math.log(maxDiff.value)) * 100)
  return Math.min(100, pct) + '%'
}
</script>

<template>
  <div class="flex flex-col h-full min-h-0">
    <!-- Header with toggle -->
    <div class="flex items-center justify-between mb-2 shrink-0">
      <div class="text-[10px] font-mono text-[#6b7280] uppercase tracking-wider">Shares</div>
      <div class="flex gap-0.5 bg-[#0a0a0a] rounded p-0.5">
        <button
          @click="filter = 'all'"
          class="text-[10px] font-mono px-2 py-0.5 rounded transition-colors min-h-[28px]"
          :class="filter === 'all' ? 'bg-[#1a1a1a] text-[#f97316]' : 'text-[#6b7280] hover:text-[#e5e5e5]'"
        >All</button>
        <button
          @click="filter = 'submitted'"
          class="text-[10px] font-mono px-2 py-0.5 rounded transition-colors min-h-[28px]"
          :class="filter === 'submitted' ? 'bg-[#1a1a1a] text-[#f97316]' : 'text-[#6b7280] hover:text-[#e5e5e5]'"
        >Submitted</button>
      </div>
    </div>

    <!-- Share list -->
    <div class="flex-1 overflow-y-auto space-y-0.5 min-h-0">
      <div
        v-if="visible.length === 0"
        class="text-xs text-[#4b4b4b] font-mono py-4 text-center"
      >
        waiting for shares...
      </div>
      <div
        v-for="(share, i) in visible"
        :key="share.ts + '-' + i"
        class="flex items-center gap-2 py-1 px-1 text-xs font-mono rounded"
        :class="[
          i === 0 ? 'animate-slide-in' : '',
          share.submitted ? 'bg-[#f97316]/5' : (i % 2 === 0 ? 'bg-transparent' : 'bg-[#111111]/50')
        ]"
      >
        <span class="text-[#4b4b4b] w-[60px] shrink-0 text-[11px]">{{ formatTime(share.ts) }}</span>
        <span class="text-[#6b7280] w-[48px] text-right shrink-0">{{ share.diff_str }}</span>
        <div class="flex-1 h-[6px] bg-[#1a1a1a] rounded-sm overflow-hidden">
          <div
            class="h-full rounded-sm transition-all duration-300"
            :class="share.submitted ? 'bg-[#f97316]' : 'bg-[#22c55e]/60'"
            :style="{ width: barWidth(share.diff) }"
          />
        </div>
        <span class="w-4 text-center shrink-0">
          <span v-if="share.submitted" class="text-[#f97316]" title="Submitted to pool">&#9733;</span>
          <span v-else class="text-[#22c55e]/60" title="Accepted">&#10003;</span>
        </span>
      </div>
    </div>
  </div>
</template>
