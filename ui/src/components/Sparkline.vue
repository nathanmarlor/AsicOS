<script setup lang="ts">
import { computed } from 'vue'

const props = withDefaults(defineProps<{
  data: number[]
  color?: string
  height?: number
  width?: number
}>(), {
  color: '#f97316',
  height: 24,
  width: 80,
})

const points = computed(() => {
  if (props.data.length < 2) return ''
  const d = props.data
  const min = Math.min(...d)
  const max = Math.max(...d)
  const range = max - min || 1
  const stepX = props.width / (d.length - 1)
  const pad = 2
  const h = props.height - pad * 2

  return d.map((v, i) => {
    const x = i * stepX
    const y = pad + h - ((v - min) / range) * h
    return `${x.toFixed(1)},${y.toFixed(1)}`
  }).join(' ')
})

const areaPath = computed(() => {
  if (props.data.length < 2) return ''
  const d = props.data
  const min = Math.min(...d)
  const max = Math.max(...d)
  const range = max - min || 1
  const stepX = props.width / (d.length - 1)
  const pad = 2
  const h = props.height - pad * 2

  const pts = d.map((v, i) => {
    const x = i * stepX
    const y = pad + h - ((v - min) / range) * h
    return `${x.toFixed(1)},${y.toFixed(1)}`
  })

  return `M0,${props.height} L${pts.join(' L')} L${props.width},${props.height} Z`
})
</script>

<template>
  <svg
    :width="width"
    :height="height"
    :viewBox="`0 0 ${width} ${height}`"
    class="block"
    preserveAspectRatio="none"
  >
    <path
      v-if="areaPath"
      :d="areaPath"
      :fill="color"
      fill-opacity="0.05"
    />
    <polyline
      v-if="points"
      :points="points"
      fill="none"
      :stroke="color"
      stroke-width="1.5"
      stroke-linejoin="round"
      stroke-linecap="round"
    />
  </svg>
</template>
