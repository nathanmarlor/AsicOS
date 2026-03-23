<script setup lang="ts">
import { onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import ModeToggle from './components/ModeToggle.vue'
import ThemeToggle from './components/ThemeToggle.vue'
import Logo from './components/Logo.vue'
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
  <div class="min-h-screen font-sans" style="background-color: var(--bg); color: var(--text);">
    <!-- Top bar -->
    <header class="h-10 flex items-center justify-between px-4 border-b select-none" style="border-color: var(--border); background-color: var(--surface);">
      <router-link to="/" class="flex items-center gap-2 text-sm font-mono font-medium hover:text-white transition-colors" style="color: var(--text);">
        <Logo />
        <span v-if="system.info" class="text-[10px] ml-1" style="color: var(--text-muted);">{{ system.info.board_name }}</span>
      </router-link>

      <div class="flex items-center gap-3">
        <ModeToggle />
        <ThemeToggle />
        <router-link to="/settings" class="hover:text-gray-300 transition-colors text-xs font-mono" style="color: var(--text-secondary);">
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
