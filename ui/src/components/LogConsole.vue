<script setup lang="ts">
import { ref, computed, watch, nextTick } from 'vue'
import { useWebSocket, type LogEntry } from '../composables/useWebSocket'

const { logs, connected } = useWebSocket()
const filter = ref<'ALL' | 'WARN' | 'ERROR'>('ALL')
const logContainer = ref<HTMLElement | null>(null)
const autoScroll = ref(true)

const filtered = computed(() => {
  if (filter.value === 'ALL') return logs.value
  return logs.value.filter(l => {
    if (filter.value === 'WARN') return l.level === 'WARN' || l.level === 'ERROR'
    return l.level === 'ERROR'
  })
})

function formatTs(ts: number): string {
  return new Date(ts).toTimeString().slice(0, 8)
}

function levelColor(level: string): string {
  switch (level) {
    case 'ERROR': return 'text-red-400'
    case 'WARN': return 'text-yellow-400'
    case 'DEBUG': return 'text-gray-600'
    default: return 'text-gray-500'
  }
}

watch(filtered, async () => {
  if (!autoScroll.value) return
  await nextTick()
  if (logContainer.value) {
    logContainer.value.scrollTop = logContainer.value.scrollHeight
  }
})

function onScroll() {
  if (!logContainer.value) return
  const el = logContainer.value
  autoScroll.value = el.scrollHeight - el.scrollTop - el.clientHeight < 30
}
</script>

<template>
  <div class="flex flex-col h-full">
    <div class="flex items-center justify-between mb-2">
      <div class="flex items-center gap-2">
        <span class="text-[10px] font-mono text-gray-600 uppercase tracking-wider">Log Console</span>
        <span
          class="w-1.5 h-1.5 rounded-full"
          :class="connected ? 'bg-green-500' : 'bg-red-500'"
        />
      </div>
      <div class="flex gap-1">
        <button
          v-for="f in (['ALL', 'WARN', 'ERROR'] as const)"
          :key="f"
          @click="filter = f"
          class="text-[10px] font-mono px-2 py-0.5 rounded transition-colors"
          :class="filter === f ? 'bg-accent/20 text-accent' : 'text-gray-600 hover:text-gray-400'"
        >{{ f }}</button>
      </div>
    </div>

    <div
      ref="logContainer"
      @scroll="onScroll"
      class="flex-1 overflow-y-auto bg-[#0a0a0a] border border-border rounded p-2 font-mono text-[11px] leading-relaxed min-h-[200px] max-h-[300px]"
    >
      <div v-if="filtered.length === 0" class="text-gray-700 py-4 text-center">
        {{ connected ? 'no log entries' : 'connecting...' }}
      </div>
      <div
        v-for="(entry, i) in filtered"
        :key="i"
        class="flex gap-2 whitespace-nowrap"
      >
        <span class="text-gray-700">{{ formatTs(entry.ts) }}</span>
        <span class="w-10 shrink-0" :class="levelColor(entry.level)">{{ entry.level.padEnd(5) }}</span>
        <span class="text-gray-400 truncate">{{ entry.msg }}</span>
      </div>
    </div>
  </div>
</template>
