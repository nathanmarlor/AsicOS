<script setup lang="ts">
import { computed } from 'vue'
import { useSystemStore } from '../stores/system'
import { useMiningStore } from '../stores/mining'
import HashrateDial from '../components/HashrateDial.vue'
import TemperatureBar from '../components/TemperatureBar.vue'
import PoolStatus from '../components/PoolStatus.vue'
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
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  return `${h}h ${m}m`
})
const power = computed(() => system.info?.power.watts ?? 0)
</script>

<template>
  <div class="max-w-lg mx-auto px-4 py-8 space-y-6">
    <!-- Big hashrate -->
    <HashrateDial :hashrate="hashrate" :is-mining="isMining" />

    <!-- Stats row -->
    <div class="grid grid-cols-2 gap-3">
      <div class="card text-center">
        <div class="text-[10px] font-mono text-gray-600 uppercase tracking-wider mb-1">Shares</div>
        <div class="font-mono font-bold text-2xl text-green-500">{{ accepted.toLocaleString() }}</div>
      </div>
      <div class="card text-center">
        <div class="text-[10px] font-mono text-gray-600 uppercase tracking-wider mb-1">Best Diff</div>
        <div class="font-mono font-bold text-2xl text-accent">{{ bestDiffStr }}</div>
      </div>
    </div>

    <!-- Share feed -->
    <div class="card">
      <ShareFeed :shares="mining.shares" :best-diff="bestDiff" />
    </div>

    <!-- Temperature -->
    <TemperatureBar label="Chip Temp" :value="chipTemp" />

    <!-- Pool status -->
    <PoolStatus :url="poolUrl" :connected="poolConnected" />

    <!-- Footer stats -->
    <div class="flex justify-between text-[10px] font-mono text-gray-600 pt-2">
      <span>uptime: {{ uptime }}</span>
      <span>{{ power.toFixed(0) }}W</span>
    </div>
  </div>
</template>
