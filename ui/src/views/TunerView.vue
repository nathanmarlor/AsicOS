<script setup lang="ts">
import { onMounted, onUnmounted, computed } from 'vue'
import { useTunerStore } from '../stores/tuner'

const tuner = useTunerStore()

onMounted(() => { tuner.poll() })
onUnmounted(() => { tuner.stopPolling() })

const isIdle = computed(() => !tuner.status || tuner.status.state === 'idle')
const isRunning = computed(() => tuner.status?.state === 'running')
const isComplete = computed(() => tuner.status?.state === 'complete')
const progress = computed(() => tuner.status?.progress_pct ?? 0)
const step = computed(() => tuner.status?.current_step ?? 0)
const totalSteps = computed(() => tuner.status?.total_steps ?? 0)
const results = computed(() => tuner.status?.results ?? [])
const profiles = computed(() => tuner.status?.profiles ?? null)
const bestIndex = computed(() => tuner.status?.best_index ?? -1)

function jth(r: { hashrate: number; power: number }): string {
  if (r.hashrate <= 0) return '--'
  return (r.power / (r.hashrate / 1000)).toFixed(1)
}

async function handleStart() {
  await tuner.start()
}
</script>

<template>
  <div class="max-w-3xl mx-auto px-4 py-6 space-y-6">
    <div class="text-sm font-mono text-[var(--text-secondary)]">Auto-Tuner</div>

    <!-- Description -->
    <div v-if="isIdle" class="text-xs font-mono text-[var(--text-muted)] leading-relaxed">
      Sweeps frequency and voltage combinations to find optimal settings.
      Tests each combination for 30 seconds, measuring hashrate, power, and temperature.
      Recommends three profiles: Eco (best efficiency), Balanced, and Power (max hashrate).
    </div>

    <!-- Start/Abort -->
    <div class="flex gap-3">
      <button
        v-if="!isRunning"
        @click="handleStart"
        class="btn btn-primary"
      >
        {{ isComplete ? 'Re-run Auto-Tune' : 'Start Auto-Tune' }}
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
      <div class="flex justify-between text-xs font-mono text-[var(--text-secondary)]">
        <span>Step {{ step }} / {{ totalSteps }}</span>
        <span>{{ progress.toFixed(0) }}%</span>
      </div>
      <div class="h-2 bg-[var(--surface-light)] rounded-full overflow-hidden">
        <div
          class="h-full bg-accent rounded-full transition-all duration-500"
          :style="{ width: progress + '%' }"
        />
      </div>
      <!-- Current test info -->
      <div v-if="results.length > 0" class="text-[10px] font-mono text-[var(--text-muted)]">
        Testing {{ results[results.length - 1].freq }} MHz @ {{ results[results.length - 1].voltage }} mV...
      </div>
    </div>

    <!-- Profile cards (shown when complete) -->
    <div v-if="isComplete && profiles" class="grid grid-cols-1 sm:grid-cols-3 gap-3">
      <div
        class="card text-left space-y-2"
        :class="profiles.eco ? 'border-[#22c55e]' : 'opacity-40'"
      >
        <div class="font-mono font-medium text-sm text-[#22c55e]">Eco</div>
        <div class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">Best J/TH</div>
        <template v-if="profiles.eco">
          <div class="text-[11px] font-mono text-[var(--text-secondary)] space-y-0.5">
            <div>{{ profiles.eco.freq }} MHz @ {{ profiles.eco.voltage }} mV</div>
            <div>{{ profiles.eco.hashrate.toFixed(0) }} GH/s</div>
            <div>{{ profiles.eco.power.toFixed(1) }} W</div>
            <div class="text-[#22c55e] font-medium">{{ jth(profiles.eco) }} J/TH</div>
          </div>
          <button @click="tuner.applyProfile('eco')" class="btn btn-primary w-full text-xs mt-2">Apply</button>
        </template>
        <div v-else class="text-xs font-mono text-[var(--text-muted)]">No stable result</div>
      </div>

      <div
        class="card text-left space-y-2"
        :class="profiles.balanced ? 'border-[#f97316]' : 'opacity-40'"
      >
        <div class="font-mono font-medium text-sm text-[#f97316]">Balanced</div>
        <div class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">Best overall</div>
        <template v-if="profiles.balanced">
          <div class="text-[11px] font-mono text-[var(--text-secondary)] space-y-0.5">
            <div>{{ profiles.balanced.freq }} MHz @ {{ profiles.balanced.voltage }} mV</div>
            <div>{{ profiles.balanced.hashrate.toFixed(0) }} GH/s</div>
            <div>{{ profiles.balanced.power.toFixed(1) }} W</div>
            <div class="text-[#f97316] font-medium">{{ jth(profiles.balanced) }} J/TH</div>
          </div>
          <button @click="tuner.applyProfile('balanced')" class="btn btn-primary w-full text-xs mt-2">Apply</button>
        </template>
        <div v-else class="text-xs font-mono text-[var(--text-muted)]">No stable result</div>
      </div>

      <div
        class="card text-left space-y-2"
        :class="profiles.power ? 'border-[#ef4444]' : 'opacity-40'"
      >
        <div class="font-mono font-medium text-sm text-[#ef4444]">Power</div>
        <div class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">Max hashrate</div>
        <template v-if="profiles.power">
          <div class="text-[11px] font-mono text-[var(--text-secondary)] space-y-0.5">
            <div>{{ profiles.power.freq }} MHz @ {{ profiles.power.voltage }} mV</div>
            <div class="text-[#ef4444] font-medium">{{ profiles.power.hashrate.toFixed(0) }} GH/s</div>
            <div>{{ profiles.power.power.toFixed(1) }} W</div>
            <div>{{ jth(profiles.power) }} J/TH</div>
          </div>
          <button @click="tuner.applyProfile('power')" class="btn btn-primary w-full text-xs mt-2">Apply</button>
        </template>
        <div v-else class="text-xs font-mono text-[var(--text-muted)]">No stable result</div>
      </div>
    </div>

    <!-- Results table -->
    <div v-if="results.length > 0" class="card overflow-x-auto">
      <div class="text-[10px] font-mono text-[var(--text-muted)] uppercase tracking-wider mb-2">
        Results ({{ results.length }} tests)
      </div>
      <table class="w-full text-xs font-mono">
        <thead>
          <tr class="text-[var(--text-muted)] border-b border-[var(--border)]">
            <th class="text-left py-1.5 font-normal">#</th>
            <th class="text-right py-1.5 font-normal">MHz</th>
            <th class="text-right py-1.5 font-normal">mV</th>
            <th class="text-right py-1.5 font-normal">GH/s</th>
            <th class="text-right py-1.5 font-normal">W</th>
            <th class="text-right py-1.5 font-normal">J/TH</th>
            <th class="text-right py-1.5 font-normal">Temp</th>
            <th class="text-center py-1.5 font-normal">Stable</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="(r, i) in results"
            :key="i"
            class="border-b border-[var(--border)]/30 last:border-0"
            :class="i === bestIndex ? 'bg-[#f97316]/10 text-[var(--text)]' : 'text-[var(--text-secondary)]'"
          >
            <td class="py-1.5">{{ i + 1 }}</td>
            <td class="text-right py-1.5">{{ r.freq }}</td>
            <td class="text-right py-1.5">{{ r.voltage }}</td>
            <td class="text-right py-1.5">{{ r.hashrate.toFixed(1) }}</td>
            <td class="text-right py-1.5">{{ r.power.toFixed(1) }}</td>
            <td class="text-right py-1.5">{{ jth(r) }}</td>
            <td class="text-right py-1.5">{{ r.temp > 0 ? r.temp.toFixed(0) + '°' : '--' }}</td>
            <td class="text-center py-1.5">{{ r.stable ? '✓' : '✗' }}</td>
          </tr>
        </tbody>
      </table>
    </div>

    <div v-if="tuner.error" class="text-xs font-mono text-[#ef4444]">
      {{ tuner.error }}
    </div>
  </div>
</template>
