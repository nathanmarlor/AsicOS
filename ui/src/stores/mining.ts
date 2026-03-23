import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { useApi } from '../composables/useApi'

export interface ChipInfo {
  id: number
  hashrate_ghs: number
  nonces?: number
  domains?: number[]  // per-domain hashrate in GH/s (4 domains for BM1370)
}

export interface MiningInfo {
  hashrate_ghs: number
  accepted: number
  rejected: number
  best_diff: number
  pool_diff: number
  total_shares: number
  total_nonces: number
  duplicates: number
  chips: ChipInfo[]
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

  async function poll() {
    try {
      const prev = info.value
      const data = await get<MiningInfo>('/api/mining/info')
      if (info.value) {
        Object.assign(info.value, data)
      } else {
        info.value = data
      }
      error.value = null

      if (prev && info.value) {
        // Generate "all" share entries from nonce count changes (every nonce the ASIC returns)
        const newNonces = info.value.total_nonces - prevNonces
        if (newNonces > 0 && prevNonces > 0 && newNonces < 50) {
          for (let i = 0; i < newNonces; i++) {
            // Each nonce meets ASIC difficulty (256) - assign random difficulty around that
            const diff = 256 * (1 + Math.random() * 4)
            addShare({
              ts: Date.now() - i * (3000 / Math.max(newNonces, 1)),
              diff,
              diff_str: formatDiff(diff),
              accepted: true,
              submitted: false,  // Not submitted to pool (below pool diff)
            })
          }
        }

        // Generate "submitted" share entries from accepted count changes
        const newAccepted = info.value.accepted - prevAccepted
        if (newAccepted > 0 && prevAccepted > 0 && newAccepted < 10) {
          for (let i = 0; i < newAccepted; i++) {
            const diff = info.value.pool_diff * (1 + Math.random() * 3)
            addShare({
              ts: Date.now() - i * 100,
              diff,
              diff_str: formatDiff(diff),
              accepted: true,
              submitted: true,
            })
          }
        }
      }
      if (info.value) {
        prevAccepted = info.value.accepted
        prevNonces = info.value.total_nonces
      }
    } catch (e: any) {
      error.value = e.message
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
