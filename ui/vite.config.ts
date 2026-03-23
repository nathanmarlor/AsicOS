import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import { mockApiPlugin } from './src/mock/mockServer'

const useMock = process.env.MOCK === '1' || process.argv.includes('--mock')

export default defineConfig({
  plugins: [
    vue(),
    ...(useMock ? [mockApiPlugin()] : []),
  ],
  define: {
    'import.meta.env.VITE_MOCK': JSON.stringify(useMock ? '1' : '0'),
  },
  base: './',
  build: {
    outDir: '../build/www',
    emptyOutDir: true,
    rollupOptions: {
      output: { manualChunks: undefined }
    }
  },
  server: {
    ...(!useMock ? {
      proxy: { '/api': 'http://192.168.4.1', '/metrics': 'http://192.168.4.1' }
    } : {})
  }
})
