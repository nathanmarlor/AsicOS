<script setup lang="ts">
import { ref, computed, watch, nextTick } from 'vue'
import { useWebSocket, type LogEntry } from '../composables/useWebSocket'

const { logs, connected } = useWebSocket()
const filter = ref<'ALL' | 'INFO' | 'WARN' | 'ERROR'>('ALL')
const logContainer = ref<HTMLElement | null>(null)
const autoScroll = ref(true)
const collapsed = ref(false)

const filtered = computed(() => {
  if (filter.value === 'ALL') return logs.value
  return logs.value.filter(l => {
    if (filter.value === 'INFO') return true
    if (filter.value === 'WARN') return l.level === 'WARN' || l.level === 'ERROR'
    return l.level === 'ERROR'
  })
})

const lastLine = computed(() => {
  if (logs.value.length === 0) return 'no log entries'
  const e = logs.value[logs.value.length - 1]
  return `${formatTs(e.ts)} ${e.level} ${e.msg}`
})

function formatTs(ts: number): string {
  return new Date(ts).toTimeString().slice(0, 8)
}

function levelColor(level: string): string {
  switch (level) {
    case 'ERROR': return 'text-[#ef4444]'
    case 'WARN': return 'text-[#eab308]'
    case 'DEBUG': return 'text-[var(--text-muted)]'
    default: return 'text-[var(--text-secondary)]'
  }
}

watch(filtered, async () => {
  if (!autoScroll.value || collapsed.value) return
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
  <div class="flex flex-col h-full min-h-0">
    <!-- Clickable header -->
    <button
      @click="collapsed = !collapsed"
      class="flex items-center justify-between mb-2 shrink-0 w-full text-left min-h-[32px] group"
    >
      <div class="flex items-center gap-2">
        <span class="text-[10px] font-mono text-[var(--text-secondary)] uppercase tracking-wider">Log Console</span>
        <span
          class="w-1.5 h-1.5 rounded-full"
          :class="connected ? 'bg-[#22c55e]' : 'bg-[#ef4444]'"
        />
        <span class="text-[10px] font-mono text-[var(--text-muted)] transition-transform" :class="collapsed ? '' : 'rotate-90'">&#9654;</span>
      </div>
      <div v-if="!collapsed" class="flex gap-0.5">
        <button
          v-for="f in (['ALL', 'INFO', 'WARN', 'ERROR'] as const)"
          :key="f"
          @click.stop="filter = f"
          class="text-[10px] font-mono px-2 py-0.5 rounded transition-colors min-h-[24px]"
          :class="filter === f ? 'bg-[#f97316]/20 text-[#f97316]' : 'text-[var(--text-muted)] hover:text-[var(--text-secondary)]'"
        >{{ f }}</button>
      </div>
    </button>

    <!-- Collapsed preview -->
    <div
      v-if="collapsed"
      class="text-[11px] font-mono text-[var(--text-muted)] truncate px-2 py-1 bg-[var(--bg)] border border-[var(--border)] rounded"
    >
      {{ lastLine }}
    </div>

    <!-- Expanded log -->
    <div
      v-else
      ref="logContainer"
      @scroll="onScroll"
      class="flex-1 overflow-y-auto bg-[var(--bg)] border border-[var(--border)] rounded p-2 font-mono text-[11px] leading-relaxed min-h-[120px] max-h-[300px]"
    >
      <div v-if="filtered.length === 0" class="text-[var(--text-muted)] py-4 text-center">
        {{ connected ? 'no log entries' : 'connecting...' }}
      </div>
      <div
        v-for="(entry, i) in filtered"
        :key="i"
        class="flex gap-2 whitespace-nowrap py-px"
        :class="i % 2 === 0 ? '' : 'bg-[var(--surface)]/30'"
      >
        <span class="text-[var(--text-muted)]">{{ formatTs(entry.ts) }}</span>
        <span class="w-10 shrink-0" :class="levelColor(entry.level)">{{ entry.level.padEnd(5) }}</span>
        <span class="text-[var(--text-secondary)] truncate">{{ entry.msg }}</span>
      </div>
    </div>
  </div>
</template>
