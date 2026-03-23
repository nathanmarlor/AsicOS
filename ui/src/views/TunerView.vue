<script setup lang="ts">
import { onMounted, onUnmounted, computed } from 'vue'
import { useTunerStore } from '../stores/tuner'

const tuner = useTunerStore()

onMounted(() => { tuner.poll() })
onUnmounted(() => { tuner.stopPolling() })

const isRunning = computed(() => tuner.status?.running ?? false)
const isComplete = computed(() => tuner.status?.complete ?? false)
const progress = computed(() => tuner.status?.progress ?? 0)
const step = computed(() => tuner.status?.step ?? 0)
const totalSteps = computed(() => tuner.status?.total_steps ?? 0)
const results = computed(() => tuner.status?.results ?? [])
const profiles = computed(() => tuner.status?.profiles ?? null)

async function handleStart() {
  await tuner.start()
}
</script>

<template>
  <div class="max-w-3xl mx-auto px-4 py-6 space-y-6">
    <div class="text-sm font-mono text-[var(--text-secondary)]">Auto-Tuner</div>

    <!-- Start/Abort -->
    <div class="flex gap-3">
      <button
        v-if="!isRunning"
        @click="handleStart"
        class="btn btn-primary"
      >
        Start Auto-Tune
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
    </div>

    <!-- Profile cards -->
    <div v-if="isComplete && profiles" class="grid grid-cols-3 gap-3">
      <!-- Eco profile -->
      <div
        class="card text-left space-y-2"
        :class="profiles.eco ? 'border-green-600' : 'opacity-40'"
      >
        <div class="font-mono font-medium text-sm text-green-400">Eco</div>
        <div class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">Best efficiency</div>
        <template v-if="profiles.eco">
          <div class="text-xs font-mono text-[var(--text-secondary)] space-y-0.5">
            <div>{{ profiles.eco.freq }} MHz @ {{ profiles.eco.voltage }} mV</div>
            <div>{{ profiles.eco.hashrate.toFixed(1) }} GH/s</div>
            <div>{{ profiles.eco.power.toFixed(1) }} W</div>
            <div class="text-green-400 font-medium">{{ profiles.eco.efficiency.toFixed(3) }} GH/s/W</div>
            <div>{{ profiles.eco.temp.toFixed(0) }}&deg;C</div>
          </div>
          <button @click="tuner.applyProfile('eco')" class="btn btn-primary w-full text-xs mt-2">
            Apply Eco
          </button>
        </template>
        <div v-else class="text-xs font-mono text-[var(--text-muted)]">No valid result</div>
      </div>

      <!-- Balanced profile -->
      <div
        class="card text-left space-y-2"
        :class="profiles.balanced ? 'border-accent' : 'opacity-40'"
      >
        <div class="font-mono font-medium text-sm text-accent">Balanced</div>
        <div class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">Best overall</div>
        <template v-if="profiles.balanced">
          <div class="text-xs font-mono text-[var(--text-secondary)] space-y-0.5">
            <div>{{ profiles.balanced.freq }} MHz @ {{ profiles.balanced.voltage }} mV</div>
            <div>{{ profiles.balanced.hashrate.toFixed(1) }} GH/s</div>
            <div>{{ profiles.balanced.power.toFixed(1) }} W</div>
            <div>{{ profiles.balanced.efficiency.toFixed(3) }} GH/s/W</div>
            <div>{{ profiles.balanced.temp.toFixed(0) }}&deg;C</div>
          </div>
          <button @click="tuner.applyProfile('balanced')" class="btn btn-primary w-full text-xs mt-2">
            Apply Balanced
          </button>
        </template>
        <div v-else class="text-xs font-mono text-[var(--text-muted)]">No valid result</div>
      </div>

      <!-- Power profile -->
      <div
        class="card text-left space-y-2"
        :class="profiles.power ? 'border-orange-500' : 'opacity-40'"
      >
        <div class="font-mono font-medium text-sm text-orange-400">Power</div>
        <div class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider">Max hashrate</div>
        <template v-if="profiles.power">
          <div class="text-xs font-mono text-[var(--text-secondary)] space-y-0.5">
            <div>{{ profiles.power.freq }} MHz @ {{ profiles.power.voltage }} mV</div>
            <div class="text-orange-400 font-medium">{{ profiles.power.hashrate.toFixed(1) }} GH/s</div>
            <div>{{ profiles.power.power.toFixed(1) }} W</div>
            <div>{{ profiles.power.efficiency.toFixed(3) }} GH/s/W</div>
            <div>{{ profiles.power.temp.toFixed(0) }}&deg;C</div>
          </div>
          <button @click="tuner.applyProfile('power')" class="btn btn-primary w-full text-xs mt-2">
            Apply Power
          </button>
        </template>
        <div v-else class="text-xs font-mono text-[var(--text-muted)]">No valid result</div>
      </div>
    </div>

    <!-- Results table -->
    <div v-if="results.length > 0" class="card overflow-x-auto">
      <div class="text-[10px] font-mono text-[var(--text-muted)] uppercase tracking-wider mb-2">Results</div>
      <table class="w-full text-xs font-mono">
        <thead>
          <tr class="text-[var(--text-muted)] border-b border-border">
            <th class="text-left py-1.5 font-normal">#</th>
            <th class="text-right py-1.5 font-normal">MHz</th>
            <th class="text-right py-1.5 font-normal">mV</th>
            <th class="text-right py-1.5 font-normal">GH/s</th>
            <th class="text-right py-1.5 font-normal">W</th>
            <th class="text-right py-1.5 font-normal">GH/s/W</th>
            <th class="text-right py-1.5 font-normal">Temp</th>
            <th class="text-center py-1.5 font-normal">Stable</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="(r, i) in results"
            :key="i"
            class="border-b border-border/30 last:border-0 text-[var(--text-secondary)]"
          >
            <td class="py-1.5">{{ i + 1 }}</td>
            <td class="text-right py-1.5">{{ r.frequency }}</td>
            <td class="text-right py-1.5">{{ r.voltage }}</td>
            <td class="text-right py-1.5">{{ r.hashrate.toFixed(1) }}</td>
            <td class="text-right py-1.5">{{ r.power.toFixed(1) }}</td>
            <td class="text-right py-1.5">{{ r.efficiency.toFixed(2) }}</td>
            <td class="text-right py-1.5">{{ r.temp.toFixed(0) }}&deg;</td>
            <td class="text-center py-1.5">{{ r.stable ? 'Y' : 'N' }}</td>
          </tr>
        </tbody>
      </table>
    </div>

    <div v-if="tuner.error" class="text-xs font-mono text-red-400">
      {{ tuner.error }}
    </div>
  </div>
</template>
