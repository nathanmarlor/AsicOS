import { defineStore } from 'pinia'
import { ref, onUnmounted } from 'vue'
import { useApi } from '../composables/useApi'

export interface SystemInfo {
  board_name: string
  asic_model: string
  expected_chips: number
  hashrate_ghs: number
  per_chip_hashrate_ghs: number[]
  chip_count: number
  temps: { chip: number; vr: number; board: number }
  power: { vin: number; vout: number; watts: number; fan0_rpm: number; fan1_rpm: number; overheat: boolean; vr_fault: boolean }
  mining: { best_difficulty: number; total_shares_submitted: number; duplicate_nonces: number }
  pool: { state: string; accepted: number; rejected: number; difficulty: number }
  config: { pool_url: string; pool_port: number; pool_user: string; frequency: number; voltage: number; wifi_ssid: string; ui_mode: string }
  uptime_ms: number
  free_heap: number
}

export const useSystemStore = defineStore('system', () => {
  const { get } = useApi()
  const info = ref<SystemInfo | null>(null)
  const error = ref<string | null>(null)
  let timer: ReturnType<typeof setInterval> | null = null

  async function poll() {
    try {
      info.value = await get<SystemInfo>('/api/system/info')
      error.value = null
    } catch (e: any) {
      error.value = e.message
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

  return { info, error, start, stop, poll }
})
