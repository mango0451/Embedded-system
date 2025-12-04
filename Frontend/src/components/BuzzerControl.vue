<!-- BuzzerControl.vue -->
<script setup>
import { ref } from 'vue'

const connected = ref(false)
const connecting = ref(false)
const isOn = ref(false)
const error = ref('')

const P2P_SERVICE_UUID    = '0000fe40-cc7a-482a-984a-7f2ed5b3e58f';
const P2P_WRITE_CHAR_UUID = '0000fe41-8e22-4541-9d4c-21edae82ed19';

// Will hold bluetooth references after connect()
let device = null
let writeChar = null

async function connect() {
  error.value = ''
  connecting.value = true

  try {
    // Open the OS Bluetooth picker
    device = await navigator.bluetooth.requestDevice({
      acceptAllDevices: true,
      optionalServices: [P2P_SERVICE_UUID]
      // You can filter by namePrefix if you want:
      // filters: [{ namePrefix: 'P2P' }]
    })

    const server = await device.gatt.connect()
    const service = await server.getPrimaryService(P2P_SERVICE_UUID)
    writeChar = await service.getCharacteristic(P2P_WRITE_CHAR_UUID)

    connected.value = true

    device.addEventListener('gattserverdisconnected', () => {
      connected.value = false
      writeChar = null
      isOn.value = false
    })
  } catch (e) {
    console.error(e)
    error.value = e?.message || String(e)
    connected.value = false
    writeChar = null
  } finally {
    connecting.value = false
  }
}

async function sendLed(state) {
  if (!connected.value || !writeChar) {
    error.value = 'Not connected to device.'
    return
  }

  try {
    // Matches your firmware:
    // dev = pPayload[0] (0x00 = all devices)
    // state = pPayload[1] (0x01 = ON, 0x00 = OFF)
    const dev = 0x00
    const payload = new Uint8Array([dev, state])

    // Your STM32 handler is in P2PS_STM_WRITE_EVT
    await writeChar.writeValueWithoutResponse(payload)
  } catch (e) {
    console.error(e)
    error.value = e?.message || String(e)
  }
}

async function toggleLed() {
  const next = !isOn.value
  await sendLed(next ? 0x01 : 0x00)
  isOn.value = next
}
</script>

<template>
  <section class="card">
    <header class="card-header">
      <h2>Bluetooth LED Control</h2>
      <div class="status-pill" :class="connected ? 'ok' : 'bad'">
        <span class="dot" />
        <span>{{ connected ? 'Connected' : 'Disconnected' }}</span>
      </div>
    </header>

    <p class="hint">
      1) Click Connect and choose your STM32 BLE device.<br />
      2) Use the button below to toggle the onboard LED.
    </p>

    <div class="controls">
      <button
        class="btn secondary"
        type="button"
        :disabled="connecting || connected"
        @click="connect"
      >
        <span v-if="connecting">Connecting…</span>
        <span v-else-if="connected">Connected</span>
        <span v-else>Connect</span>
      </button>

      <button
        class="btn primary"
        type="button"
        :disabled="!connected"
        @click="toggleLed"
      >
        Turn LED {{ isOn ? 'Off' : 'On' }}
      </button>
    </div>

    <p class="led-state">
      LED state (UI): <span :class="isOn ? 'on' : 'off'">
        {{ isOn ? 'ON' : 'OFF' }}
      </span>
    </p>

    <p v-if="error" class="error">
      {{ error }}
    </p>

    <p class="note">
      Web Bluetooth only works on supported browsers (Chrome / Edge) and requires
      HTTPS or <code>localhost</code>.
    </p>
  </section>
</template>

<style scoped>
.card {
  width: 100%;
  max-width: 420px;
  padding: 20px 18px 18px;
  border-radius: 18px;
  background: rgba(15, 23, 42, 0.92);
  box-shadow: 0 14px 40px rgba(0,0,0,0.55);
  border: 1px solid rgba(148, 163, 184, 0.35);
  backdrop-filter: blur(14px);
}

.card-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 12px;
}

.card-header h2 {
  font-size: 1.1rem;
  margin: 0;
}

.status-pill {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 4px 10px;
  border-radius: 999px;
  font-size: 0.8rem;
  border: 1px solid rgba(148, 163, 184, 0.5);
}

.status-pill.ok {
  background: rgba(16, 185, 129, 0.08);
  border-color: rgba(16, 185, 129, 0.7);
  color: #6ee7b7;
}

.status-pill.bad {
  background: rgba(248, 113, 113, 0.08);
  border-color: rgba(248, 113, 113, 0.7);
  color: #fecaca;
}

.status-pill .dot {
  width: 8px;
  height: 8px;
  border-radius: 999px;
  background: currentColor;
}

.hint {
  margin: 0 0 16px;
  font-size: 0.9rem;
  color: #cbd5f5;
  line-height: 1.4;
}

.controls {
  display: flex;
  flex-direction: column;
  gap: 10px;
  margin-bottom: 10px;
}

.btn {
  padding: 10px 14px;
  border-radius: 10px;
  border: none;
  cursor: pointer;
  font-size: 0.97rem;
  font-weight: 600;
  letter-spacing: 0.02em;
  display: inline-flex;
  align-items: center;
  justify-content: center;
}

.btn.primary {
  background: #38bdf8;
  color: #0b1120;
}

.btn.secondary {
  background: rgba(15, 23, 42, 0.9);
  color: #e5e7eb;
  border: 1px solid rgba(148, 163, 184, 0.7);
}

.btn:disabled {
  opacity: 0.6;
  cursor: default;
}

.btn:not(:disabled):hover {
  filter: brightness(1.05);
}

.btn:active {
  transform: translateY(1px);
}

.led-state {
  margin: 4px 0;
  font-size: 0.92rem;
}

.led-state span.on {
  color: #4ade80;
}

.led-state span.off {
  color: #fca5a5;
}

.error {
  margin: 4px 0;
  color: #fecaca;
  font-size: 0.86rem;
}

.note {
  margin: 6px 0 0;
  font-size: 0.8rem;
  color: #9ca3af;
}
</style>
