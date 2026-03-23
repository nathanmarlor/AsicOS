<script setup lang="ts">
import { onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import ModeToggle from './components/ModeToggle.vue'
import { useSystemStore } from './stores/system'
import { useMiningStore } from './stores/mining'

const router = useRouter()
const system = useSystemStore()
const mining = useMiningStore()

onMounted(() => {
  system.start()
  mining.start()
})

onUnmounted(() => {
  system.stop()
  mining.stop()
})
</script>

<template>
  <div class="min-h-screen bg-[#0a0a0a] text-gray-200 font-sans">
    <!-- Top bar -->
    <header class="h-10 flex items-center justify-between px-4 border-b border-border bg-surface select-none">
      <router-link to="/" class="flex items-center gap-2 text-sm font-mono font-medium text-gray-300 hover:text-white transition-colors">
        <span class="text-accent font-bold">Asic</span><span>OS</span>
        <span v-if="system.info" class="text-[10px] text-gray-600 ml-1">{{ system.info.board_name }}</span>
      </router-link>

      <div class="flex items-center gap-3">
        <ModeToggle />
        <router-link to="/settings" class="text-gray-500 hover:text-gray-300 transition-colors text-xs font-mono">
          settings
        </router-link>
      </div>
    </header>

    <!-- Content -->
    <main>
      <router-view />
    </main>
  </div>
</template>
