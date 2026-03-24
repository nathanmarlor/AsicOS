import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { useApi } from '../composables/useApi'

export interface ChipInfo {
  id: number
  hashrate_ghs: number
  nonces?: number
  rolling_nonces?: number
  hw_errors?: number
  domains?: number[]  // per-domain hashrate in GH/s (4 domains for BM1370)
}

export interface MiningInfo {
  hashrate_ghs: number
  accepted: number
  rejected: number
  best_diff: number
  alltime_best_diff: number
  pool_diff: number
  total_shares: number
  total_nonces: number
  duplicates: number
  chips: ChipInfo[]
  last_share_diff: number
  recent_nonces: { diff: number; submitted: boolean; age_sec: number }[]
}

export interface ShareEntry {
  ts: number
  diff: number
  diff_str: string
  accepted: boolean
  submitted: boolean  // true = met pool difficulty, false = below pool diff
}

export const useMiningStore = defineStore('mining', () => {
  const { get } = useApi()
  const info = ref<MiningInfo | null>(null)
  const error = ref<string | null>(null)
  const allShares = ref<ShareEntry[]>([])
  const submittedShares = ref<ShareEntry[]>([])
  // Keep legacy shares ref pointing to allShares for backward compat
  const shares = allShares
  let timer: ReturnType<typeof setInterval> | null = null
  let prevAccepted = 0

  const totalHashrate = computed(() => info.value?.hashrate_ghs ?? 0)

  function addShare(entry: ShareEntry) {
    allShares.value.unshift(entry)
    if (allShares.value.length > 100) {
      allShares.value = allShares.value.slice(0, 100)
    }
    if (entry.submitted) {
      submittedShares.value.unshift(entry)
      if (submittedShares.value.length > 50) {
        submittedShares.value = submittedShares.value.slice(0, 50)
      }
    }
  }

  let prevNonces = 0

  let polling = false
  async function poll() {
    if (polling) return
    polling = true
    try {
      const prev = info.value
      const data = await get<MiningInfo>('/api/mining/info')
      info.value = data
      error.value = null

      if (info.value) {
        const recent = info.value.recent_nonces ?? []

        if (prevAccepted === 0 && submittedShares.value.length === 0) {
          // First load: populate from server's ring buffer with real timestamps
          const now = Date.now()
          for (let i = 0; i < Math.min(recent.length, 15); i++) {
            addShare({
              ts: now - (recent[i].age_sec ?? 0) * 1000,
              diff: recent[i].diff,
              diff_str: formatDiff(recent[i].diff),
              accepted: true,
              submitted: true,
            })
          }
        } else {
          // Incremental: add new shares since last poll
          const newAccepted = info.value.accepted - prevAccepted
          if (newAccepted > 0 && prevAccepted > 0) {
            for (let i = 0; i < Math.min(newAccepted, recent.length); i++) {
              addShare({
                ts: Date.now() - (recent[i].age_sec ?? 0) * 1000,
                diff: recent[i].diff,
                diff_str: formatDiff(recent[i].diff),
                accepted: true,
                submitted: true,
              })
            }
          }
        }
        prevAccepted = info.value.accepted
        prevNonces = info.value.total_nonces
      }
    } catch (e: any) {
      error.value = e.message
    } finally {
      polling = false
    }
  }

  function formatDiff(d: number): string {
    if (d >= 1e12) return (d / 1e12).toFixed(2) + 'T'
    if (d >= 1e9) return (d / 1e9).toFixed(2) + 'G'
    if (d >= 1e6) return (d / 1e6).toFixed(2) + 'M'
    if (d >= 1e3) return (d / 1e3).toFixed(1) + 'K'
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

  return {
    info, error, shares, allShares, submittedShares,
    totalHashrate, start, stop, poll, formatDiff
  }
})
