import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { useApi } from '../composables/useApi'

export interface ChipInfo {
  id: number
  hashrate: number
  frequency: number
  voltage: number
  temp: number
}

export interface MiningInfo {
  is_mining: boolean
  hashrate_ghs: number
  shares_accepted: number
  shares_rejected: number
  best_diff: number
  best_diff_str: string
  pool_url: string
  pool_port: number
  pool_user: string
  pool_connected: boolean
  pool_difficulty: number
  chips: ChipInfo[]
  efficiency: number
}

export interface ShareEntry {
  ts: number
  diff: number
  diff_str: string
  accepted: boolean
}

export const useMiningStore = defineStore('mining', () => {
  const { get } = useApi()
  const info = ref<MiningInfo | null>(null)
  const error = ref<string | null>(null)
  const shares = ref<ShareEntry[]>([])
  let timer: ReturnType<typeof setInterval> | null = null
  let prevAccepted = 0

  const totalHashrate = computed(() => info.value?.hashrate_ghs ?? 0)

  async function poll() {
    try {
      const prev = info.value
      info.value = await get<MiningInfo>('/api/mining/info')
      error.value = null

      // Detect new shares
      if (prev && info.value) {
        const newAccepted = info.value.shares_accepted - prevAccepted
        if (newAccepted > 0 && prevAccepted > 0) {
          shares.value.unshift({
            ts: Date.now(),
            diff: info.value.pool_difficulty,
            diff_str: formatDiff(info.value.pool_difficulty),
            accepted: true
          })
          if (shares.value.length > 50) {
            shares.value = shares.value.slice(0, 50)
          }
        }
      }
      if (info.value) {
        prevAccepted = info.value.shares_accepted
      }
    } catch (e: any) {
      error.value = e.message
    }
  }

  function formatDiff(d: number): string {
    if (d >= 1e12) return (d / 1e12).toFixed(2) + 'T'
    if (d >= 1e9) return (d / 1e9).toFixed(2) + 'G'
    if (d >= 1e6) return (d / 1e6).toFixed(2) + 'M'
    if (d >= 1e3) return (d / 1e3).toFixed(2) + 'K'
    return d.toFixed(0)
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

  return { info, error, shares, totalHashrate, start, stop, poll, formatDiff }
})
