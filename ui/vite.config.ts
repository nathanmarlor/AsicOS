import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

export default defineConfig({
  plugins: [vue()],
  base: './',
  build: {
    outDir: '../build/www',
    emptyOutDir: true,
    rollupOptions: {
      output: { manualChunks: undefined }
    }
  },
  server: {
    proxy: { '/api': 'http://192.168.4.1', '/metrics': 'http://192.168.4.1' }
  }
})
