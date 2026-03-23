import { defineStore } from 'pinia'
import { ref, onUnmounted } from 'vue'
import { useApi } from '../composables/useApi'

export interface SystemInfo {
  hostname: string
  version: string
  board: string
  asic_count: number
  freq_min: number
  freq_max: number
  frequency: number
  core_voltage: number
  vr_temp: number
  board_temp: number
  chip_temp: number
  fan_speed_0: number
  fan_speed_1: number
  fan_target_temp: number
  overheat_temp: number
  power_watts: number
  free_heap: number
  uptime_seconds: number
  wifi_ssid: string
  wifi_status: string
  ip_address: string
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
