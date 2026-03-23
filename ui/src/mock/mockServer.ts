import type { Plugin } from 'vite'

const MOCK_SYSTEM_INFO = {
  board: "NerdQAxe++",
  asic_model: "BM1370",
  firmware: "AsicOS 1.0.0-dev",
  uptime_s: 86432,
  free_heap: 183240,
  hashrate_ghs: 487.3,
  chip_hashrate: [123.1, 121.8, 119.7, 122.7],
  chip_temp: 54.2,
  vr_temp: 43.8,
  board_temp: 37.1,
  power_w: 15.8,
  voltage_mv: 1150,
  fan0_rpm: 3240,
  fan1_rpm: 2890,
  overheat: false,
  accepted: 1847,
  rejected: 3,
  best_diff: 2.41e9,
  pool_diff: 512,
  pool_state: "mining",
  pool_url: "public-pool.io:3333",
  pool_user: "bc1qexampleaddress.asicos-worker",
  frequency: 600,
  voltage: 1150,
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
  mode: "balanced",
  progress: 0,
  best_index: -1,
  best_eff_index: -1,
  results: [],
}

const MOCK_REMOTE_STATUS = {
  state: "unlicensed",
  licensed: false,
  device_id: "ASICOS-AABBCCDDEEFF",
}

// Simulate live data variation
function jitter(base: number, pct: number = 0.02): number {
  return base * (1 + (Math.random() - 0.5) * 2 * pct)
}

function getMockSystemInfo() {
  return {
    ...MOCK_SYSTEM_INFO,
    uptime_s: MOCK_SYSTEM_INFO.uptime_s + Math.floor(Date.now() / 1000) % 100000,
    hashrate_ghs: jitter(487.3),
    chip_hashrate: MOCK_SYSTEM_INFO.chip_hashrate.map(h => jitter(h)),
    chip_temp: jitter(54.2, 0.01),
    vr_temp: jitter(43.8, 0.01),
    power_w: jitter(15.8),
    fan0_rpm: Math.round(jitter(3240, 0.03)),
    fan1_rpm: Math.round(jitter(2890, 0.03)),
    accepted: MOCK_SYSTEM_INFO.accepted + Math.floor(Math.random() * 3),
    free_heap: Math.round(jitter(183240, 0.05)),
  }
}

function getMockMiningInfo() {
  const accepted = MOCK_MINING_INFO.accepted + Math.floor(Math.random() * 3)
  return {
    ...MOCK_MINING_INFO,
    hashrate_ghs: jitter(487.3),
    accepted,
    total_shares: accepted + MOCK_MINING_INFO.rejected,
    chips: MOCK_MINING_INFO.chips.map(c => ({ ...c, hashrate_ghs: jitter(c.hashrate_ghs) })),
  }
}

// Simulated share events for WebSocket
let shareCounter = 1847

function generateShareEvent() {
  shareCounter++
  const diff = Math.pow(2, Math.random() * 20 + 5)
  return JSON.stringify({
    type: "share",
    id: shareCounter,
    difficulty: diff,
    timestamp: Date.now(),
    accepted: Math.random() > 0.01,
  })
}

const API_ROUTES: Record<string, () => unknown> = {
  '/api/system/info': getMockSystemInfo,
  '/api/mining/info': getMockMiningInfo,
  '/api/tuner/status': () => MOCK_TUNER_STATUS,
  '/api/remote/status': () => MOCK_REMOTE_STATUS,
}

const POST_ROUTES: Record<string, (body: string) => unknown> = {
  '/api/system': () => ({ status: "ok" }),
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
asicos_shares_accepted_total ${MOCK_SYSTEM_INFO.accepted}
`

export function mockApiPlugin(): Plugin {
  return {
    name: 'asicos-mock-api',
    configureServer(server) {
      // Mock WebSocket
      server.ws.on('connection', (socket) => {
        // Send mock log lines periodically
        const logInterval = setInterval(() => {
          const levels = ['I', 'I', 'I', 'I', 'D', 'W']
          const tags = ['stratum', 'mining', 'hashrate', 'power', 'asic']
          const level = levels[Math.floor(Math.random() * levels.length)]
          const tag = tags[Math.floor(Math.random() * tags.length)]
          const messages: Record<string, string[]> = {
            stratum: ['Share accepted', 'New job received', 'Pool difficulty set to 512'],
            mining: ['Job dispatched to ASICs', 'Extranonce2 incremented'],
            hashrate: [`Hashrate: ${jitter(487.3).toFixed(1)} GH/s`],
            power: [`T:${jitter(54.2, 0.01).toFixed(1)}C P:${jitter(15.8).toFixed(1)}W`],
            asic: ['Nonce found', 'Result processed'],
          }
          const msg = messages[tag][Math.floor(Math.random() * messages[tag].length)]
          socket.send(JSON.stringify({ type: 'log', data: `${level} (${tag}) ${msg}` }))
        }, 2000)

        // Send mock share events
        const shareInterval = setInterval(() => {
          if (Math.random() > 0.3) {
            socket.send(generateShareEvent())
          }
        }, 5000)

        socket.on('close', () => {
          clearInterval(logInterval)
          clearInterval(shareInterval)
        })
      })

      // Mock REST API
      server.middlewares.use((req, res, next) => {
        const url = req.url || ''

        if (url === '/metrics') {
          res.setHeader('Content-Type', 'text/plain; version=0.0.4')
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
          req.on('data', chunk => body += chunk)
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
