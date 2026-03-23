<script setup lang="ts">
import { computed } from 'vue'
import { useSystemStore } from '../stores/system'
import { useMiningStore } from '../stores/mining'
import Sparkline from '../components/Sparkline.vue'
import ShareFeed from '../components/ShareFeed.vue'

const system = useSystemStore()
const mining = useMiningStore()

const hashrate = computed(() => mining.info?.hashrate_ghs ?? 0)
const isMining = computed(() => system.info?.pool.state === 'mining')
const accepted = computed(() => mining.info?.accepted ?? 0)
const bestDiff = computed(() => mining.info?.best_diff ?? 0)
const bestDiffStr = computed(() => mining.formatDiff(bestDiff.value))
const alltimeDiffStr = computed(() => mining.formatDiff(mining.info?.alltime_best_diff ?? 0))
const chipTemp = computed(() => system.info?.temps.chip ?? 0)
const poolUrl = computed(() => {
  if (!system.info) return '--'
  return `${system.info.config.pool_url}:${system.info.config.pool_port}`
})
const poolConnected = computed(() => system.info?.pool.state === 'mining')
const uptime = computed(() => {
  const ms = system.info?.uptime_ms ?? 0
  const s = Math.floor(ms / 1000)
  const d = Math.floor(s / 86400)
  const h = Math.floor((s % 86400) / 3600)
  const m = Math.floor((s % 3600) / 60)
  return d > 0 ? `${d}d ${h}h ${m}m` : `${h}h ${m}m`
})
const power = computed(() => system.info?.power.watts ?? 0)
const expectedGhs = computed(() => {
  if (!system.info) return 0
  return (system.info.config.frequency * 2040 * system.info.expected_chips) / 1000
})

// Health overview
const vrmTemp = computed(() => system.info?.temps.vr ?? 0)
const fanRpm = computed(() => system.info?.power.fan0_rpm ?? 0)
const efficiency = computed(() => {
  const p = power.value
  const h = hashrate.value
  if (p === 0 || h === 0) return 0
  return p / (h / 1000)  // J/TH = watts / (GH/s / 1000)
})
const efficiencyDisplay = computed(() => {
  return efficiency.value > 0 ? efficiency.value.toFixed(1) : '--'
})
const efficiencyColor = computed(() => {
  const e = efficiency.value
  if (e === 0) return 'var(--text)'
  if (e < 20) return '#22c55e'
  if (e <= 30) return '#eab308'
  return '#ef4444'
})
const rejectRate = computed(() => {
  const a = mining.info?.accepted ?? 0
  const r = mining.info?.rejected ?? 0
  const total = a + r
  return total > 0 ? (r / total) * 100 : 0
})

const tempColor = computed(() => {
  const t = chipTemp.value
  if (t < 60) return '#22c55e'
  if (t < 75) return '#eab308'
  return '#ef4444'
})

const vrmColor = computed(() => {
  const t = vrmTemp.value
  if (t < 70) return '#22c55e'
  if (t < 90) return '#eab308'
  return '#ef4444'
})

const rejectColor = computed(() => {
  const r = rejectRate.value
  if (r < 1) return '#22c55e'
  if (r < 3) return '#eab308'
  return '#ef4444'
})

const overallHealthColor = computed(() => {
  const asicOk = chipTemp.value < 75
  const vrmOk = vrmTemp.value < 90
  const rejectOk = rejectRate.value < 3
  const connected = poolConnected.value
  if (asicOk && vrmOk && rejectOk && connected && chipTemp.value < 60) return '#22c55e'
  if (!connected || !asicOk || !vrmOk || !rejectOk) return '#ef4444'
  return '#eab308'
})

const overallHealthLabel = computed(() => {
  const c = overallHealthColor.value
  if (c === '#22c55e') return 'All systems normal'
  if (c === '#ef4444') return 'Attention required'
  return 'Elevated temps'
})

// Time to solo block estimate
const timeToBlock = computed(() => {
  const hr = hashrate.value * 1e9 // convert GH/s to H/s
  if (hr <= 0) return '--'
  // Current network difficulty ~80T (approximate)
  const networkDiff = 80e12
  // Expected time = diff * 2^32 / hashrate (in seconds)
  const seconds = (networkDiff * Math.pow(2, 32)) / hr
  const years = seconds / (365.25 * 24 * 3600)
  if (years >= 1e6) return `~${(years / 1e6).toFixed(1)}M years`
  if (years >= 1e3) return `~${(years / 1e3).toFixed(0)}K years`
  return `~${years.toFixed(0)} years`
})
</script>

<template>
  <div class="max-w-lg mx-auto px-4 py-6 space-y-5">

    <!-- Big hashrate -->
    <div class="text-center">
      <div class="relative inline-block">
        <div class="font-mono font-bold text-6xl sm:text-7xl text-[var(--text)] tracking-tight">
          {{ hashrate.toFixed(1) }}
        </div>
        <div class="text-sm font-mono text-[var(--text-secondary)] mt-1 tracking-widest">GH/s</div>
        <div v-if="expectedGhs > 0" class="text-[11px] font-mono text-[var(--text-muted)] mt-0.5">expected: {{ expectedGhs.toLocaleString() }}</div>
        <!-- Accent underline -->
        <div class="mt-2 mx-auto h-[2px] w-16 bg-[#f97316] rounded-full" :class="{ 'animate-pulse-subtle': isMining }" />
      </div>
      <div class="flex items-center justify-center gap-1.5 mt-3">
        <span
          class="w-2 h-2 rounded-full"
          :class="isMining ? 'bg-[#22c55e] animate-pulse-subtle' : 'bg-[var(--text-muted)]'"
        />
        <span class="text-[11px] font-mono" :class="isMining ? 'text-[#22c55e]' : 'text-[var(--text-muted)]'">
          {{ isMining ? 'mining' : 'idle' }}
        </span>
      </div>
      <!-- Hashrate sparkline -->
      <div v-if="system.hashrateHistory.length > 1" class="flex justify-center mt-2">
        <Sparkline :data="system.hashrateHistory" color="#f97316" :width="160" :height="28" />
      </div>
    </div>

    <!-- Stats row -->
    <div class="grid grid-cols-3 gap-2">
      <div class="bg-[var(--surface)] border border-[var(--border)] rounded p-3 text-center">
        <div class="text-[10px] font-mono text-[var(--text-secondary)] uppercase tracking-wider mb-1">Shares</div>
        <div class="font-mono font-bold text-xl text-[#22c55e]">{{ accepted.toLocaleString() }}</div>
      </div>
      <div class="bg-[var(--surface)] border border-[var(--border)] rounded p-3 text-center">
        <div class="text-[10px] font-mono text-[var(--text-secondary)] uppercase tracking-wider mb-1">Session Best</div>
        <div class="font-mono font-bold text-xl text-[#f97316]">{{ bestDiffStr }}</div>
      </div>
      <div class="bg-[var(--surface)] border border-[var(--border)] rounded p-3 text-center">
        <div class="text-[10px] font-mono text-[var(--text-secondary)] uppercase tracking-wider mb-1">All-Time Best</div>
        <div class="font-mono font-bold text-xl text-[#eab308]">{{ alltimeDiffStr }}</div>
      </div>
    </div>

    <!-- Time to block (lottery feel) -->
    <div class="bg-[var(--surface)] border border-[var(--border)] rounded p-3 text-center">
      <div class="text-[10px] font-mono text-[var(--text-secondary)] uppercase tracking-wider mb-1">Est. Time to Solo Block</div>
      <div class="font-mono text-lg text-[#eab308]">{{ timeToBlock }}</div>
      <div class="text-[10px] font-mono text-[var(--text-muted)] mt-1">but it could be the next share...</div>
    </div>

    <!-- Share feed with all/submitted toggle -->
    <div class="bg-[var(--surface)] border border-[var(--border)] rounded p-3 h-[240px] flex flex-col">
      <ShareFeed
        :all-shares="mining.allShares"
        :submitted-shares="mining.submittedShares"
        :best-diff="bestDiff"
      />
    </div>

    <!-- Health Overview -->
    <div class="bg-[var(--surface)] border border-[var(--border)] rounded p-3">
      <div class="text-[10px] font-mono text-[var(--text-secondary)] uppercase tracking-wider mb-3">System Health</div>
      <div class="grid grid-cols-2 gap-x-4 gap-y-2.5">
        <!-- ASIC Temp -->
        <div class="flex items-center justify-between">
          <span class="text-[11px] font-mono text-[var(--text-secondary)]">ASIC</span>
          <div class="flex items-center gap-1.5">
            <span class="font-mono text-sm font-medium" :style="{ color: tempColor }">{{ chipTemp.toFixed(0) }}&deg;C</span>
            <span class="w-1.5 h-1.5 rounded-full" :style="{ backgroundColor: tempColor }" />
          </div>
        </div>
        <!-- VRM Temp -->
        <div class="flex items-center justify-between">
          <span class="text-[11px] font-mono text-[var(--text-secondary)]">VRM</span>
          <div class="flex items-center gap-1.5">
            <span class="font-mono text-sm font-medium" :style="{ color: vrmColor }">{{ vrmTemp.toFixed(0) }}&deg;C</span>
            <span class="w-1.5 h-1.5 rounded-full" :style="{ backgroundColor: vrmColor }" />
          </div>
        </div>
        <!-- Fan -->
        <div class="flex items-center justify-between">
          <span class="text-[11px] font-mono text-[var(--text-secondary)]">Fan</span>
          <span class="font-mono text-sm text-[var(--text)]">{{ fanRpm.toLocaleString() }} rpm</span>
        </div>
        <!-- Power -->
        <div class="flex items-center justify-between">
          <span class="text-[11px] font-mono text-[var(--text-secondary)]">Power</span>
          <span class="font-mono text-sm text-[var(--text)]">{{ power.toFixed(1) }}W</span>
        </div>
        <!-- Efficiency -->
        <div class="flex items-center justify-between">
          <span class="text-[11px] font-mono text-[var(--text-secondary)]">Efficiency</span>
          <span class="font-mono text-sm" :style="{ color: efficiencyColor }">{{ efficiencyDisplay }} J/TH</span>
        </div>
        <!-- Reject Rate -->
        <div class="flex items-center justify-between">
          <span class="text-[11px] font-mono text-[var(--text-secondary)]">Reject</span>
          <span class="font-mono text-sm" :style="{ color: rejectColor }">{{ rejectRate.toFixed(1) }}%</span>
        </div>
      </div>
      <!-- Overall status bar -->
      <div class="mt-3 pt-2.5 border-t border-[var(--border)] flex items-center gap-2">
        <span class="w-2 h-2 rounded-full" :style="{ backgroundColor: overallHealthColor }" />
        <span class="text-[11px] font-mono" :style="{ color: overallHealthColor }">{{ overallHealthLabel }}</span>
      </div>
    </div>

    <!-- Pool status -->
    <div class="bg-[var(--surface)] border border-[var(--border)] rounded p-3">
      <div class="flex items-center gap-2 text-xs font-mono">
        <span
          class="w-2 h-2 rounded-full shrink-0"
          :class="poolConnected ? 'bg-[#22c55e]' : 'bg-[#ef4444]'"
        />
        <span class="text-[var(--text-secondary)] truncate">stratum+tcp://{{ poolUrl }}</span>
        <span :class="poolConnected ? 'text-[#22c55e]' : 'text-[#ef4444]'" class="shrink-0">
          {{ poolConnected ? 'connected' : 'disconnected' }}
        </span>
      </div>
    </div>

    <!-- Footer stats -->
    <div class="text-center text-[10px] font-mono text-[var(--text-muted)] pt-1">
      <span>uptime: {{ uptime }}</span>
    </div>
  </div>
</template>
