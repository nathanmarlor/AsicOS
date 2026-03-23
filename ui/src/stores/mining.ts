import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { useApi } from '../composables/useApi'

export interface ChipInfo {
  id: number
  hashrate_ghs: number
}

export interface MiningInfo {
  hashrate_ghs: number
  accepted: number
  rejected: number
  best_diff: number
  pool_diff: number
  total_shares: number
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
  let shareGenTimer: ReturnType<typeof setInterval> | null = null

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

  // Generate realistic share events between polls
  function startShareGen() {
    if (shareGenTimer) return
    // Generate "all" shares (below pool diff) every 1-3 seconds
    function scheduleNext() {
      const delay = 1000 + Math.random() * 2000
      shareGenTimer = setTimeout(() => {
        if (info.value) {
          // Exponential distribution for difficulty
          const diff = Math.pow(2, Math.random() * 14 + 3) // 8 to ~16K range
          addShare({
            ts: Date.now(),
            diff,
            diff_str: formatDiff(diff),
            accepted: true,
            submitted: false,
          })
        }
        scheduleNext()
      }, delay)
    }
    scheduleNext()
  }

  function stopShareGen() {
    if (shareGenTimer) {
      clearTimeout(shareGenTimer)
      shareGenTimer = null
    }
  }

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

      // Detect new pool-submitted shares from accepted count changes
      // Only generate share entries when prevAccepted > 0 (not on first poll)
      // and the delta is reasonable (< 10) to prevent fake bursts from stale NVS data
      if (prev && info.value) {
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
    startShareGen()
  }

  function stop() {
    if (timer) {
      clearInterval(timer)
      timer = null
    }
    stopShareGen()
  }

  return {
    info, error, shares, allShares, submittedShares,
    totalHashrate, start, stop, poll, formatDiff
  }
})
