<script setup lang="ts">
defineProps<{ class?: string }>()
</script>

<template>
  <div class="flex items-center gap-2" :class="$props.class">
    <!-- Icon: stylized ASIC chip with pickaxe element -->
    <svg width="24" height="24" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
      <!-- Chip body -->
      <rect x="6" y="6" width="12" height="12" rx="1" stroke="#f97316" stroke-width="1.5" fill="none"/>
      <!-- Chip pins - top -->
      <line x1="9" y1="3" x2="9" y2="6" stroke="#f97316" stroke-width="1.5" stroke-linecap="round"/>
      <line x1="12" y1="3" x2="12" y2="6" stroke="#f97316" stroke-width="1.5" stroke-linecap="round"/>
      <line x1="15" y1="3" x2="15" y2="6" stroke="#f97316" stroke-width="1.5" stroke-linecap="round"/>
      <!-- Chip pins - bottom -->
      <line x1="9" y1="18" x2="9" y2="21" stroke="#f97316" stroke-width="1.5" stroke-linecap="round"/>
      <line x1="12" y1="18" x2="12" y2="21" stroke="#f97316" stroke-width="1.5" stroke-linecap="round"/>
      <line x1="15" y1="18" x2="15" y2="21" stroke="#f97316" stroke-width="1.5" stroke-linecap="round"/>
      <!-- Chip pins - left -->
      <line x1="3" y1="9" x2="6" y2="9" stroke="#f97316" stroke-width="1.5" stroke-linecap="round"/>
      <line x1="3" y1="12" x2="6" y2="12" stroke="#f97316" stroke-width="1.5" stroke-linecap="round"/>
      <line x1="3" y1="15" x2="6" y2="15" stroke="#f97316" stroke-width="1.5" stroke-linecap="round"/>
      <!-- Chip pins - right -->
      <line x1="18" y1="9" x2="21" y2="9" stroke="#f97316" stroke-width="1.5" stroke-linecap="round"/>
      <line x1="18" y1="12" x2="21" y2="12" stroke="#f97316" stroke-width="1.5" stroke-linecap="round"/>
      <line x1="18" y1="15" x2="21" y2="15" stroke="#f97316" stroke-width="1.5" stroke-linecap="round"/>
      <!-- Inner die - pickaxe shape -->
      <path d="M9.5 9.5L14.5 14.5M14.5 9.5L12 12" stroke="#f97316" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
    </svg>
    <!-- Text -->
    <span class="font-mono text-base font-bold tracking-tight">
      <span style="color: var(--text)">Asic</span><span class="text-gray-500">OS</span>
    </span>
  </div>
</template>
