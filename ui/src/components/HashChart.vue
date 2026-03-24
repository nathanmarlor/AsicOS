<script setup lang="ts">
import { computed, ref } from 'vue'

const props = withDefaults(defineProps<{
  data: number[]
  color?: string
  height?: number
  showGrid?: boolean
  label?: string
  referenceLine?: number
  referenceLabel?: string
  minY?: number
  maxY?: number
  secondData?: number[]
  secondColor?: string
  secondLabel?: string
}>(), {
  color: '#f97316',
  height: 200,
  showGrid: true,
  secondColor: '#3b82f6',
})

const W = 600
const PAD = { top: 10, right: 10, bottom: 24, left: 48 }

const chartW = computed(() => W - PAD.left - PAD.right)
const chartH = computed(() => props.height - PAD.top - PAD.bottom)

const stats = computed(() => {
  if (props.minY != null && props.maxY != null) {
    return { min: props.minY, max: props.maxY, range: props.maxY - props.minY }
  }
  if (props.data.length === 0) return { min: 0, max: 100, range: 100 }
  const min = Math.min(...props.data)
  const max = Math.max(...props.data)
  const pad = (max - min) * 0.1 || 1
  return { min: min - pad, max: max + pad, range: (max + pad) - (min - pad) }
})

const gridLines = computed(() => {
  const { min, range } = stats.value
  const lines: { y: number; label: string }[] = []
  const steps = [0.25, 0.5, 0.75, 1.0]
  for (const s of steps) {
    const val = min + range * s
    const y = PAD.top + chartH.value - (chartH.value * s)
    lines.push({ y, label: val.toFixed(1) })
  }
  return lines
})

function toY(val: number): number {
  const { min, range } = stats.value
  return PAD.top + chartH.value - ((val - min) / range) * chartH.value
}

function toX(i: number): number {
  if (props.data.length <= 1) return PAD.left
  return PAD.left + (i / (props.data.length - 1)) * chartW.value
}

// Smooth cubic bezier path
const smoothPath = computed(() => {
  const d = props.data
  if (d.length < 2) return ''

  const pts = d.map((v, i) => ({ x: toX(i), y: toY(v) }))

  let path = `M${pts[0].x.toFixed(1)},${pts[0].y.toFixed(1)}`

  for (let i = 0; i < pts.length - 1; i++) {
    const p0 = pts[Math.max(0, i - 1)]
    const p1 = pts[i]
    const p2 = pts[i + 1]
    const p3 = pts[Math.min(pts.length - 1, i + 2)]

    const tension = 0.3
    const cp1x = p1.x + (p2.x - p0.x) * tension
    const cp1y = p1.y + (p2.y - p0.y) * tension
    const cp2x = p2.x - (p3.x - p1.x) * tension
    const cp2y = p2.y - (p3.y - p1.y) * tension

    path += ` C${cp1x.toFixed(1)},${cp1y.toFixed(1)} ${cp2x.toFixed(1)},${cp2y.toFixed(1)} ${p2.x.toFixed(1)},${p2.y.toFixed(1)}`
  }

  return path
})

// Second data line (optional overlay, e.g. VRM temp on same chart as ASIC temp)
const secondSmoothPath = computed(() => {
  const d = props.secondData
  if (!d || d.length < 2) return ''
  const pts = d.map((v, i) => ({ x: toX(i), y: toY(v) }))
  let path = `M${pts[0].x.toFixed(1)},${pts[0].y.toFixed(1)}`
  for (let i = 0; i < pts.length - 1; i++) {
    const p0 = pts[Math.max(0, i - 1)]
    const p1 = pts[i]
    const p2 = pts[i + 1]
    const p3 = pts[Math.min(pts.length - 1, i + 2)]
    const tension = 0.3
    const cp1x = p1.x + (p2.x - p0.x) * tension
    const cp1y = p1.y + (p2.y - p0.y) * tension
    const cp2x = p2.x - (p3.x - p1.x) * tension
    const cp2y = p2.y - (p3.y - p1.y) * tension
    path += ` C${cp1x.toFixed(1)},${cp1y.toFixed(1)} ${cp2x.toFixed(1)},${cp2y.toFixed(1)} ${p2.x.toFixed(1)},${p2.y.toFixed(1)}`
  }
  return path
})

const areaPath = computed(() => {
  if (!smoothPath.value) return ''
  const bottomY = PAD.top + chartH.value
  const lastX = toX(props.data.length - 1)
  const firstX = toX(0)
  return `${smoothPath.value} L${lastX.toFixed(1)},${bottomY} L${firstX.toFixed(1)},${bottomY} Z`
})

const lastPoint = computed(() => {
  if (props.data.length === 0) return null
  const i = props.data.length - 1
  return { x: toX(i), y: toY(props.data[i]) }
})

// Hover state
const hovering = ref(false)
const hoverIndex = ref(-1)
const hoverPos = ref({ x: 0, y: 0 })

function onMouseMove(e: MouseEvent | TouchEvent) {
  const svg = e.currentTarget as SVGSVGElement
  const rect = svg.getBoundingClientRect()
  const clientX = 'touches' in e ? e.touches[0].clientX : (e as MouseEvent).clientX
  const svgX = ((clientX - rect.left) / rect.width) * W

  if (props.data.length < 2) return
  const relX = Math.max(0, Math.min(1, (svgX - PAD.left) / chartW.value))
  const idx = Math.round(relX * (props.data.length - 1))

  hoverIndex.value = idx
  hoverPos.value = { x: toX(idx), y: toY(props.data[idx]) }
  hovering.value = true
}

function onMouseLeave() {
  hovering.value = false
  hoverIndex.value = -1
}

const hoverValue = computed(() => {
  if (hoverIndex.value < 0 || hoverIndex.value >= props.data.length) return null
  return props.data[hoverIndex.value]
})

const hoverSecondValue = computed(() => {
  if (!props.secondData || hoverIndex.value < 0 || hoverIndex.value >= props.secondData.length) return null
  return props.secondData[hoverIndex.value]
})

const hoverTimeAgo = computed(() => {
  if (hoverIndex.value < 0) return ''
  const totalPoints = props.data.length
  const pointsFromEnd = totalPoints - 1 - hoverIndex.value
  const secondsAgo = pointsFromEnd * 3
  if (secondsAgo < 60) return `${secondsAgo}s ago`
  return `${Math.floor(secondsAgo / 60)}m ${secondsAgo % 60}s ago`
})

// Unique gradient ID - strip all non-alphanumeric chars to avoid SVG url() issues
const _uid = Math.random().toString(36).slice(2, 8)
const gradientId = computed(() => `grad-${_uid}`)
</script>

<template>
  <div class="w-full">
    <svg
      :viewBox="`0 0 ${W} ${height}`"
      class="w-full block"
      preserveAspectRatio="xMidYMid meet"
      @mousemove="onMouseMove"
      @mouseleave="onMouseLeave"
      @touchmove.prevent="onMouseMove"
      @touchend="onMouseLeave"
    >
      <defs>
        <linearGradient :id="gradientId" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" :stop-color="color" stop-opacity="0.15" />
          <stop offset="100%" :stop-color="color" stop-opacity="0.01" />
        </linearGradient>
      </defs>

      <!-- Grid lines -->
      <template v-if="showGrid">
        <line
          v-for="(g, i) in gridLines"
          :key="'g' + i"
          :x1="PAD.left"
          :y1="g.y"
          :x2="PAD.left + chartW"
          :y2="g.y"
          :stroke="'var(--border)'"
          stroke-width="1"
        />
        <text
          v-for="(g, i) in gridLines"
          :key="'gl' + i"
          :x="PAD.left - 6"
          :y="g.y + 3"
          text-anchor="end"
          :fill="'var(--text-muted)'"
          font-size="10"
          font-family="monospace"
        >{{ g.label }}</text>
      </template>

      <!-- Area fill -->
      <path
        v-if="areaPath"
        :d="areaPath"
        :fill="`url(#${gradientId})`"
      />

      <!-- Line -->
      <path
        v-if="smoothPath"
        :d="smoothPath"
        fill="none"
        :stroke="color"
        stroke-width="2"
        stroke-linejoin="round"
        stroke-linecap="round"
      />

      <!-- Second line (optional) -->
      <path
        v-if="secondSmoothPath"
        :d="secondSmoothPath"
        fill="none"
        :stroke="secondColor"
        stroke-width="2"
        stroke-linejoin="round"
        stroke-linecap="round"
      />

      <!-- Current value dot -->
      <circle
        v-if="lastPoint"
        :cx="lastPoint.x"
        :cy="lastPoint.y"
        r="3"
        :fill="color"
      />
      <circle
        v-if="lastPoint"
        :cx="lastPoint.x"
        :cy="lastPoint.y"
        r="5"
        :fill="color"
        fill-opacity="0.3"
      />

      <!-- Reference line -->
      <template v-if="referenceLine != null">
        <line
          :x1="PAD.left"
          :y1="toY(referenceLine)"
          :x2="PAD.left + chartW"
          :y2="toY(referenceLine)"
          stroke="var(--text-muted)"
          stroke-width="1"
          stroke-dasharray="6,4"
          opacity="0.7"
        />
        <text
          v-if="referenceLabel"
          :x="PAD.left + chartW - 2"
          :y="toY(referenceLine) - 4"
          text-anchor="end"
          fill="var(--text-muted)"
          font-size="9"
          font-family="monospace"
          opacity="0.8"
        >{{ referenceLabel }} {{ referenceLine.toLocaleString() }}</text>
      </template>

      <!-- Time label -->
      <text
        :x="PAD.left + chartW"
        :y="height - 4"
        text-anchor="end"
        :fill="'var(--text-muted)'"
        font-size="10"
        font-family="monospace"
      >Last 6 min</text>

      <!-- Label -->
      <text
        v-if="label"
        :x="PAD.left"
        :y="height - 4"
        text-anchor="start"
        :fill="'var(--text-secondary)'"
        font-size="10"
        font-family="monospace"
        text-transform="uppercase"
        letter-spacing="0.05em"
      >{{ label }}</text>

      <!-- Hover crosshair + tooltip -->
      <template v-if="hovering && hoverValue != null">
        <line
          :x1="hoverPos.x" :y1="PAD.top"
          :x2="hoverPos.x" :y2="PAD.top + chartH"
          stroke="var(--text-muted)" stroke-width="1" stroke-dasharray="3,3" opacity="0.5"
        />
        <circle :cx="hoverPos.x" :cy="hoverPos.y" r="4" :fill="color" />
        <!-- Second dot if dual data -->
        <circle v-if="hoverSecondValue != null" :cx="hoverPos.x" :cy="toY(hoverSecondValue)" r="4" :fill="secondColor" />
        <!-- Tooltip box (taller if dual) -->
        <rect
          :x="hoverPos.x + (hoverPos.x > W/2 ? -100 : 10)"
          :y="Math.max(PAD.top + 4, hoverPos.y - 32)"
          :width="hoverSecondValue != null ? 90 : 80"
          :height="hoverSecondValue != null ? 36 : 26" rx="3"
          fill="var(--surface)" stroke="var(--border)" stroke-width="1"
        />
        <!-- Primary value -->
        <text
          :x="hoverPos.x + (hoverPos.x > W/2 ? -55 : 55)"
          :y="Math.max(PAD.top + 4, hoverPos.y - 32) + 13"
          text-anchor="middle"
          :fill="color" font-size="11" font-family="monospace"
        >{{ hoverValue.toFixed(1) }}{{ label?.includes('TEMP') ? '°' : '' }}</text>
        <!-- Second value -->
        <text v-if="hoverSecondValue != null"
          :x="hoverPos.x + (hoverPos.x > W/2 ? -55 : 55)"
          :y="Math.max(PAD.top + 4, hoverPos.y - 32) + 25"
          text-anchor="middle"
          :fill="secondColor" font-size="11" font-family="monospace"
        >{{ hoverSecondValue.toFixed(1) }}{{ label?.includes('TEMP') ? '°' : '' }}</text>
        <!-- Time -->
        <text
          :x="hoverPos.x + (hoverPos.x > W/2 ? -55 : 55)"
          :y="Math.max(PAD.top + 4, hoverPos.y - 32) + (hoverSecondValue != null ? 35 : 23)"
          text-anchor="middle"
          fill="var(--text-muted)" font-size="9" font-family="monospace"
        >{{ hoverTimeAgo }}</text>
      </template>
    </svg>
  </div>
</template>
