<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { useSystemStore } from '../stores/system'

const system = useSystemStore()

// Dismissed state for each alert type
const dismissedOverheat = ref(false)
const dismissedVrFault = ref(false)
const dismissedCrash = ref(false)

// Track previous values to reset dismissed state when condition changes
const prevOverheat = ref(false)
const prevVrFault = ref(false)
const prevResetReason = ref('')

const isOverheat = computed(() => system.info?.power.overheat ?? false)
const isVrFault = computed(() => system.info?.power.vr_fault ?? false)
const resetReason = computed(() => system.info?.reset_reason ?? '')
const isCrashReset = computed(() => {
  const reason = resetReason.value.toLowerCase()
  return reason.includes('panic') || reason.includes('watchdog') || reason.includes('brownout')
})

// Reset dismissed state when condition changes
watch(isOverheat, (val) => {
  if (val !== prevOverheat.value) {
    dismissedOverheat.value = false
    prevOverheat.value = val
  }
})

watch(isVrFault, (val) => {
  if (val !== prevVrFault.value) {
    dismissedVrFault.value = false
    prevVrFault.value = val
  }
})

watch(resetReason, (val) => {
  if (val !== prevResetReason.value) {
    dismissedCrash.value = false
    prevResetReason.value = val
  }
})

const showOverheat = computed(() => isOverheat.value && !dismissedOverheat.value)
const showVrFault = computed(() => isVrFault.value && !dismissedVrFault.value)
const showCrash = computed(() => isCrashReset.value && !dismissedCrash.value)
const hasAlerts = computed(() => showOverheat.value || showVrFault.value || showCrash.value)
</script>

<template>
  <div v-if="hasAlerts" class="space-y-1.5">
    <!-- Overheat Alert -->
    <div
      v-if="showOverheat"
      class="flex items-center justify-between px-3 py-2 rounded border font-mono text-[11px]"
      style="background: rgba(239, 68, 68, 0.1); border-color: rgba(239, 68, 68, 0.3);"
    >
      <span class="text-[#ef4444]">
        <strong>OVERHEAT</strong> &mdash; Overheat Protection Active &ndash; Mining disabled. ASIC temp or VR temp exceeded safe limits.
      </span>
      <button
        @click="dismissedOverheat = true"
        class="ml-3 shrink-0 text-[#ef4444] hover:text-[#fca5a5] transition-colors text-sm leading-none px-1 min-w-[44px] min-h-[44px] flex items-center justify-center"
        aria-label="Dismiss overheat alert"
      >&times;</button>
    </div>

    <!-- VR Fault Alert -->
    <div
      v-if="showVrFault"
      class="flex items-center justify-between px-3 py-2 rounded border font-mono text-[11px]"
      style="background: rgba(239, 68, 68, 0.1); border-color: rgba(239, 68, 68, 0.3);"
    >
      <span class="text-[#ef4444]">
        <strong>VR FAULT</strong> &mdash; Voltage Regulator Fault &ndash; Check power supply and connections.
      </span>
      <button
        @click="dismissedVrFault = true"
        class="ml-3 shrink-0 text-[#ef4444] hover:text-[#fca5a5] transition-colors text-sm leading-none px-1 min-w-[44px] min-h-[44px] flex items-center justify-center"
        aria-label="Dismiss VR fault alert"
      >&times;</button>
    </div>

    <!-- Crash Reset Alert -->
    <div
      v-if="showCrash"
      class="flex items-center justify-between px-3 py-2 rounded border font-mono text-[11px]"
      style="background: rgba(234, 179, 8, 0.1); border-color: rgba(234, 179, 8, 0.3);"
    >
      <span class="text-[#eab308]">
        <strong>WARNING</strong> &mdash; Last restart was due to: {{ resetReason }}
      </span>
      <button
        @click="dismissedCrash = true"
        class="ml-3 shrink-0 text-[#eab308] hover:text-[#fde047] transition-colors text-sm leading-none px-1 min-w-[44px] min-h-[44px] flex items-center justify-center"
        aria-label="Dismiss crash alert"
      >&times;</button>
    </div>
  </div>
</template>
