<!-- BuzzerControl.vue -->
<script setup>
import { ref } from 'vue'

const connected   = ref(false)
const connecting  = ref(false)
const error       = ref('')

// UI flag – just for display
const alarmArmed  = ref(false)

// 24-hour UI fields (0–23)
const currentHour   = ref(0)
const currentMinute = ref(0)

const alarmHour     = ref(7)
const alarmMinute   = ref(0)

// Live sensor data (null = not received yet)
const voltage1 = ref(null)   // V
const current1 = ref(null)   // mA
const voltage2 = ref(null)   // V
const current2 = ref(null)   // mA

// UUIDs from nRF Connect / ST P2P example
const P2P_SERVICE_UUID      = '0000fe40-cc7a-482a-984a-7f2ed5b3e58f'
const P2P_WRITE_CHAR_UUID   = '0000fe41-8e22-4541-9d4c-21edae82ed19' // time/alarm (write)
const P2P_NOTIFY_CHAR_UUID  = '0000fe42-8e22-4541-9d4c-21edae82ed19' // sensors (notify)

// Will hold bluetooth references after connect()
let device     = null
let writeChar  = null
let notifyChar = null

// Initialize current time from browser (UI only)
;(function initCurrentTimeFromBrowser () {
  const d = new Date()
  currentHour.value   = d.getHours()   // 0–23
  currentMinute.value = d.getMinutes() // 0–59
})()

async function connect () {
  error.value = ''
  connecting.value = true

  try {
    if (!navigator.bluetooth) {
      throw new Error('Web Bluetooth not supported in this browser.')
    }

    device = await navigator.bluetooth.requestDevice({
      acceptAllDevices: true,
      optionalServices: [P2P_SERVICE_UUID]
    })

    const server  = await device.gatt.connect()
    const service = await server.getPrimaryService(P2P_SERVICE_UUID)

    // Write characteristic for TIME / ALARM
    writeChar     = await service.getCharacteristic(P2P_WRITE_CHAR_UUID)

    // Notify characteristic for sensor data
    notifyChar    = await service.getCharacteristic(P2P_NOTIFY_CHAR_UUID)
    await notifyChar.startNotifications()
    notifyChar.addEventListener('characteristicvaluechanged', handleNotify)
    console.log('Started notifications on', P2P_NOTIFY_CHAR_UUID)

    connected.value = true

    device.addEventListener('gattserverdisconnected', () => {
      console.log('Device disconnected')
      connected.value  = false
      writeChar        = null
      notifyChar       = null
      alarmArmed.value = false

      // Clear sensor values on disconnect
      voltage1.value = null
      current1.value = null
      voltage2.value = null
      current2.value = null
    })

    // On connect: sync from browser and send TIME to MCU
    await syncFromBrowser(false)
  } catch (e) {
    console.error(e)
    error.value    = e?.message || String(e)
    connected.value = false
    writeChar       = null
    notifyChar      = null
  } finally {
    connecting.value = false
  }
}

// Handle notifications from P2P_NOTIFY_CHAR_UUID
// Expected payload for sensors (8 bytes, little-endian int16_t):
// [ mv1_L, mv1_H, ma1_L, ma1_H, mv2_L, mv2_H, ma2_L, ma2_H ]
function handleNotify (event) {
  const dv  = event.target.value
  const len = dv.byteLength

  // Dump raw bytes every time for debugging
  const raw = []
  for (let i = 0; i < len; i++) {
    raw.push(dv.getUint8(i))
  }
  console.log('Notify len =', len, 'bytes =', raw)

  // Normal sensor frame: 8 bytes
  if (len >= 8) {
    const mv1 = dv.getInt16(0, true)
    const ma1 = dv.getInt16(2, true)
    const mv2 = dv.getInt16(4, true)
    const ma2 = dv.getInt16(6, true)

    // mV → V, current stays in mA
    voltage1.value = mv1 / 1000.0
    current1.value = ma1
    voltage2.value = mv2 / 1000.0
    current2.value = ma2

    console.log(
      'Sensors:',
      'ch1', voltage1.value, 'V', current1.value, 'mA',
      'ch2', voltage2.value, 'V', current2.value, 'mA'
    )
    return
  }

  // Old 2-byte button payload — log and ignore for now
  if (len === 2) {
    const devSel = dv.getUint8(0)
    const btn    = dv.getUint8(1)
    console.log('Legacy 2-byte notify (device, button) =', devSel, btn)
    return
  }

  console.warn('Notify with unexpected length, ignoring:', len)
}

// Sync UI fields from browser clock.
// uiOnly = true  -> just update UI
// uiOnly = false -> update UI and send TIME to MCU
async function syncFromBrowser (uiOnly = true) {
  const d = new Date()
  currentHour.value   = d.getHours()
  currentMinute.value = d.getMinutes()

  if (!uiOnly && connected.value && writeChar) {
    await writeTimeToMcu(currentHour.value, currentMinute.value)
  }
}

// Clamp helpers
function clampFields () {
  let ch = Number(currentHour.value)
  let cm = Number(currentMinute.value)
  let ah = Number(alarmHour.value)
  let am = Number(alarmMinute.value)

  if (Number.isNaN(ch)) ch = 0
  if (Number.isNaN(cm)) cm = 0
  if (Number.isNaN(ah)) ah = 0
  if (Number.isNaN(am)) am = 0

  if (ch < 0 || ch > 23) ch = 0
  if (cm < 0 || cm > 59) cm = 0
  if (ah < 0 || ah > 23) ah = 0
  if (am < 0 || am > 59) am = 0

  currentHour.value   = ch
  currentMinute.value = cm
  alarmHour.value     = ah
  alarmMinute.value   = am
}

// Helper: TIME frame – 2-byte write: [hour, minute] with bit7 = 0
async function writeTimeToMcu (hour24, minute) {
  if (!connected.value || !writeChar) {
    error.value = 'Not connected to device.'
    return
  }

  let h = Number(hour24)
  let m = Number(minute)

  if (Number.isNaN(h)) h = 0
  if (Number.isNaN(m)) m = 0

  if (h < 0 || h > 23) h = 0
  if (m < 0 || m > 59) m = 0

  const payload = new Uint8Array([
    (h & 0x7f),      // bit7 = 0 → TIME
    (m & 0xff)
  ])

  console.log('Write TIME (2-byte):', h, m, payload)

  try {
    if (typeof writeChar.writeValueWithoutResponse === 'function') {
      await writeChar.writeValueWithoutResponse(payload)
    } else {
      await writeChar.writeValue(payload)
    }
    error.value = ''
  } catch (e) {
    console.error(e)
    error.value = e?.message || String(e)
  }
}

// Helper: ALARM frame – 2-byte write: [hour|0x80, minute]
async function writeAlarmToMcu (hour24, minute) {
  if (!connected.value || !writeChar) {
    error.value = 'Not connected to device.'
    return
  }

  let h = Number(hour24)
  let m = Number(minute)

  if (Number.isNaN(h)) h = 0
  if (Number.isNaN(m)) m = 0

  if (h < 0 || h > 23) h = 0
  if (m < 0 || m > 59) m = 0

  const cmdHour = (h & 0x7f) | 0x80  // bit7 = 1 → ALARM

  const payload = new Uint8Array([
    cmdHour & 0xff,
    m & 0xff
  ])

  console.log('Write ALARM (2-byte):', h, m, payload)

  try {
    if (typeof writeChar.writeValueWithoutResponse === 'function') {
      await writeChar.writeValueWithoutResponse(payload)
    } else {
      await writeChar.writeValue(payload)
    }
    error.value = ''
  } catch (e) {
    console.error(e)
    error.value = e?.message || String(e)
  }
}

// Main button: send TIME then ALARM (both 2-byte frames)
async function setTimeAndAlarm () {
  if (!connected.value || !writeChar) {
    error.value = 'Not connected to device.'
    return
  }

  clampFields()

  const ch = currentHour.value
  const cm = currentMinute.value
  const ah = alarmHour.value
  const am = alarmMinute.value

  console.log('setTimeAndAlarm -> TIME:', ch, cm, 'ALARM:', ah, am)

  await writeTimeToMcu(ch, cm)
  await writeAlarmToMcu(ah, am)

  alarmArmed.value = true
}

// Extra debug: send fixed 12:34 as TIME-only
async function sendTest1234 () {
  await writeTimeToMcu(12, 34)
}
</script>

<template>
  <section class="card">
    <header class="card-header">
      <h2>Bluetooth Clock / Alarm + Sensors</h2>
      <div class="status-pill" :class="connected ? 'ok' : 'bad'">
        <span class="dot" />
        <span>{{ connected ? 'Connected' : 'Disconnected' }}</span>
      </div>
    </header>

    <p class="hint">
      1) Click Connect and choose your STM32 BLE device.<br>
      2) Time will automatically sync from your computer clock to the MCU.<br>
      3) Optionally adjust time/alarm and press “Set Time &amp; Alarm”.<br>
      4) Live voltage and current are streamed from the INA219 sensors.
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
    </div>

    <div class="banner" v-if="connected">
      Connected
    </div>
    <div class="banner banner-off" v-else>
      Not connected
    </div>

    <div class="time-grid">
      <div class="time-group">
        <h3>Current time (MCU clock)</h3>
        <div class="time-inputs">
          <label>
            Hour (0–23)
            <input
              type="number"
              v-model.number="currentHour"
              min="0"
              max="23"
            >
          </label>
          <span class="colon">:</span>
          <label>
            Minute
            <input
              type="number"
              v-model.number="currentMinute"
              min="0"
              max="59"
            >
          </label>
        </div>
        <button
          class="btn mini"
          type="button"
          @click="syncFromBrowser(true)"
        >
          Sync with browser time (UI only)
        </button>
      </div>

      <div class="time-group">
        <h3>Alarm time (sent to MCU)</h3>
        <div class="time-inputs">
          <label>
            Hour (0–23)
            <input
              type="number"
              v-model.number="alarmHour"
              min="0"
              max="23"
            >
          </label>
          <span class="colon">:</span>
          <label>
            Minute
            <input
              type="number"
              v-model.number="alarmMinute"
              min="0"
              max="59"
            >
          </label>
        </div>
      </div>
    </div>

    <div class="controls">
      <button
        class="btn primary"
        type="button"
        :disabled="!connected"
        @click="setTimeAndAlarm"
      >
        Set Time &amp; Alarm
      </button>

      <button
        class="btn mini"
        type="button"
        :disabled="!connected"
        @click="sendTest1234"
      >
        Send test 12:34 (time only)
      </button>
    </div>

    <p class="led-state">
      Alarm status (UI): <span :class="alarmArmed ? 'on' : 'off'">
        {{ alarmArmed ? 'Armed (MCU alarm set)' : 'Not armed' }}
      </span>
    </p>

    <!-- Sensor display -->
    <div class="sensors" v-if="connected">
      <h3>Power measurements (INA219)</h3>
      <div class="sensor-grid">
        <div class="sensor-card">
          <h4>Channel 1</h4>
          <p>
            Voltage:
            <span>
              {{ voltage1 !== null ? voltage1.toFixed(3) + ' V' : '—' }}
            </span>
          </p>
          <p>
            Current:
            <span>
              {{ current1 !== null ? current1.toFixed(1) + ' mA' : '—' }}
            </span>
          </p>
        </div>
        <div class="sensor-card">
          <h4>Channel 2</h4>
          <p>
            Voltage:
            <span>
              {{ voltage2 !== null ? voltage2.toFixed(3) + ' V' : '—' }}
            </span>
          </p>
          <p>
            Current:
            <span>
              {{ current2 !== null ? current2.toFixed(1) + ' mA' : '—' }}
            </span>
          </p>
        </div>
      </div>
      <p class="hint sensors-hint">
        Values update once per second from the MCU.
      </p>
    </div>

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
  max-width: 920px;
  margin: 2rem auto;
  padding: 1.75rem 2rem;
  border-radius: 18px;
  background: #111827;
  color: #e5e7eb;
  box-shadow: 0 18px 40px rgba(0, 0, 0, 0.45);
  font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 1rem;
}

.status-pill {
  display: inline-flex;
  align-items: center;
  gap: 0.35rem;
  padding: 0.3rem 0.9rem;
  border-radius: 999px;
  font-size: 0.8rem;
}
.status-pill.ok {
  background: rgba(16, 185, 129, 0.15);
  color: #6ee7b7;
}
.status-pill.bad {
  background: rgba(248, 113, 113, 0.15);
  color: #fecaca;
}
.status-pill .dot {
  width: 8px;
  height: 8px;
  border-radius: 999px;
  background: currentColor;
}

.hint,
.note {
  font-size: 0.85rem;
  color: #9ca3af;
}

.banner {
  margin-top: 0.5rem;
  padding: 0.35rem 0.75rem;
  border-radius: 999px;
  text-align: center;
  font-size: 0.8rem;
  background: rgba(55, 65, 81, 0.8);
  color: #e5e7eb;
}
.banner-off {
  opacity: 0.7;
}

.controls {
  margin: 1rem 0;
  display: flex;
  gap: 0.5rem;
  flex-wrap: wrap;
}

.btn {
  border: none;
  border-radius: 999px;
  padding: 0.5rem 1.2rem;
  cursor: pointer;
  font-size: 0.9rem;
  transition: transform 0.08s ease, box-shadow 0.08s ease, opacity 0.1s ease;
}
.btn.primary {
  background: #2563eb;
  color: #e5e7eb;
}
.btn.primary:hover:enabled {
  transform: translateY(-1px);
  box-shadow: 0 6px 16px rgba(37, 99, 235, 0.45);
}
.btn.secondary {
  background: #374151;
  color: #e5e7eb;
}
.btn.secondary:hover:enabled {
  transform: translateY(-1px);
  box-shadow: 0 6px 16px rgba(15, 23, 42, 0.6);
}
.btn.mini {
  font-size: 0.8rem;
  padding: 0.3rem 0.9rem;
}
.btn:disabled {
  opacity: 0.5;
  cursor: default;
  box-shadow: none;
  transform: none;
}

.time-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
  gap: 1.5rem;
  margin: 1.25rem 0;
}

.time-group h3 {
  margin-bottom: 0.5rem;
  font-size: 0.98rem;
}

.time-inputs {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 0.5rem;
}

.time-inputs label {
  display: flex;
  flex-direction: column;
  font-size: 0.8rem;
}

.time-inputs input {
  margin-top: 0.2rem;
  padding: 0.3rem 0.5rem;
  border-radius: 6px;
  border: 1px solid #4b5563;
  background: #020617;
  color: #e5e7eb;
  width: 4.2rem;
}

.time-inputs input:focus {
  outline: none;
  border-color: #2563eb;
}

.colon {
  font-size: 1.3rem;
  padding: 0 0.2rem;
}

.led-state {
  font-size: 0.9rem;
  margin-top: 0.25rem;
}
.led-state .on {
  color: #6ee7b7;
}
.led-state .off {
  color: #fca5a5;
}

/* Sensor section */
.sensors {
  margin-top: 1.5rem;
}

.sensor-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 1rem;
  margin-top: 0.75rem;
}

.sensor-card {
  padding: 0.9rem 1rem;
  border-radius: 12px;
  background: #020617;
  border: 1px solid #1f2937;
}

.sensor-card h4 {
  margin-bottom: 0.4rem;
  font-size: 0.92rem;
  color: #e5e7eb;
}

.sensor-card p {
  margin: 0.15rem 0;
  font-size: 0.86rem;
  color: #d1d5db;
}

.sensor-card span {
  font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
}

.sensors-hint {
  margin-top: 0.4rem;
}

.error {
  margin-top: 0.5rem;
  color: #fecaca;
  font-size: 0.85rem;
}
</style>
