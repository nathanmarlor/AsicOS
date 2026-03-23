import { defineStore } from 'pinia'
import { ref } from 'vue'
import { useApi } from '../composables/useApi'

export interface TunerResult {
  frequency: number
  voltage: number
  hashrate: number
  power: number
  efficiency: number
  temp: number
  stable: boolean
  score: number
}

export interface TunerStatus {
  running: boolean
  progress: number
  step: number
  total_steps: number
  mode: string
  results: TunerResult[]
  best_index: number
  complete: boolean
}

export const useTunerStore = defineStore('tuner', () => {
  const { get, post } = useApi()
  const status = ref<TunerStatus | null>(null)
  const error = ref<string | null>(null)
  let timer: ReturnType<typeof setInterval> | null = null

  async function poll() {
    try {
      status.value = await get<TunerStatus>('/api/tuner/status')
      error.value = null

      if (status.value?.running && !timer) {
        timer = setInterval(poll, 2000)
      } else if (!status.value?.running && timer) {
        clearInterval(timer)
        timer = null
      }
    } catch (e: any) {
      error.value = e.message
    }
  }

  async function start(mode: string) {
    try {
      await post('/api/tuner/start', { mode })
      error.value = null
      poll()
      if (!timer) {
        timer = setInterval(poll, 2000)
      }
    } catch (e: any) {
      error.value = e.message
    }
  }

  async function abort() {
    try {
      await post('/api/tuner/abort')
      error.value = null
      poll()
    } catch (e: any) {
      error.value = e.message
    }
  }

  async function applyBest() {
    try {
      await post('/api/tuner/apply')
      error.value = null
    } catch (e: any) {
      error.value = e.message
    }
  }

  function stopPolling() {
    if (timer) {
      clearInterval(timer)
      timer = null
    }
  }

  return { status, error, poll, start, abort, applyBest, stopPolling }
})
