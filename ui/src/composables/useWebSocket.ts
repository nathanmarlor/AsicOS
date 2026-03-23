import { ref, onUnmounted } from 'vue'

const WS_BASE = import.meta.env.VITE_MOCK === '1'
  ? `ws://${location.host}`
  : (import.meta.env.DEV ? 'ws://192.168.4.1' : `ws://${location.host}`)

export interface LogEntry {
  ts: number
  level: string
  msg: string
}

const logs = ref<LogEntry[]>([])
const allLogs = ref<LogEntry[]>([])
const connected = ref(false)

let ws: WebSocket | null = null
let reconnectTimer: ReturnType<typeof setTimeout> | null = null
let refCount = 0

function connect() {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return

  ws = new WebSocket(`${WS_BASE}/api/ws`)

  ws.onopen = () => {
    connected.value = true
  }

  ws.onmessage = (evt) => {
    try {
      const data = JSON.parse(evt.data)
      const entry: LogEntry = {
        ts: data.ts || Date.now(),
        level: data.level || 'INFO',
        msg: data.msg || evt.data
      }
      allLogs.value.push(entry)
      logs.value.push(entry)
      if (logs.value.length > 200) {
        logs.value = logs.value.slice(-200)
      }
    } catch {
      const fallback: LogEntry = { ts: Date.now(), level: 'INFO', msg: evt.data }
      allLogs.value.push(fallback)
      logs.value.push(fallback)
      if (logs.value.length > 200) {
        logs.value = logs.value.slice(-200)
      }
    }
  }

  ws.onclose = () => {
    connected.value = false
    scheduleReconnect()
  }

  ws.onerror = () => {
    ws?.close()
  }
}

function scheduleReconnect() {
  if (reconnectTimer) return
  if (refCount <= 0) return
  reconnectTimer = setTimeout(() => {
    reconnectTimer = null
    connect()
  }, 3000)
}

export function useWebSocket() {
  refCount++
  if (refCount === 1) connect()

  onUnmounted(() => {
    refCount--
    if (refCount <= 0) {
      if (reconnectTimer) clearTimeout(reconnectTimer)
      reconnectTimer = null
      ws?.close()
      ws = null
    }
  })

  return { logs, allLogs, connected }
}
