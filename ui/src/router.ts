import { createRouter, createWebHashHistory } from 'vue-router'

const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    { path: '/', redirect: '/simple' },
    { path: '/simple', component: () => import('./views/SimpleView.vue') },
    { path: '/advanced', component: () => import('./views/AdvancedView.vue') },
    { path: '/tuner', component: () => import('./views/TunerView.vue') },
    { path: '/settings', component: () => import('./views/SettingsView.vue') },
    { path: '/remote', component: () => import('./views/RemoteView.vue') }
  ]
})

export default router
