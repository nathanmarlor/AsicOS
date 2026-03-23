<script setup lang="ts">
defineProps<{
  url: string
  connected: boolean
}>()
</script>

<template>
  <div class="flex items-center gap-2 text-xs font-mono">
    <span
      class="w-1.5 h-1.5 rounded-full shrink-0"
      :class="connected ? 'bg-green-500' : 'bg-red-500'"
    />
    <span class="text-[var(--text-secondary)] truncate">{{ url }}</span>
    <span :class="connected ? 'text-[#22c55e]' : 'text-[#ef4444]'"
      {{ connected ? 'connected' : 'disconnected' }}
    </span>
  </div>
</template>
