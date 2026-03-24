import { defineStore } from 'pinia'
import { ref } from 'vue'
import { useApi } from '../composables/useApi'

export interface SystemInfo {
  board_name: string
  asic_model: string
  expected_chips: number
  hashrate_ghs: number
  per_chip_hashrate_ghs: number[]
  chip_count: number
  has_adc_vcore: boolean
  firmware_version: string
  temps: { chip: number; vr: number; board: number }
  power: { vin: number; iin: number; vout: number; iout: number; watts: number; input_watts: number; efficiency_pct: number; fan0_rpm: number; fan1_rpm: number; fan0_pct: number; fan1_pct: number; fan_override: number; fan_mode: string; overheat: boolean; vr_fault: boolean; vr_temp: number; vcore_adc_mv: number }
  mining: { best_difficulty: number; total_shares_submitted: number; duplicate_nonces: number; hw_errors: number; hw_error_rate: number }
  pool: { state: string; accepted: number; rejected: number; difficulty: number; rtt_ms: number; block_height: number; blocks_found: number; share_rate: number; reject_reasons: { job_not_found: number; duplicate: number; low_difficulty: number; other: number } }
  config: { pool_url: string; pool_port: number; pool_user: string; frequency: number; voltage: number; wifi_ssid: string; ui_mode: string }
  uptime_ms: number
  free_heap: number
  wifi_rssi: number
  cpu_usage?: number
  reset_reason?: string
}

const HISTORY_SIZE = 120

export const useSystemStore = defineStore('system', () => {
  const { get } = useApi()
  const info = ref<SystemInfo | null>(null)
  const error = ref<string | null>(null)
  let timer: ReturnType<typeof setInterval> | null = null

  // Ring buffers for sparklines (120 points = 6 min at 3s poll)
  const hashrateHistory = ref<number[]>([])
  const chipTempHistory = ref<number[]>([])
  const powerHistory = ref<number[]>([])
  const vrTempHistory = ref<number[]>([])
  const efficiencyHistory = ref<number[]>([])

  function pushHistory(arr: number[], val: number) {
    arr.push(val)
    if (arr.length > HISTORY_SIZE) arr.shift()
  }

  let polling = false
  async function poll() {
    if (polling) return
    polling = true
    try {
      const data = await get<SystemInfo>('/api/system/info')
      info.value = data
      error.value = null

      if (info.value) {
        pushHistory(hashrateHistory.value, info.value.hashrate_ghs)
        pushHistory(chipTempHistory.value, info.value.temps.chip)
        pushHistory(powerHistory.value, info.value.power.watts)
        pushHistory(vrTempHistory.value, info.value.temps.vr)
        // J/TH = watts / (hashrate_ghs / 1000)
        const eff = (info.value.power.watts > 0 && info.value.hashrate_ghs > 0)
          ? info.value.power.watts / (info.value.hashrate_ghs / 1000) : 0
        pushHistory(efficiencyHistory.value, eff)
      }
    } catch (e: any) {
      error.value = e.message
    } finally {
      polling = false
    }
  }

  function start() {
    if (timer) return
    poll()
    timer = setInterval(poll, 3000)
  }

  function stop() {
    if (timer) {
      clearInterval(timer)
      timer = null
    }
  }

  return {
    info, error, start, stop, poll,
    hashrateHistory, chipTempHistory, powerHistory, vrTempHistory, efficiencyHistory
  }
})
