<!-- App.vue -->
<script setup>
import BuzzerControl from './components/BuzzerControl.vue'
// or './components/BuzzerControl.vue' if you keep it in /components
</script>

<template>
  <main class="app-shell">
    <header class="app-header">
      <h1>STM32 BLE LED Control</h1>
      <p>Use Web Bluetooth to toggle the onboard LED</p>
    </header>

    <section class="content">
      <BuzzerControl />
    </section>
  </main>
</template>

<style>
:root {
  font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  color-scheme: dark light;
}

body {
  margin: 0;
}

.app-shell {
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  background: radial-gradient(circle at top left, #1f2937 0, #020617 42%, #020617 100%);
  color: #e5e7eb;
}

.app-header {
  text-align: center;
  background: rgba(15, 23, 42, 0.95);
  color: #fff;
  padding: 20px 16px;
  box-shadow: 0 2px 8px rgba(0,0,0,.35);
}

.app-header h1 {
  margin: 0;
  font-size: 1.6rem;
  letter-spacing: 0.04em;
}

.app-header p {
  margin: 4px 0 0;
  opacity: 0.85;
}

.content {
  flex: 1;
  display: grid;
  place-items: start center;
  padding: 24px;
}
</style>
