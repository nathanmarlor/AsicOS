<script setup lang="ts">
import { computed } from 'vue'
import { useRouter } from 'vue-router'
import { useSystemStore } from '../stores/system'
import { useMiningStore } from '../stores/mining'
import { useApi } from '../composables/useApi'
import AsicGrid from '../components/AsicGrid.vue'
import PoolStatus from '../components/PoolStatus.vue'
import LogConsole from '../components/LogConsole.vue'

const router = useRouter()
const system = useSystemStore()
const mining = useMiningStore()
const { post } = useApi()

const hashrate = computed(() => mining.info?.hashrate_ghs ?? 0)
const efficiency = computed(() => {
  const p = system.info?.power_watts ?? 0
  if (p === 0) return 0
  return hashrate.value / p
})
const power = computed(() => system.info?.power_watts ?? 0)
const chipTemp = computed(() => system.info?.chip_temp ?? 0)
const vrTemp = computed(() => system.info?.vr_temp ?? 0)
const chips = computed(() => mining.info?.chips ?? [])

const details = computed(() => {
  const m = mining.info
  const s = system.info
  if (!m || !s) return []
  return [
    { label: 'Pool Difficulty', value: mining.formatDiff(m.pool_difficulty) },
    { label: 'Best Difficulty', value: m.best_diff_str || mining.formatDiff(m.best_diff) },
    { label: 'Accepted / Rejected', value: `${m.shares_accepted} / ${m.shares_rejected}` },
    { label: 'Frequency', value: `${s.frequency} MHz` },
    { label: 'Core Voltage', value: `${s.core_voltage} mV` },
    { label: 'VR Temp', value: `${s.vr_temp.toFixed(1)} C` },
    { label: 'Board Temp', value: `${s.board_temp.toFixed(1)} C` },
    { label: 'Fan 0', value: `${s.fan_speed_0} RPM` },
    { label: 'Fan 1', value: `${s.fan_speed_1} RPM` },
    { label: 'Free Heap', value: `${(s.free_heap / 1024).toFixed(0)} KB` },
    { label: 'Uptime', value: formatUptime(s.uptime_seconds) },
  ]
})

function formatUptime(s: number): string {
  const d = Math.floor(s / 86400)
  const h = Math.floor((s % 86400) / 3600)
  const m = Math.floor((s % 3600) / 60)
  return d > 0 ? `${d}d ${h}h ${m}m` : `${h}h ${m}m`
}

const poolUrl = computed(() => {
  if (!mining.info) return '--'
  return `${mining.info.pool_url}:${mining.info.pool_port}`
})

const poolConnected = computed(() => mining.info?.pool_connected ?? false)

let confirmRestart = false
async function restart() {
  if (!confirmRestart) {
    confirmRestart = true
    setTimeout(() => { confirmRestart = false }, 3000)
    return
  }
  try {
    await post('/api/system/restart')
  } catch { /* device will restart */ }
}
</script>

<template>
  <div class="px-4 py-4 space-y-4 max-w-6xl mx-auto">
    <!-- Top metric bar -->
    <div class="grid grid-cols-5 gap-2">
      <div class="card text-center py-3">
        <div class="font-mono font-bold text-xl text-white">{{ hashrate.toFixed(1) }}</div>
        <div class="text-[10px] font-mono text-gray-600">GH/s</div>
      </div>
      <div class="card text-center py-3">
        <div class="font-mono font-bold text-xl text-white">{{ efficiency.toFixed(2) }}</div>
        <div class="text-[10px] font-mono text-gray-600">GH/s/W</div>
      </div>
      <div class="card text-center py-3">
        <div class="font-mono font-bold text-xl text-white">{{ power.toFixed(0) }}</div>
        <div class="text-[10px] font-mono text-gray-600">Watts</div>
      </div>
      <div class="card text-center py-3">
        <div class="font-mono font-bold text-xl text-white">{{ chipTemp.toFixed(0) }}&deg;</div>
        <div class="text-[10px] font-mono text-gray-600">Chip Temp</div>
      </div>
      <div class="card text-center py-3">
        <div class="font-mono font-bold text-xl text-white">{{ vrTemp.toFixed(0) }}&deg;</div>
        <div class="text-[10px] font-mono text-gray-600">VR Temp</div>
      </div>
    </div>

    <!-- Main content: 2 columns -->
    <div class="grid grid-cols-1 lg:grid-cols-3 gap-4">
      <!-- Left: ASIC grid + details -->
      <div class="lg:col-span-2 space-y-4">
        <!-- ASIC Hashmap -->
        <div class="card">
          <AsicGrid :chips="chips" />
        </div>

        <!-- Mining details table -->
        <div class="card">
          <div class="text-[10px] font-mono text-gray-600 uppercase tracking-wider mb-2">Mining Details</div>
          <table class="w-full text-xs font-mono">
            <tbody>
              <tr v-for="row in details" :key="row.label" class="border-b border-border/50 last:border-0">
                <td class="py-1.5 text-gray-500 pr-4">{{ row.label }}</td>
                <td class="py-1.5 text-gray-300 text-right">{{ row.value }}</td>
              </tr>
            </tbody>
          </table>
        </div>

        <!-- Pool status -->
        <div class="card">
          <PoolStatus :url="poolUrl" :connected="poolConnected" />
        </div>
      </div>

      <!-- Right: Log console + actions -->
      <div class="space-y-4">
        <div class="card h-[420px] flex flex-col">
          <LogConsole />
        </div>

        <!-- Quick actions -->
        <div class="flex gap-2">
          <button @click="router.push('/tuner')" class="btn btn-secondary flex-1">
            Auto-Tune
          </button>
          <button @click="restart" class="btn btn-danger flex-1">
            {{ confirmRestart ? 'Confirm Restart?' : 'Restart' }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>
