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
const restartRequired = ref(false)

// OTA state
const otaChecking = ref(false)
const otaInfo = ref<{ current_version: string; latest_version: string; update_available: boolean; fw_download_url: string; www_download_url: string } | null>(null)
const otaUpdating = ref(false)
const otaProgress = ref('')
const otaError = ref('')
const otaFile = ref<File | null>(null)
const otaUploading = ref(false)

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

const freqMin = ref(100)
const freqMax = ref(800)
const voltMin = ref(800)
const voltMax = ref(1500)

const confirmRestart = ref(false)

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
    fallbackUrl.value = cfg.pool2_url ?? ''
    fallbackPort.value = cfg.pool2_port ?? 3333
    fallbackUser.value = cfg.pool2_user ?? ''
    frequency.value = cfg.frequency ?? 500
    voltage.value = cfg.voltage ?? 1200
    freqMin.value = cfg.freq_min ?? 100
    freqMax.value = cfg.freq_max ?? 800
    voltMin.value = cfg.voltage_min ?? 800
    voltMax.value = cfg.voltage_max ?? 1500
    fanTargetTemp.value = cfg.fan_target ?? 65
    overheatTemp.value = cfg.overheat_temp ?? 95
    lokiUrl.value = ''
    defaultMode.value = cfg.ui_mode ?? 'simple'
    // Load fan override from power state
    const pwr = info.power ?? {}
    fanOverride.value = pwr.fan_override ?? -1
  } catch (e: any) {
    error.value = 'Failed to load config: ' + e.message
  }
})

async function save() {
  saving.value = true
  saved.value = false
  error.value = ''
  try {
    // Only send fields that have non-empty values to avoid overwriting with blanks
    const payload: Record<string, any> = {}
    if (wifiSsid.value != null) payload.wifi_ssid = wifiSsid.value
    if (wifiPassword.value != null) payload.wifi_pass = wifiPassword.value
    if (poolUrl.value != null) payload.pool_url = poolUrl.value
    if (poolPort.value != null) payload.pool_port = poolPort.value
    if (poolUser.value != null) payload.pool_user = poolUser.value
    if (poolPassword.value != null) payload.pool_pass = poolPassword.value
    if (frequency.value != null) payload.frequency = frequency.value
    if (voltage.value != null) payload.voltage = voltage.value
    if (fallbackUrl.value != null) payload.pool2_url = fallbackUrl.value
    if (fallbackPort.value != null) payload.pool2_port = fallbackPort.value
    if (fallbackUser.value != null) payload.pool2_user = fallbackUser.value
    if (fanTargetTemp.value != null) payload.fan_target = fanTargetTemp.value
    if (overheatTemp.value != null) payload.overheat_temp = overheatTemp.value
    if (defaultMode.value != null) payload.ui_mode = defaultMode.value
    await post('/api/system', payload)
    saved.value = true
    restartRequired.value = true
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
  if (!confirmRestart.value) {
    confirmRestart.value = true
    setTimeout(() => { confirmRestart.value = false }, 3000)
    return
  }
  try {
    await post('/api/system/restart')
  } catch { /* device will restart */ }
}

// OTA functions
async function checkForUpdates() {
  otaChecking.value = true
  otaError.value = ''
  otaInfo.value = null
  try {
    const data = await get('/api/system/ota/check')
    otaInfo.value = data
  } catch (e: any) {
    otaError.value = 'Failed to check for updates: ' + e.message
  } finally {
    otaChecking.value = false
  }
}

function onFileSelected(event: Event) {
  const input = event.target as HTMLInputElement
  otaFile.value = input.files?.[0] ?? null
}

async function uploadFirmware() {
  if (!otaFile.value) return
  otaUploading.value = true
  otaProgress.value = 'Uploading firmware...'
  otaError.value = ''
  try {
    const { BASE_URL } = useApi()
    const res = await fetch(`${BASE_URL}/api/system/ota`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body: otaFile.value,
    })
    if (!res.ok) throw new Error(`Upload failed: ${res.status}`)
    otaProgress.value = 'Firmware updated! Device is restarting...'
    otaFile.value = null
  } catch (e: any) {
    otaError.value = 'Firmware upload failed: ' + e.message
    otaProgress.value = ''
  } finally {
    otaUploading.value = false
  }
}

async function uploadWww() {
  if (!otaFile.value) return
  otaUploading.value = true
  otaProgress.value = 'Uploading web UI...'
  otaError.value = ''
  try {
    const { BASE_URL } = useApi()
    const res = await fetch(`${BASE_URL}/api/system/ota/www`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body: otaFile.value,
    })
    if (!res.ok) throw new Error(`Upload failed: ${res.status}`)
    otaProgress.value = 'Web UI updated! Device is restarting...'
    otaFile.value = null
  } catch (e: any) {
    otaError.value = 'Web UI upload failed: ' + e.message
    otaProgress.value = ''
  } finally {
    otaUploading.value = false
  }
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
          step="5"
          class="w-full accent-[#f97316] bg-transparent"
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
          :min="voltMin"
          :max="voltMax"
          step="5"
          class="w-full accent-[#f97316] bg-transparent"
        />
        <div class="flex justify-between text-[10px] text-[var(--text-muted)] font-mono">
          <span>{{ voltMin }}</span>
          <span>{{ voltMax }}</span>
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
        <input v-model.number="fanTargetTemp" type="range" min="40" max="85" class="w-full accent-[#f97316] bg-transparent" />
      </div>
      <div>
        <div class="flex justify-between text-xs mb-1">
          <label class="text-[var(--text-secondary)]">Overheat Temp</label>
          <span class="font-mono text-[var(--text)]">{{ overheatTemp }}&deg;C</span>
        </div>
        <input v-model.number="overheatTemp" type="range" min="70" max="120" class="w-full accent-[#f97316] bg-transparent" />
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
            class="w-full accent-[#f97316] bg-transparent"
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
        <select v-model="defaultMode" class="w-full font-mono">
          <option value="simple">Simple</option>
          <option value="advanced">Advanced</option>
        </select>
      </div>
    </section>

    <!-- Firmware Update (OTA) -->
    <section class="card space-y-3">
      <div class="text-[10px] font-mono text-[var(--text-muted)] uppercase tracking-wider">Firmware Update</div>

      <!-- Check for updates -->
      <div class="flex items-center gap-3">
        <button
          @click="checkForUpdates"
          :disabled="otaChecking"
          class="btn btn-secondary text-xs"
        >
          {{ otaChecking ? 'Checking...' : 'Check for Updates' }}
        </button>
        <span v-if="system.info?.firmware_version" class="text-[10px] font-mono text-[var(--text-muted)]">
          Current: {{ system.info.firmware_version }}
        </span>
      </div>

      <!-- Update info -->
      <div v-if="otaInfo" class="bg-[var(--bg)] rounded p-3 space-y-2">
        <div class="flex items-center justify-between text-xs font-mono">
          <span class="text-[var(--text-secondary)]">Installed</span>
          <span class="text-[var(--text)]">{{ otaInfo.current_version }}</span>
        </div>
        <div class="flex items-center justify-between text-xs font-mono">
          <span class="text-[var(--text-secondary)]">Latest</span>
          <span :class="otaInfo.update_available ? 'text-[#22c55e]' : 'text-[var(--text)]'">
            {{ otaInfo.latest_version }}
          </span>
        </div>
        <div v-if="otaInfo.update_available" class="pt-2 border-t border-[var(--border)] space-y-2">
          <div class="text-[10px] font-mono text-[var(--text-secondary)] mb-1">Download, then use Manual Upload below to flash:</div>
          <div class="flex gap-2">
            <a
              :href="otaInfo.fw_download_url"
              target="_blank"
              class="btn btn-primary text-xs flex-1 text-center"
            >Download Firmware</a>
            <a
              :href="otaInfo.www_download_url"
              target="_blank"
              class="btn btn-secondary text-xs flex-1 text-center"
            >Download Web UI</a>
          </div>
          <div class="text-[9px] font-mono text-[var(--text-muted)]">
            Firmware updates preserve your settings. Web UI updates only change the interface.
          </div>
        </div>
        <div v-else class="text-[10px] font-mono text-[#22c55e]">
          Firmware is up to date.
        </div>
      </div>

      <!-- Manual upload -->
      <div class="border-t border-[var(--border)] pt-3">
        <div class="text-xs text-[var(--text-secondary)] mb-2">Manual Upload</div>
        <div class="flex items-center gap-2">
          <input
            type="file"
            accept=".bin"
            @change="onFileSelected"
            class="text-[11px] font-mono text-[var(--text-secondary)] file:mr-2 file:py-1 file:px-3 file:rounded file:border-0 file:text-[11px] file:font-mono file:bg-[var(--surface-light)] file:text-[var(--text-secondary)] hover:file:text-[var(--text)]"
          />
          <button
            @click="uploadFirmware"
            :disabled="!otaFile || otaUploading"
            class="btn btn-secondary text-xs"
          >
            {{ otaUploading ? 'Uploading...' : 'Flash FW' }}
          </button>
          <button
            @click="uploadWww"
            :disabled="!otaFile || otaUploading"
            class="btn btn-secondary text-xs"
          >
            {{ otaUploading ? 'Uploading...' : 'Flash WWW' }}
          </button>
        </div>
      </div>

      <!-- Progress / status -->
      <div v-if="otaProgress" class="text-[10px] font-mono text-[#3b82f6] animate-pulse">
        {{ otaProgress }}
      </div>
      <div v-if="otaError" class="text-[10px] font-mono text-[#ef4444]">
        {{ otaError }}
      </div>
    </section>

    <!-- Restart required banner -->
    <div
      v-if="restartRequired"
      class="flex items-center justify-between rounded px-4 py-3 text-xs font-mono"
      style="background: rgba(234, 179, 8, 0.15); color: #eab308; border: 1px solid rgba(234, 179, 8, 0.3);"
    >
      <span>Restart required for changes to take effect</span>
      <button @click="restart" class="ml-3 px-3 py-1 rounded text-[11px] font-mono" style="background: rgba(234, 179, 8, 0.25);">
        Restart
      </button>
    </div>

    <!-- Actions -->
    <div class="flex gap-3">
      <button @click="save" :disabled="saving" class="btn btn-primary min-h-[44px]">
        {{ saving ? 'Saving...' : saved ? 'Saved' : 'Save' }}
      </button>
      <button @click="restart" class="btn btn-danger min-h-[44px]">
        {{ confirmRestart ? 'Confirm Restart?' : 'Restart Device' }}
      </button>
    </div>

    <div v-if="error" class="text-xs font-mono text-[#ef4444]">{{ error }}</div>
  </div>
</template>
