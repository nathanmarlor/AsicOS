<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { useApi } from '../composables/useApi'

const { get, post } = useApi()

const status = ref<any>(null)
const error = ref('')
const licenceKey = ref('')
const activating = ref(false)

onMounted(async () => {
  try {
    status.value = await get('/api/remote/status')
  } catch (e: any) {
    error.value = e.message
  }
})

async function activate() {
  if (!licenceKey.value.trim()) return
  activating.value = true
  error.value = ''
  try {
    status.value = await post('/api/remote/activate', { licence_key: licenceKey.value.trim() })
    licenceKey.value = ''
  } catch (e: any) {
    error.value = e.message
  } finally {
    activating.value = false
  }
}
</script>

<template>
  <div class="max-w-lg mx-auto px-4 py-6 space-y-6">
    <div class="text-sm font-mono text-[var(--text-secondary)]">Remote Access</div>

    <!-- Status card -->
    <div class="card space-y-3">
      <div class="text-[10px] font-mono text-[var(--text-muted)] uppercase tracking-wider">Status</div>

      <div v-if="status" class="space-y-2 text-xs font-mono">
        <div class="flex justify-between">
          <span class="text-[var(--text-secondary)]">Connection</span>
          <span class="flex items-center gap-1.5">
            <span class="w-1.5 h-1.5 rounded-full" :class="status.connected ? 'bg-green-500' : 'bg-[var(--text-muted)]'" />
            <span :class="status.connected ? 'text-green-500' : 'text-[var(--text-secondary)]'">
              {{ status.connected ? 'connected' : 'disconnected' }}
            </span>
          </span>
        </div>
        <div class="flex justify-between">
          <span class="text-[var(--text-secondary)]">Device ID</span>
          <span class="text-[var(--text)]">{{ status.device_id || '--' }}</span>
        </div>
        <div class="flex justify-between">
          <span class="text-[var(--text-secondary)]">Licensed</span>
          <span :class="status.licensed ? 'text-green-500' : 'text-[var(--text-secondary)]'">
            {{ status.licensed ? 'yes' : 'no' }}
          </span>
        </div>
      </div>

      <div v-else-if="!error" class="text-xs font-mono text-[var(--text-muted)]">Loading...</div>
    </div>

    <!-- Licence activation -->
    <div v-if="status && !status.licensed" class="card space-y-3">
      <div class="text-[10px] font-mono text-[var(--text-muted)] uppercase tracking-wider">Activate Licence</div>
      <div class="flex gap-2">
        <input
          v-model="licenceKey"
          type="text"
          placeholder="Enter licence key"
          class="flex-1"
        />
        <button @click="activate" :disabled="activating" class="btn btn-primary">
          {{ activating ? '...' : 'Activate' }}
        </button>
      </div>
    </div>

    <!-- Info -->
    <div class="text-xs text-[var(--text-muted)] leading-relaxed">
      Remote access allows you to monitor and control this device from anywhere via
      the AsicOS cloud relay. A valid licence key is required. Your device connects
      outbound to the relay server -- no port forwarding needed.
    </div>

    <div v-if="error" class="text-xs font-mono text-red-400">{{ error }}</div>
  </div>
</template>
