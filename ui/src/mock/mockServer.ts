import type { Plugin, ViteDevServer } from 'vite'
import { WebSocketServer, WebSocket } from 'ws'

const MOCK_SYSTEM_INFO = {
  board_name: "NerdQAxe++",
  asic_model: "BM1370",
  expected_chips: 4,
  hashrate_ghs: 487.3,
  per_chip_hashrate_ghs: [123.1, 121.8, 119.7, 122.7],
  chip_count: 4,
  temps: { chip: 54.2, vr: 43.8, board: 37.1 },
  power: { vin: 12.1, vout: 1.15, watts: 15.8, fan0_rpm: 3240, fan1_rpm: 2890, overheat: false, vr_fault: false },
  mining: { best_difficulty: 2.41e9, total_shares_submitted: 1850, duplicate_nonces: 0 },
  pool: { state: "mining", accepted: 1847, rejected: 3, difficulty: 512 },
  config: { pool_url: "public-pool.io", pool_port: 3333, pool_user: "bc1qexampleaddress.asicos-worker", frequency: 600, voltage: 1150, wifi_ssid: "MyWiFi", ui_mode: "simple" },
  uptime_ms: 86432000,
  free_heap: 183240,
}

const MOCK_MINING_INFO = {
  hashrate_ghs: 487.3,
  accepted: 1847,
  rejected: 3,
  best_diff: 2.41e9,
  pool_diff: 512,
  total_shares: 1850,
  duplicates: 0,
  chips: [
    { id: 0, hashrate_ghs: 123.1 },
    { id: 1, hashrate_ghs: 121.8 },
    { id: 2, hashrate_ghs: 119.7 },
    { id: 3, hashrate_ghs: 122.7 },
  ],
}

const MOCK_TUNER_STATUS = {
  state: "idle",
  progress: 0,
  best_index: -1,
  best_eff_index: -1,
  results: [] as unknown[],
  profiles: {
    eco: null,
    balanced: null,
    power: null,
  },
}

const MOCK_TUNER_STATUS_COMPLETE = {
  state: "complete",
  progress: 100,
  current_step: 12,
  total_steps: 12,
  best_index: 5,
  best_eff_index: 2,
  results: [
    { freq: 400, voltage: 1100, hashrate: 320.5, power: 10.2, temp: 48, efficiency: 31.42, stable: true },
    { freq: 500, voltage: 1100, hashrate: 405.2, power: 12.8, temp: 51, efficiency: 31.66, stable: true },
    { freq: 400, voltage: 1150, hashrate: 325.1, power: 10.8, temp: 49, efficiency: 30.10, stable: true },
    { freq: 500, voltage: 1150, hashrate: 410.8, power: 13.5, temp: 52, efficiency: 30.43, stable: true },
    { freq: 600, voltage: 1150, hashrate: 487.3, power: 15.8, temp: 55, efficiency: 30.84, stable: true },
    { freq: 600, voltage: 1200, hashrate: 495.1, power: 17.2, temp: 57, efficiency: 28.78, stable: true },
    { freq: 700, voltage: 1200, hashrate: 560.2, power: 21.5, temp: 62, efficiency: 26.06, stable: true },
    { freq: 700, voltage: 1250, hashrate: 572.8, power: 23.1, temp: 64, efficiency: 24.80, stable: true },
    { freq: 525, voltage: 1125, hashrate: 430.5, power: 13.1, temp: 50, efficiency: 32.86, stable: true },
    { freq: 550, voltage: 1125, hashrate: 452.1, power: 13.9, temp: 51, efficiency: 32.53, stable: true },
    { freq: 575, voltage: 1150, hashrate: 468.9, power: 14.8, temp: 53, efficiency: 31.68, stable: true },
    { freq: 625, voltage: 1175, hashrate: 502.4, power: 16.5, temp: 56, efficiency: 30.45, stable: true },
  ],
  profiles: {
    eco: { freq: 525, voltage: 1125, hashrate: 430.5, power: 13.1, temp: 50, efficiency: 32.86, stable: true },
    balanced: { freq: 600, voltage: 1150, hashrate: 487.3, power: 15.8, temp: 55, efficiency: 30.84, stable: true },
    power: { freq: 700, voltage: 1250, hashrate: 572.8, power: 23.1, temp: 64, efficiency: 24.80, stable: true },
  },
}

const MOCK_REMOTE_STATUS = {
  state: "unlicensed",
  licensed: false,
  device_id: "ASICOS-AABBCCDDEEFF",
}

function jitter(base: number, pct: number = 0.02): number {
  return base * (1 + (Math.random() - 0.5) * 2 * pct)
}

let mockAccepted = 1847

function getMockSystemInfo() {
  mockAccepted += Math.random() > 0.5 ? 1 : 0
  return {
    ...MOCK_SYSTEM_INFO,
    uptime_ms: (Math.floor(Date.now() / 1000) - 1711100000) * 1000,
    hashrate_ghs: jitter(487.3),
    per_chip_hashrate_ghs: MOCK_SYSTEM_INFO.per_chip_hashrate_ghs.map(h => jitter(h)),
    temps: {
      chip: jitter(54.2, 0.01),
      vr: jitter(43.8, 0.01),
      board: jitter(37.1, 0.01),
    },
    power: {
      ...MOCK_SYSTEM_INFO.power,
      watts: jitter(15.8),
      fan0_rpm: Math.round(jitter(3240, 0.03)),
      fan1_rpm: Math.round(jitter(2890, 0.03)),
    },
    pool: {
      ...MOCK_SYSTEM_INFO.pool,
      accepted: mockAccepted,
    },
    free_heap: Math.round(jitter(183240, 0.05)),
  }
}

function getMockMiningInfo() {
  return {
    ...MOCK_MINING_INFO,
    hashrate_ghs: jitter(487.3),
    accepted: mockAccepted,
    total_shares: mockAccepted + MOCK_MINING_INFO.rejected,
    chips: MOCK_MINING_INFO.chips.map(c => ({ ...c, hashrate_ghs: jitter(c.hashrate_ghs) })),
  }
}

let shareCounter = 1847

// Generate share events with realistic difficulty distribution
// Most shares are below pool difficulty (frequent, small diff)
// Occasionally one meets pool difficulty (submitted)
function generateShareEvent(forceSubmitted: boolean = false) {
  shareCounter++
  const poolDiff = MOCK_MINING_INFO.pool_diff // 512

  let diff: number
  let submitted: boolean

  if (forceSubmitted || Math.random() < 0.08) {
    // Submitted share - meets or exceeds pool difficulty
    diff = poolDiff * (1 + Math.random() * 5)
    submitted = true
  } else {
    // Below pool difficulty - exponential distribution
    diff = Math.pow(2, Math.random() * 8 + 3) // 8 to ~2048 range, mostly below 512
    submitted = false
  }

  return JSON.stringify({
    type: "share",
    id: shareCounter,
    difficulty: diff,
    submitted,
    timestamp: Date.now(),
    accepted: Math.random() > 0.005,
  })
}

const MOCK_SYSTEM_CONFIG = {
  wifi_ssid: "MyWiFi",
  wifi_password: "",
  pool_url: "public-pool.io",
  pool_port: 3333,
  pool_user: "bc1qexampleaddress.asicos-worker",
  pool_password: "x",
  fallback_url: "",
  fallback_port: 3333,
  fallback_user: "",
  frequency: 600,
  core_voltage: 1150,
  fan_target_temp: 65,
  overheat_temp: 95,
  loki_url: "",
  default_mode: "simple",
}

const API_ROUTES: Record<string, () => unknown> = {
  '/api/system/info': getMockSystemInfo,
  '/api/system/config': () => MOCK_SYSTEM_CONFIG,
  '/api/mining/info': getMockMiningInfo,
  '/api/tuner/status': () => MOCK_TUNER_STATUS,
  '/api/remote/status': () => MOCK_REMOTE_STATUS,
}

const POST_ROUTES: Record<string, (body: string) => unknown> = {
  '/api/system': () => ({ status: "ok" }),
  '/api/system/config': () => ({ status: "ok" }),
  '/api/system/restart': () => ({ status: "restarting" }),
  '/api/tuner/start': () => ({ status: "started" }),
  '/api/tuner/abort': () => ({ status: "aborting" }),
  '/api/tuner/apply': () => ({ status: "applied" }),
  '/api/remote/activate': (body: string) => {
    try {
      const { licence_key } = JSON.parse(body)
      return { valid: licence_key === "DEMO-LICENCE-KEY", device_id: "ASICOS-AABBCCDDEEFF" }
    } catch { return { valid: false } }
  },
}

const MOCK_METRICS = () => `# HELP asicos_hashrate_ghs Total hashrate in GH/s
# TYPE asicos_hashrate_ghs gauge
asicos_hashrate_ghs ${jitter(487.3)}

# HELP asicos_chip_temp_celsius Chip temperature
# TYPE asicos_chip_temp_celsius gauge
asicos_chip_temp_celsius ${jitter(54.2, 0.01)}

# HELP asicos_power_watts Power consumption
# TYPE asicos_power_watts gauge
asicos_power_watts ${jitter(15.8)}

# HELP asicos_shares_accepted_total Accepted shares
# TYPE asicos_shares_accepted_total counter
asicos_shares_accepted_total ${mockAccepted}
`

export function mockApiPlugin(): Plugin {
  return {
    name: 'asicos-mock-api',
    configureServer(server: ViteDevServer) {
      // WebSocket server for /api/ws - attach to Vite's HTTP server
      const wss = new WebSocketServer({ noServer: true })

      server.httpServer?.on('upgrade', (request, socket, head) => {
        if (request.url === '/api/ws') {
          wss.handleUpgrade(request, socket, head, (ws) => {
            wss.emit('connection', ws, request)
          })
        }
      })

      wss.on('connection', (ws: WebSocket) => {
        console.log('[mock] WebSocket client connected to /api/ws')

        // Send mock log lines every 2s
        const logInterval = setInterval(() => {
          if (ws.readyState !== WebSocket.OPEN) return
          const levels = ['I', 'I', 'I', 'I', 'D', 'W']
          const tags = ['stratum', 'mining', 'hashrate', 'power', 'asic']
          const level = levels[Math.floor(Math.random() * levels.length)]
          const tag = tags[Math.floor(Math.random() * tags.length)]
          const messages: Record<string, string[]> = {
            stratum: ['Share accepted', 'New job received', 'Pool difficulty set to 512', 'Nonce submitted'],
            mining: ['Job dispatched to ASICs', 'Extranonce2 incremented', 'New block template received'],
            hashrate: [`Hashrate: ${jitter(487.3).toFixed(1)} GH/s`, `Chip 0: ${jitter(123).toFixed(1)} GH/s`],
            power: [`T:${jitter(54.2, 0.01).toFixed(1)}C P:${jitter(15.8).toFixed(1)}W F:${Math.round(jitter(3240, 0.03))}rpm`],
            asic: ['Nonce found', 'Result processed', 'Difficulty target met'],
          }
          const msg = messages[tag][Math.floor(Math.random() * messages[tag].length)]
          ws.send(`${level} (${tag}) ${msg}`)
        }, 2000)

        // Send frequent "all" share events every 1-3s
        const allShareInterval = setInterval(() => {
          if (ws.readyState !== WebSocket.OPEN) return
          ws.send(generateShareEvent(false))
        }, 1000 + Math.random() * 2000)

        // Send occasional "submitted" share events every 10-30s
        const submittedShareInterval = setInterval(() => {
          if (ws.readyState !== WebSocket.OPEN) return
          ws.send(generateShareEvent(true))
        }, 10000 + Math.random() * 20000)

        ws.on('close', () => {
          console.log('[mock] WebSocket client disconnected')
          clearInterval(logInterval)
          clearInterval(allShareInterval)
          clearInterval(submittedShareInterval)
        })
      })

      // REST API middleware
      server.middlewares.use((req, res, next) => {
        const url = req.url || ''

        // CORS
        res.setHeader('Access-Control-Allow-Origin', '*')
        res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        res.setHeader('Access-Control-Allow-Headers', 'Content-Type')

        if (req.method === 'OPTIONS') {
          res.statusCode = 204
          res.end()
          return
        }

        if (url === '/metrics') {
          res.setHeader('Content-Type', 'text/plain; version=0.0.4; charset=utf-8')
          res.end(MOCK_METRICS())
          return
        }

        if (req.method === 'GET' && API_ROUTES[url]) {
          res.setHeader('Content-Type', 'application/json')
          res.end(JSON.stringify(API_ROUTES[url]()))
          return
        }

        if (req.method === 'POST' && POST_ROUTES[url]) {
          let body = ''
          req.on('data', (chunk: Buffer) => body += chunk.toString())
          req.on('end', () => {
            res.setHeader('Content-Type', 'application/json')
            res.end(JSON.stringify(POST_ROUTES[url](body)))
          })
          return
        }

        next()
      })
    }
  }
}
