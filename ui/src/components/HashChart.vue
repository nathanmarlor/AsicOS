<script setup lang="ts">
import { computed } from 'vue'

const props = withDefaults(defineProps<{
  data: number[]
  color?: string
  height?: number
  showGrid?: boolean
  label?: string
}>(), {
  color: '#f97316',
  height: 200,
  showGrid: true,
})

const W = 600
const PAD = { top: 10, right: 10, bottom: 24, left: 48 }

const chartW = computed(() => W - PAD.left - PAD.right)
const chartH = computed(() => props.height - PAD.top - PAD.bottom)

const stats = computed(() => {
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

const gradientId = computed(() => `grad-${props.label?.replace(/\s/g, '') || 'hash'}`)
</script>

<template>
  <div class="w-full">
    <svg
      :viewBox="`0 0 ${W} ${height}`"
      class="w-full block"
      preserveAspectRatio="xMidYMid meet"
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
          stroke="#1e1e1e"
          stroke-width="1"
        />
        <text
          v-for="(g, i) in gridLines"
          :key="'gl' + i"
          :x="PAD.left - 6"
          :y="g.y + 3"
          text-anchor="end"
          fill="#4b4b4b"
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

      <!-- Time label -->
      <text
        :x="PAD.left + chartW"
        :y="height - 4"
        text-anchor="end"
        fill="#4b4b4b"
        font-size="10"
        font-family="monospace"
      >Last 6 min</text>

      <!-- Label -->
      <text
        v-if="label"
        :x="PAD.left"
        :y="height - 4"
        text-anchor="start"
        fill="#6b7280"
        font-size="10"
        font-family="monospace"
        text-transform="uppercase"
        letter-spacing="0.05em"
      >{{ label }}</text>
    </svg>
  </div>
</template>
