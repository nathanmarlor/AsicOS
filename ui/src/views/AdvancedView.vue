<script setup lang="ts">
import { computed, ref } from 'vue'
import { useRouter } from 'vue-router'
import { useSystemStore } from '../stores/system'
import { useMiningStore } from '../stores/mining'
import { useApi } from '../composables/useApi'
import KpiCard from '../components/KpiCard.vue'
import HashChart from '../components/HashChart.vue'
import AsicGrid from '../components/AsicGrid.vue'
import SharePerformance from '../components/SharePerformance.vue'
import ShareFeed from '../components/ShareFeed.vue'
import PoolQuality from '../components/PoolQuality.vue'
import LogConsole from '../components/LogConsole.vue'

const router = useRouter()
const system = useSystemStore()
const mining = useMiningStore()
const { post } = useApi()

// KPI values
const hashrate = computed(() => mining.info?.hashrate_ghs ?? 0)
const efficiency = computed(() => {
  const p = system.info?.power.watts ?? 0
  if (p === 0) return 0
  return hashrate.value / p
})
const power = computed(() => system.info?.power.watts ?? 0)
const chipTemp = computed(() => system.info?.temps.chip ?? 0)
const vrTemp = computed(() => system.info?.temps.vr ?? 0)
const accepted = computed(() => mining.info?.accepted ?? 0)
const rejected = computed(() => mining.info?.rejected ?? 0)
const rejectPct = computed(() => {
  const total = accepted.value + rejected.value
  if (total === 0) return 0
  return (rejected.value / total) * 100
})

// Trends
const hashrateTrend = computed(() => {
  const h = system.hashrateHistory
  if (h.length < 10) return undefined
  const recent = h.slice(-10)
  const older = h.slice(-20, -10)
  if (older.length === 0) return undefined
  const avgRecent = recent.reduce((a, b) => a + b, 0) / recent.length
  const avgOlder = older.reduce((a, b) => a + b, 0) / older.length
  if (avgOlder === 0) return undefined
  return ((avgRecent - avgOlder) / avgOlder) * 100
})

// Statuses
const asicTempStatus = computed(() => {
  const t = chipTemp.value
  if (t < 60) return 'good' as const
  if (t <= 75) return 'warn' as const
  return 'danger' as const
})

const vrmTempStatus = computed(() => {
  const t = vrTemp.value
  if (t < 70) return 'good' as const
  if (t <= 90) return 'warn' as const
  return 'danger' as const
})

const rejectStatus = computed(() => {
  const p = rejectPct.value
  if (p < 1) return 'good' as const
  if (p < 5) return 'warn' as const
  return 'danger' as const
})

// Health overview KPI
const healthStatus = computed(() => {
  const asicOk = chipTemp.value < 75
  const vrmOk = vrTemp.value < 90
  const asicOverheat = chipTemp.value > 75
  const vrmOverheat = vrTemp.value > 90
  const overheat = system.info?.power.overheat ?? false
  const connected = poolConnected.value
  const rp = rejectPct.value
  const hr = system.hashrateHistory
  // Check hashrate stability (last 10 vs prior 10)
  let hashrateDrop = false
  if (hr.length >= 20) {
    const recent = hr.slice(-10).reduce((a, b) => a + b, 0) / 10
    const older = hr.slice(-20, -10).reduce((a, b) => a + b, 0) / 10
    if (older > 0 && (recent / older) < 0.85) hashrateDrop = true
  }

  // Critical
  if (overheat || asicOverheat || vrmOverheat || !connected || rp > 3) return 'danger' as const
  // Warning
  if (!asicOk || !vrmOk || (rp >= 1 && rp <= 3) || hashrateDrop) return 'warn' as const
  // Good
  return 'good' as const
})

const healthLabel = computed(() => {
  switch (healthStatus.value) {
    case 'good': return 'Good'
    case 'warn': return 'Warning'
    default: return 'Critical'
  }
})

// Chip data
const chips = computed(() => mining.info?.chips ?? [])

// Pool info
const poolUrl = computed(() => {
  if (!system.info) return '--'
  return `${system.info.config.pool_url}:${system.info.config.pool_port}`
})
const poolConnected = computed(() => system.info?.pool.state === 'mining')
const poolDiffStr = computed(() => mining.formatDiff(mining.info?.pool_diff ?? 0))
const bestDiffStr = computed(() => mining.formatDiff(mining.info?.best_diff ?? 0))

// Share rate estimate (shares per minute based on recent allShares timestamps)
const shareRate = computed(() => {
  const s = mining.allShares
  if (s.length < 2) return 0
  const recent = s.slice(0, Math.min(20, s.length))
  const span = recent[0].ts - recent[recent.length - 1].ts
  if (span <= 0) return 0
  return (recent.length / span) * 60000
})

// Mining details table
const details = computed(() => {
  const m = mining.info
  const s = system.info
  if (!m || !s) return []
  return [
    { label: 'Pool Difficulty', value: mining.formatDiff(m.pool_diff) },
    { label: 'Best Difficulty', value: mining.formatDiff(m.best_diff) },
    { label: 'Accepted / Rejected', value: `${m.accepted} / ${m.rejected}` },
    { label: 'Frequency', value: `${s.config.frequency} MHz` },
    { label: 'Core Voltage', value: `${s.config.voltage} mV` },
    { label: 'Input Voltage', value: `${s.power.vin.toFixed(1)} V` },
    { label: 'Output Voltage', value: `${s.power.vout.toFixed(2)} V` },
    { label: 'VR Temp', value: `${s.temps.vr.toFixed(1)}\u00B0C` },
    { label: 'Board Temp', value: `${s.temps.board.toFixed(1)}\u00B0C` },
    { label: 'Fan 0', value: `${s.power.fan0_rpm} RPM` },
    { label: 'Fan 1', value: `${s.power.fan1_rpm} RPM` },
    { label: 'Free Heap', value: `${(s.free_heap / 1024).toFixed(0)} KB` },
    { label: 'Uptime', value: formatUptime(Math.floor(s.uptime_ms / 1000)) },
  ]
})

function formatUptime(s: number): string {
  const d = Math.floor(s / 86400)
  const h = Math.floor((s % 86400) / 3600)
  const m = Math.floor((s % 3600) / 60)
  return d > 0 ? `${d}d ${h}h ${m}m` : `${h}h ${m}m`
}

let confirmRestart = ref(false)
async function restart() {
  if (!confirmRestart.value) {
    confirmRestart.value = true
    setTimeout(() => { confirmRestart.value = false }, 3000)
    return
  }
  try {
    await post('/api/system/restart')
  } catch { /* device will restart */ }
}
</script>

<template>
  <div class="px-3 py-3 space-y-3 max-w-[1400px] mx-auto">

    <!-- Status Banner -->
    <div
      class="flex items-center justify-between px-3 py-1.5 bg-[var(--surface)] border border-[var(--border)] rounded text-[11px] font-mono"
    >
      <div class="flex items-center gap-2">
        <span
          class="w-2 h-2 rounded-full"
          :class="poolConnected ? 'bg-[#22c55e] animate-pulse-subtle' : 'bg-[#ef4444]'"
        />
        <span :class="poolConnected ? 'text-[#22c55e]' : 'text-[#ef4444]'">
          {{ poolConnected ? 'Mining' : 'Disconnected' }}
        </span>
        <span class="text-[var(--text-muted)]">|</span>
        <span class="text-[var(--text-secondary)]">{{ system.info?.board_name ?? '--' }}</span>
        <span class="text-[var(--text-muted)]">|</span>
        <span class="text-[var(--text-secondary)]">{{ system.info?.asic_model ?? '--' }}</span>
      </div>
      <div class="flex items-center gap-3 text-[var(--text-muted)]">
        <span v-if="system.info">uptime: {{ formatUptime(Math.floor(system.info.uptime_ms / 1000)) }}</span>
        <span v-if="system.info">heap: {{ (system.info.free_heap / 1024).toFixed(0) }}KB</span>
      </div>
    </div>

    <!-- KPI Strip -->
    <div class="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-6 gap-2">
      <KpiCard
        label="Hashrate"
        :value="hashrate.toFixed(1)"
        unit="GH/s"
        :history="system.hashrateHistory"
        status="good"
        :trend="hashrateTrend"
      />
      <KpiCard
        label="Efficiency"
        :value="efficiency.toFixed(2)"
        unit="GH/s/W"
        status="neutral"
      />
      <KpiCard
        label="Power"
        :value="power.toFixed(1)"
        unit="Watts"
        :history="system.powerHistory"
        status="neutral"
        spark-color="#3b82f6"
      />
      <KpiCard
        label="ASIC Temp"
        :value="chipTemp.toFixed(0) + '\u00B0'"
        unit="Celsius"
        :history="system.chipTempHistory"
        :status="asicTempStatus"
      />
      <KpiCard
        label="VRM Temp"
        :value="vrTemp.toFixed(0) + '\u00B0'"
        unit="Celsius"
        :history="system.vrTempHistory"
        :status="vrmTempStatus"
      />
      <KpiCard
        label="Reject Rate"
        :value="rejectPct.toFixed(1) + '%'"
        unit="of shares"
        :status="rejectStatus"
      />
    </div>

    <!-- Charts Row -->
    <div class="grid grid-cols-1 lg:grid-cols-5 gap-3">
      <div class="lg:col-span-3 bg-[var(--surface)] border border-[var(--border)] rounded p-3">
        <HashChart
          :data="system.hashrateHistory"
          color="#f97316"
          :height="200"
          label="HASHRATE (GH/s)"
          show-grid
        />
      </div>
      <div class="lg:col-span-2 bg-[var(--surface)] border border-[var(--border)] rounded p-3">
        <HashChart
          :data="system.chipTempHistory"
          color="#ef4444"
          :height="200"
          label="TEMPERATURE (\u00B0C)"
          show-grid
        />
      </div>
    </div>

    <!-- Middle Row: ASIC Grid + Share Performance + Share Feed -->
    <div class="grid grid-cols-1 lg:grid-cols-3 gap-3">
      <div class="bg-[var(--surface)] border border-[var(--border)] rounded p-3">
        <AsicGrid :chips="chips" />
      </div>
      <div class="bg-[var(--surface)] border border-[var(--border)] rounded p-3">
        <SharePerformance
          :accepted="accepted"
          :rejected="rejected"
          :best-diff="bestDiffStr"
          :pool-diff="poolDiffStr"
          :duplicates="mining.info?.duplicates ?? 0"
          :share-rate="shareRate"
        />
      </div>
      <div class="bg-[var(--surface)] border border-[var(--border)] rounded p-3 flex flex-col min-h-[280px]">
        <ShareFeed
          :all-shares="mining.allShares"
          :submitted-shares="mining.submittedShares"
          :best-diff="mining.info?.best_diff ?? 0"
        />
      </div>
    </div>

    <!-- Bottom Row: Pool + Details + Log -->
    <div class="grid grid-cols-1 lg:grid-cols-10 gap-3">
      <div class="lg:col-span-3 bg-[var(--surface)] border border-[var(--border)] rounded p-3">
        <PoolQuality
          :connected="poolConnected"
          :url="poolUrl"
          :difficulty="poolDiffStr"
          :reconnects="0"
          :worker="system.info?.config.pool_user ?? '--'"
        />
      </div>
      <div class="lg:col-span-4 bg-[var(--surface)] border border-[var(--border)] rounded p-3">
        <div class="text-[10px] font-mono text-[var(--text-secondary)] uppercase tracking-wider mb-2">Mining Details</div>
        <table class="w-full text-[11px] font-mono">
          <tbody>
            <tr
              v-for="(row, i) in details"
              :key="row.label"
              class="border-b border-[var(--border)]/50 last:border-0"
              :class="i % 2 === 0 ? '' : 'bg-[var(--bg)]/30'"
            >
              <td class="py-1 text-[var(--text-secondary)] pr-4">{{ row.label }}</td>
              <td class="py-1 text-[var(--text)] text-right">{{ row.value }}</td>
            </tr>
          </tbody>
        </table>
      </div>
      <div class="lg:col-span-3 bg-[var(--surface)] border border-[var(--border)] rounded p-3 flex flex-col min-h-[240px]">
        <LogConsole />
      </div>
    </div>

    <!-- Action Buttons -->
    <div class="flex flex-wrap gap-2">
      <button
        @click="router.push('/tuner')"
        class="btn btn-secondary min-h-[44px]"
      >
        Auto-Tune
      </button>
      <button
        @click="restart"
        class="btn btn-danger min-h-[44px]"
      >
        {{ confirmRestart ? 'Confirm Restart?' : 'Restart' }}
      </button>
    </div>
  </div>
</template>
