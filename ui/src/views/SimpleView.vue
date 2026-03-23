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

// Temperature status
const tempStatus = computed(() => {
  const t = chipTemp.value
  if (t < 60) return 'good'
  if (t < 80) return 'warn'
  return 'danger'
})

const tempColor = computed(() => {
  switch (tempStatus.value) {
    case 'good': return '#22c55e'
    case 'warn': return '#eab308'
    default: return '#ef4444'
  }
})

// Temperature arc (0-120 range, 180 degree arc)
const tempArcD = computed(() => {
  const t = Math.min(120, Math.max(0, chipTemp.value))
  const pct = t / 120
  const angle = pct * 180 // 0 to 180 degrees
  const rad = (angle * Math.PI) / 180
  const r = 36
  const cx = 44
  const cy = 40
  const startX = cx - r
  const startY = cy
  const endX = cx + r * Math.cos(Math.PI - rad)
  const endY = cy - r * Math.sin(rad)
  const largeArc = angle > 90 ? 1 : 0
  return `M${startX},${startY} A${r},${r} 0 ${largeArc} 1 ${endX},${endY}`
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
        <div class="font-mono font-bold text-5xl sm:text-6xl text-[#e5e5e5] tracking-tight">
          {{ hashrate.toFixed(1) }}
        </div>
        <div class="text-sm font-mono text-[#6b7280] mt-1 tracking-widest">GH/s</div>
        <!-- Accent underline -->
        <div class="mt-2 mx-auto h-[2px] w-16 bg-[#f97316] rounded-full" :class="{ 'animate-pulse-subtle': isMining }" />
      </div>
      <div class="flex items-center justify-center gap-1.5 mt-3">
        <span
          class="w-2 h-2 rounded-full"
          :class="isMining ? 'bg-[#22c55e] animate-pulse-subtle' : 'bg-[#4b4b4b]'"
        />
        <span class="text-[11px] font-mono" :class="isMining ? 'text-[#22c55e]' : 'text-[#4b4b4b]'">
          {{ isMining ? 'mining' : 'idle' }}
        </span>
      </div>
      <!-- Hashrate sparkline -->
      <div v-if="system.hashrateHistory.length > 1" class="flex justify-center mt-2">
        <Sparkline :data="system.hashrateHistory" color="#f97316" :width="160" :height="28" />
      </div>
    </div>

    <!-- Stats row -->
    <div class="grid grid-cols-2 gap-2">
      <div class="bg-[#111111] border border-[#1e1e1e] rounded p-3 text-center">
        <div class="text-[10px] font-mono text-[#6b7280] uppercase tracking-wider mb-1">Shares</div>
        <div class="font-mono font-bold text-2xl text-[#22c55e]">{{ accepted.toLocaleString() }}</div>
      </div>
      <div class="bg-[#111111] border border-[#1e1e1e] rounded p-3 text-center">
        <div class="text-[10px] font-mono text-[#6b7280] uppercase tracking-wider mb-1">Best Diff</div>
        <div class="font-mono font-bold text-2xl text-[#f97316]">{{ bestDiffStr }}</div>
      </div>
    </div>

    <!-- Time to block (lottery feel) -->
    <div class="bg-[#111111] border border-[#1e1e1e] rounded p-3 text-center">
      <div class="text-[10px] font-mono text-[#6b7280] uppercase tracking-wider mb-1">Est. Time to Solo Block</div>
      <div class="font-mono text-lg text-[#eab308]">{{ timeToBlock }}</div>
      <div class="text-[10px] font-mono text-[#4b4b4b] mt-1">but it could be the next share...</div>
    </div>

    <!-- Share feed with all/submitted toggle -->
    <div class="bg-[#111111] border border-[#1e1e1e] rounded p-3 min-h-[200px]">
      <ShareFeed
        :all-shares="mining.allShares"
        :submitted-shares="mining.submittedShares"
        :best-diff="bestDiff"
      />
    </div>

    <!-- Temperature gauge arc -->
    <div class="bg-[#111111] border border-[#1e1e1e] rounded p-3">
      <div class="text-[10px] font-mono text-[#6b7280] uppercase tracking-wider mb-2">Chip Temperature</div>
      <div class="flex items-center gap-4">
        <svg width="88" height="50" viewBox="0 0 88 50" class="shrink-0">
          <!-- Background arc -->
          <path
            d="M8,40 A36,36 0 0 1 80,40"
            fill="none"
            stroke="#1e1e1e"
            stroke-width="4"
            stroke-linecap="round"
          />
          <!-- Value arc -->
          <path
            :d="tempArcD"
            fill="none"
            :stroke="tempColor"
            stroke-width="4"
            stroke-linecap="round"
          />
        </svg>
        <div>
          <div class="font-mono font-bold text-2xl" :style="{ color: tempColor }">{{ chipTemp.toFixed(0) }}&deg;C</div>
          <div class="text-[10px] font-mono text-[#4b4b4b]">
            {{ chipTemp < 60 ? 'Normal' : chipTemp < 80 ? 'Warm' : 'Hot' }}
          </div>
        </div>
      </div>
    </div>

    <!-- Pool status -->
    <div class="bg-[#111111] border border-[#1e1e1e] rounded p-3">
      <div class="flex items-center gap-2 text-xs font-mono">
        <span
          class="w-2 h-2 rounded-full shrink-0"
          :class="poolConnected ? 'bg-[#22c55e]' : 'bg-[#ef4444]'"
        />
        <span class="text-[#6b7280] truncate">stratum+tcp://{{ poolUrl }}</span>
        <span :class="poolConnected ? 'text-[#22c55e]' : 'text-[#ef4444]'" class="shrink-0">
          {{ poolConnected ? 'connected' : 'disconnected' }}
        </span>
      </div>
    </div>

    <!-- Footer stats -->
    <div class="flex justify-between text-[10px] font-mono text-[#4b4b4b] pt-1">
      <span>uptime: {{ uptime }}</span>
      <span>{{ power.toFixed(1) }}W</span>
    </div>
  </div>
</template>
