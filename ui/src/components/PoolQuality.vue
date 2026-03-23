<script setup lang="ts">
defineProps<{
  connected: boolean
  url: string
  difficulty: string
  reconnects: number
  worker: string
}>()
</script>

<template>
  <div class="flex flex-col gap-2">
    <div class="text-[10px] font-mono text-[#6b7280] uppercase tracking-wider">Pool Connection</div>

    <!-- Connection status -->
    <div class="flex items-center gap-2">
      <span
        class="w-2 h-2 rounded-full shrink-0"
        :class="connected ? 'bg-[#22c55e]' : 'bg-[#ef4444]'"
      />
      <span
        class="text-xs font-mono font-medium"
        :class="connected ? 'text-[#22c55e]' : 'text-[#ef4444]'"
      >{{ connected ? 'Connected' : 'Disconnected' }}</span>
    </div>

    <!-- URL -->
    <div class="text-[11px] font-mono text-[#e5e5e5] break-all leading-relaxed">
      stratum+tcp://{{ url }}
    </div>

    <!-- Stats -->
    <div class="space-y-1.5 text-[11px] font-mono mt-1">
      <div class="flex justify-between">
        <span class="text-[#6b7280]">Difficulty</span>
        <span class="text-[#e5e5e5]">{{ difficulty }}</span>
      </div>
      <div class="flex justify-between">
        <span class="text-[#6b7280]">Reconnects</span>
        <span :class="reconnects > 0 ? 'text-[#eab308]' : 'text-[#e5e5e5]'">{{ reconnects }}</span>
      </div>
      <div class="flex justify-between">
        <span class="text-[#6b7280]">Worker</span>
        <span class="text-[#e5e5e5] truncate ml-4 max-w-[180px]" :title="worker">{{ worker }}</span>
      </div>
    </div>
  </div>
</template>
