<script setup lang="ts">
import { ref, onMounted, computed } from 'vue'
import { useApi } from '../composables/useApi'
import { useSystemStore } from '../stores/system'

const { get, post } = useApi()
const system = useSystemStore()

const saving = ref(false)
const saved = ref(false)
const error = ref('')
const showFallback = ref(false)

// Form fields
const wifiSsid = ref('')
const wifiPassword = ref('')

const poolUrl = ref('')
const poolPort = ref(3333)
const poolUser = ref('')
const poolPassword = ref('')

const fallbackUrl = ref('')
const fallbackPort = ref(3333)
const fallbackUser = ref('')

const frequency = ref(500)
const voltage = ref(1200)

const fanTargetTemp = ref(65)
const overheatTemp = ref(95)
const fanOverride = ref(-1)  // -1 = auto, 0-100 = manual %
const fanMode = computed(() => fanOverride.value < 0 ? 'auto' : 'manual')

const metricsUrl = computed(() => `http://${window.location.host}/metrics`)
const metricsCopied = ref(false)
function copyMetricsUrl() {
  navigator.clipboard.writeText(metricsUrl.value)
  metricsCopied.value = true
  setTimeout(() => { metricsCopied.value = false }, 2000)
}

const lokiUrl = ref('')
const defaultMode = ref('simple')

const freqMin = computed(() => 100)
const freqMax = computed(() => 800)

let confirmRestart = false

onMounted(async () => {
  try {
    const info = await get('/api/system/info')
    const cfg = info.config ?? {}
    wifiSsid.value = cfg.wifi_ssid ?? ''
    wifiPassword.value = ''  // Never returned by API for security
    poolUrl.value = cfg.pool_url ?? ''
    poolPort.value = cfg.pool_port ?? 3333
    poolUser.value = cfg.pool_user ?? ''
    poolPassword.value = ''
    fallbackUrl.value = ''
    fallbackPort.value = 3333
    fallbackUser.value = ''
    frequency.value = cfg.frequency ?? 500
    voltage.value = cfg.voltage ?? 1200
    fanTargetTemp.value = 65
    overheatTemp.value = 95
    lokiUrl.value = ''
    defaultMode.value = cfg.ui_mode ?? 'simple'
  } catch (e: any) {
    error.value = 'Failed to load config: ' + e.message
  }
})

async function save() {
  saving.value = true
  saved.value = false
  error.value = ''
  try {
    await post('/api/system', {
      wifi_ssid: wifiSsid.value,
      wifi_pass: wifiPassword.value,
      pool_url: poolUrl.value,
      pool_port: poolPort.value,
      pool_user: poolUser.value,
      pool_pass: poolPassword.value,
      frequency: frequency.value,
      voltage: voltage.value,
      ui_mode: defaultMode.value
    })
    saved.value = true
    setTimeout(() => { saved.value = false }, 3000)
  } catch (e: any) {
    error.value = e.message
  } finally {
    saving.value = false
  }
}

async function applyFanOverride() {
  try {
    await post('/api/system', { fan_override: fanOverride.value })
  } catch (e: any) {
    error.value = 'Fan override failed: ' + e.message
  }
}

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
  <div class="max-w-xl mx-auto px-4 py-6 space-y-6">
    <div class="text-sm font-mono text-[var(--text-secondary)]">Settings</div>

    <!-- WiFi -->
    <section class="card space-y-3">
      <div class="text-[10px] font-mono text-[var(--text-muted)] uppercase tracking-wider">WiFi</div>
      <div>
        <label class="text-xs text-[var(--text-secondary)] block mb-1">SSID</label>
        <input v-model="wifiSsid" type="text" class="w-full" />
      </div>
      <div>
        <label class="text-xs text-[var(--text-secondary)] block mb-1">Password</label>
        <input v-model="wifiPassword" type="password" class="w-full" />
      </div>
    </section>

    <!-- Primary Pool -->
    <section class="card space-y-3">
      <div class="text-[10px] font-mono text-[var(--text-muted)] uppercase tracking-wider">Primary Pool</div>
      <div class="grid grid-cols-3 gap-2">
        <div class="col-span-2">
          <label class="text-xs text-[var(--text-secondary)] block mb-1">URL</label>
          <input v-model="poolUrl" type="text" class="w-full" placeholder="stratum+tcp://..." />
        </div>
        <div>
          <label class="text-xs text-[var(--text-secondary)] block mb-1">Port</label>
          <input v-model.number="poolPort" type="number" class="w-full" />
        </div>
      </div>
      <div>
        <label class="text-xs text-[var(--text-secondary)] block mb-1">Username</label>
        <input v-model="poolUser" type="text" class="w-full" />
      </div>
      <div>
        <label class="text-xs text-[var(--text-secondary)] block mb-1">Password</label>
        <input v-model="poolPassword" type="password" class="w-full" placeholder="x" />
      </div>
    </section>

    <!-- Fallback Pool -->
    <section class="card space-y-3">
      <button
        @click="showFallback = !showFallback"
        class="text-[10px] font-mono text-[var(--text-muted)] uppercase tracking-wider flex items-center gap-1"
      >
        <span class="transition-transform" :class="{ 'rotate-90': showFallback }">&#9654;</span>
        Fallback Pool
      </button>
      <template v-if="showFallback">
        <div class="grid grid-cols-3 gap-2">
          <div class="col-span-2">
            <label class="text-xs text-[var(--text-secondary)] block mb-1">URL</label>
            <input v-model="fallbackUrl" type="text" class="w-full" />
          </div>
          <div>
            <label class="text-xs text-[var(--text-secondary)] block mb-1">Port</label>
            <input v-model.number="fallbackPort" type="number" class="w-full" />
          </div>
        </div>
        <div>
          <label class="text-xs text-[var(--text-secondary)] block mb-1">Username</label>
          <input v-model="fallbackUser" type="text" class="w-full" />
        </div>
      </template>
    </section>

    <!-- ASIC -->
    <section class="card space-y-3">
      <div class="text-[10px] font-mono text-[var(--text-muted)] uppercase tracking-wider">ASIC</div>
      <div>
        <div class="flex justify-between text-xs mb-1">
          <label class="text-[var(--text-secondary)]">Frequency</label>
          <span class="font-mono text-[var(--text)]">{{ frequency }} MHz</span>
        </div>
        <input
          v-model.number="frequency"
          type="range"
          :min="freqMin"
          :max="freqMax"
          step="25"
          class="w-full accent-accent bg-transparent"
        />
        <div class="flex justify-between text-[10px] text-[var(--text-muted)] font-mono">
          <span>{{ freqMin }}</span>
          <span>{{ freqMax }}</span>
        </div>
      </div>
      <div>
        <div class="flex justify-between text-xs mb-1">
          <label class="text-[var(--text-secondary)]">Core Voltage</label>
          <span class="font-mono text-[var(--text)]">{{ voltage }} mV</span>
        </div>
        <input
          v-model.number="voltage"
          type="range"
          min="800"
          max="1500"
          step="10"
          class="w-full accent-accent bg-transparent"
        />
        <div class="flex justify-between text-[10px] text-[var(--text-muted)] font-mono">
          <span>800</span>
          <span>1500</span>
        </div>
      </div>
    </section>

    <!-- Thermal & Fan -->
    <section class="card space-y-3">
      <div class="text-[10px] font-mono text-[var(--text-muted)] uppercase tracking-wider">Thermal & Fan</div>
      <div>
        <div class="flex justify-between text-xs mb-1">
          <label class="text-[var(--text-secondary)]">Fan Target Temp</label>
          <span class="font-mono text-[var(--text)]">{{ fanTargetTemp }}&deg;C</span>
        </div>
        <input v-model.number="fanTargetTemp" type="range" min="40" max="85" class="w-full accent-accent bg-transparent" />
      </div>
      <div>
        <div class="flex justify-between text-xs mb-1">
          <label class="text-[var(--text-secondary)]">Overheat Temp</label>
          <span class="font-mono text-[var(--text)]">{{ overheatTemp }}&deg;C</span>
        </div>
        <input v-model.number="overheatTemp" type="range" min="70" max="120" class="w-full accent-accent bg-transparent" />
      </div>
      <!-- Fan Override -->
      <div class="border-t border-[var(--border)] pt-3">
        <div class="flex items-center justify-between text-xs mb-2">
          <label class="text-[var(--text-secondary)]">Fan Override</label>
          <div class="flex gap-1.5">
            <button
              @click="fanOverride = -1; applyFanOverride()"
              class="text-[10px] font-mono px-2 py-0.5 rounded transition-colors"
              :class="fanOverride < 0 ? 'bg-[#22c55e]/20 text-[#22c55e]' : 'text-[var(--text-muted)] hover:text-[var(--text)]'"
            >Auto</button>
            <button
              @click="fanOverride = 100; applyFanOverride()"
              class="text-[10px] font-mono px-2 py-0.5 rounded transition-colors"
              :class="fanOverride === 100 ? 'bg-[#f97316]/20 text-[#f97316]' : 'text-[var(--text-muted)] hover:text-[var(--text)]'"
            >100%</button>
          </div>
        </div>
        <div v-if="fanOverride >= 0">
          <div class="flex justify-between text-xs mb-1">
            <span class="text-[var(--text-muted)]">Manual Speed</span>
            <span class="font-mono text-[#f97316]">{{ fanOverride }}%</span>
          </div>
          <input
            v-model.number="fanOverride"
            @change="applyFanOverride"
            type="range"
            min="0"
            max="100"
            step="5"
            class="w-full accent-accent bg-transparent"
          />
        </div>
        <div v-else class="text-[10px] font-mono text-[var(--text-muted)]">
          Fan speed controlled by PID (target: {{ fanTargetTemp }}&deg;C)
        </div>
      </div>
    </section>

    <!-- Monitoring -->
    <section class="card space-y-3">
      <div class="text-[10px] font-mono text-[var(--text-muted)] uppercase tracking-wider">Monitoring</div>
      <div>
        <label class="text-xs text-[var(--text-secondary)] block mb-1">Loki URL</label>
        <input v-model="lokiUrl" type="text" class="w-full" placeholder="http://loki:3100" />
      </div>
      <div class="border-t border-[var(--border)] pt-3">
        <div class="flex items-center justify-between">
          <div>
            <div class="text-xs text-[var(--text-secondary)]">Prometheus Endpoint</div>
            <div class="text-[10px] text-[var(--text-muted)] mt-0.5">Scrape this URL from your Prometheus config</div>
          </div>
          <button
            @click="copyMetricsUrl"
            class="text-[10px] font-mono px-2 py-1 rounded bg-[var(--surface-light)] text-[var(--text-secondary)] hover:text-[var(--text)] transition-colors"
          >{{ metricsCopied ? 'Copied!' : 'Copy' }}</button>
        </div>
        <div class="mt-1.5 font-mono text-[11px] text-[#f97316] bg-[var(--bg)] rounded px-2 py-1.5 select-all break-all">
          {{ metricsUrl }}
        </div>
      </div>
    </section>

    <!-- UI -->
    <section class="card space-y-3">
      <div class="text-[10px] font-mono text-[var(--text-muted)] uppercase tracking-wider">UI</div>
      <div>
        <label class="text-xs text-[var(--text-secondary)] block mb-1">Default Mode</label>
        <select v-model="defaultMode" class="w-full">
          <option value="simple">Simple</option>
          <option value="advanced">Advanced</option>
        </select>
      </div>
    </section>

    <!-- Actions -->
    <div class="flex gap-3">
      <button @click="save" :disabled="saving" class="btn btn-primary">
        {{ saving ? 'Saving...' : saved ? 'Saved' : 'Save' }}
      </button>
      <button @click="restart" class="btn btn-danger">
        {{ confirmRestart ? 'Confirm Restart?' : 'Restart Device' }}
      </button>
    </div>

    <div v-if="error" class="text-xs font-mono text-red-400">{{ error }}</div>
  </div>
</template>
