<script setup lang="ts">
import { computed } from 'vue'
import type { ShareEntry } from '../stores/mining'

const props = defineProps<{
  shares: ShareEntry[]
  bestDiff: number
}>()

const visible = computed(() => props.shares.slice(0, 20))

const maxDiff = computed(() => {
  if (visible.value.length === 0) return 1
  return Math.max(...visible.value.map(s => s.diff), 1)
})

function formatTime(ts: number): string {
  const d = new Date(ts)
  return d.toTimeString().slice(0, 8)
}

function barWidth(diff: number): string {
  const pct = Math.max(5, (diff / maxDiff.value) * 100)
  return pct + '%'
}
</script>

<template>
  <div class="space-y-0.5 overflow-hidden">
    <div class="text-[10px] font-mono text-gray-600 uppercase tracking-wider mb-2">Live Shares</div>
    <div
      v-if="visible.length === 0"
      class="text-xs text-gray-600 font-mono py-4 text-center"
    >
      waiting for shares...
    </div>
    <div
      v-for="(share, i) in visible"
      :key="share.ts + '-' + i"
      class="flex items-center gap-2 py-1 text-xs font-mono"
      :class="{ 'animate-slide-in': i === 0 }"
    >
      <span class="text-gray-600 w-16 shrink-0">{{ formatTime(share.ts) }}</span>
      <span class="text-gray-400 w-12 text-right shrink-0">{{ share.diff_str }}</span>
      <div class="flex-1 h-1 bg-[#1a1a1a] rounded-full overflow-hidden">
        <div
          class="h-full rounded-full transition-all duration-300"
          :class="share.accepted ? 'bg-accent' : 'bg-red-500'"
          :style="{ width: barWidth(share.diff) }"
        />
      </div>
    </div>
  </div>
</template>
