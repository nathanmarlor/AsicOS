<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed } from 'vue'
import { useTunerStore } from '../stores/tuner'

const tuner = useTunerStore()
const selectedMode = ref('balanced')

const modes = [
  { id: 'eco', label: 'Eco', desc: 'Low power, lower temps, reduced hashrate' },
  { id: 'balanced', label: 'Balanced', desc: 'Optimal balance of hashrate and efficiency' },
  { id: 'power', label: 'Power', desc: 'Maximum hashrate, higher power draw' }
]

onMounted(() => { tuner.poll() })
onUnmounted(() => { tuner.stopPolling() })

const isRunning = computed(() => tuner.status?.running ?? false)
const isComplete = computed(() => tuner.status?.complete ?? false)
const progress = computed(() => tuner.status?.progress ?? 0)
const step = computed(() => tuner.status?.step ?? 0)
const totalSteps = computed(() => tuner.status?.total_steps ?? 0)
const results = computed(() => tuner.status?.results ?? [])
const bestIndex = computed(() => tuner.status?.best_index ?? -1)

async function handleStart() {
  await tuner.start(selectedMode.value)
}
</script>

<template>
  <div class="max-w-3xl mx-auto px-4 py-6 space-y-6">
    <div class="text-sm font-mono text-gray-500">Auto-Tuner</div>

    <!-- Mode selector -->
    <div class="grid grid-cols-3 gap-3">
      <button
        v-for="mode in modes"
        :key="mode.id"
        @click="selectedMode = mode.id"
        :disabled="isRunning"
        class="card text-left transition-colors"
        :class="selectedMode === mode.id ? 'border-accent' : 'hover:border-gray-600'"
      >
        <div class="font-mono font-medium text-sm" :class="selectedMode === mode.id ? 'text-accent' : 'text-gray-300'">
          {{ mode.label }}
        </div>
        <div class="text-[11px] text-gray-600 mt-1">{{ mode.desc }}</div>
      </button>
    </div>

    <!-- Start/Abort -->
    <div class="flex gap-3">
      <button
        v-if="!isRunning"
        @click="handleStart"
        class="btn btn-primary"
      >
        Start Tuning
      </button>
      <button
        v-else
        @click="tuner.abort()"
        class="btn btn-danger"
      >
        Abort
      </button>
    </div>

    <!-- Progress -->
    <div v-if="isRunning" class="space-y-2">
      <div class="flex justify-between text-xs font-mono text-gray-500">
        <span>Step {{ step }} / {{ totalSteps }}</span>
        <span>{{ progress.toFixed(0) }}%</span>
      </div>
      <div class="h-2 bg-[var(--surface-light)] rounded-full overflow-hidden">
        <div
          class="h-full bg-accent rounded-full transition-all duration-500"
          :style="{ width: progress + '%' }"
        />
      </div>
    </div>

    <!-- Results table -->
    <div v-if="results.length > 0" class="card overflow-x-auto">
      <div class="text-[10px] font-mono text-gray-600 uppercase tracking-wider mb-2">Results</div>
      <table class="w-full text-xs font-mono">
        <thead>
          <tr class="text-gray-600 border-b border-border">
            <th class="text-left py-1.5 font-normal">#</th>
            <th class="text-right py-1.5 font-normal">MHz</th>
            <th class="text-right py-1.5 font-normal">mV</th>
            <th class="text-right py-1.5 font-normal">GH/s</th>
            <th class="text-right py-1.5 font-normal">W</th>
            <th class="text-right py-1.5 font-normal">GH/s/W</th>
            <th class="text-right py-1.5 font-normal">Temp</th>
            <th class="text-center py-1.5 font-normal">Stable</th>
            <th class="text-right py-1.5 font-normal">Score</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="(r, i) in results"
            :key="i"
            class="border-b border-border/30 last:border-0"
            :class="i === bestIndex ? 'text-accent' : 'text-gray-400'"
          >
            <td class="py-1.5">{{ i + 1 }}</td>
            <td class="text-right py-1.5">{{ r.frequency }}</td>
            <td class="text-right py-1.5">{{ r.voltage }}</td>
            <td class="text-right py-1.5">{{ r.hashrate.toFixed(1) }}</td>
            <td class="text-right py-1.5">{{ r.power.toFixed(1) }}</td>
            <td class="text-right py-1.5">{{ r.efficiency.toFixed(2) }}</td>
            <td class="text-right py-1.5">{{ r.temp.toFixed(0) }}&deg;</td>
            <td class="text-center py-1.5">{{ r.stable ? 'Y' : 'N' }}</td>
            <td class="text-right py-1.5">{{ r.score.toFixed(1) }}</td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- Apply button -->
    <div v-if="isComplete && bestIndex >= 0">
      <button @click="tuner.applyBest()" class="btn btn-primary">
        Apply Best Result
      </button>
    </div>

    <div v-if="tuner.error" class="text-xs font-mono text-red-400">
      {{ tuner.error }}
    </div>
  </div>
</template>
