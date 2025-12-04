<script setup>
import { ref } from 'vue'

const connected = ref(false)
const connecting = ref(false)
const error = ref('')

// "Alarm armed" from UI point of view (we assume MCU accepted it)
const alarmArmed = ref(false)

// 12-hour UI fields
const currentHour12 = ref(12)
const currentMinute = ref(0)
const currentAmPm = ref('AM')

const alarmHour12 = ref(7)
const alarmMinute = ref(0)
const alarmAmPm = ref('AM')

const P2P_SERVICE_UUID    = '0000fe40-cc7a-482a-984a-7f2ed5b3e58f'
const P2P_WRITE_CHAR_UUID = '0000fe41-8e22-4541-9d4c-21edae82ed19'

// Will hold bluetooth references after connect()
let device = null
let writeChar = null

// Convert 24h -> 12h + AM/PM
function from24To12(hour24) {
  let h = hour24 % 24
  const ampm = h >= 12 ? 'PM' : 'AM'
  h = h % 12
  if (h === 0) h = 12
  return { h, ampm }
}

// Convert 12h + AM/PM -> 24h
function to24Hour(hour12, ampm) {
  let h = Number(hour12) || 0
  h = Math.min(12, Math.max(1, h)) // clamp 1–12

  if (ampm === 'AM') {
    if (h === 12) h = 0            // 12 AM -> 0
  } else { // PM
    if (h !== 12) h += 12          // 1–11 PM -> 13–23, 12 PM stays 12
  }
  return h
}

// Fill current time from browser when component loads (in 12-hour format)
;(function initCurrentTimeFromBrowser() {
  const d = new Date()
  const { h, ampm } = from24To12(d.getHours())
  currentHour12.value = h
  currentMinute.value = d.getMinutes()
  currentAmPm.value = ampm
})()

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
      alarmArmed.value = false
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

function syncWithBrowserTime() {
  const d = new Date()
  const { h, ampm } = from24To12(d.getHours())
  currentHour12.value = h
  currentMinute.value = d.getMinutes()
  currentAmPm.value = ampm
}

function clampTime() {
  // Clamp UI 12h fields & minutes
  let ch = Number(currentHour12.value)  || 1
  let cm = Number(currentMinute.value)  || 0
  let ah = Number(alarmHour12.value)    || 1
  let am = Number(alarmMinute.value)    || 0

  ch = Math.min(12, Math.max(1, ch))
  ah = Math.min(12, Math.max(1, ah))
  cm = Math.min(59, Math.max(0, cm))
  am = Math.min(59, Math.max(0, am))

  currentHour12.value  = ch
  currentMinute.value  = cm
  alarmHour12.value    = ah
  alarmMinute.value    = am

  // AM/PM defaults in case something weird happens
  if (currentAmPm.value !== 'AM' && currentAmPm.value !== 'PM') {
    currentAmPm.value = 'AM'
  }
  if (alarmAmPm.value !== 'AM' && alarmAmPm.value !== 'PM') {
    alarmAmPm.value = 'AM'
  }
}

async function setTimeAndAlarm() {
  if (!connected.value || !writeChar) {
    error.value = 'Not connected to device.'
    return
  }

  clampTime()

  const ch24 = to24Hour(currentHour12.value, currentAmPm.value)
  const cm   = currentMinute.value
  const ah24 = to24Hour(alarmHour12.value, alarmAmPm.value)
  const am   = alarmMinute.value

  try {
    // Payload expected by STM32:
    // p[0] = current hour   (0–23)
    // p[1] = current minute (0–59)
    // p[2] = alarm hour     (0–23)
    // p[3] = alarm minute   (0–59)
    const payload = new Uint8Array([ch24, cm, ah24, am])

    if (typeof writeChar.writeValueWithoutResponse === 'function') {
      await writeChar.writeValueWithoutResponse(payload)
    } else {
      await writeChar.writeValue(payload)
    }

    alarmArmed.value = true
    error.value = ''
  } catch (e) {
    console.error(e)
    error.value = e?.message || String(e)
  }
}
</script>

<template>
  <section class="card">
    <header class="card-header">
      <h2>Bluetooth Clock / Alarm</h2>
      <div class="status-pill" :class="connected ? 'ok' : 'bad'">
        <span class="dot" />
        <span>{{ connected ? 'Connected' : 'Disconnected' }}</span>
      </div>
    </header>

    <p class="hint">
      1) Click Connect and choose your STM32 BLE device.<br />
      2) Adjust the current time and alarm time below.<br />
      3) Click "Set Time &amp; Alarm" to program the clock.
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

    <div class="time-grid">
      <div class="time-group">
        <h3>Current time (MCU clock)</h3>
        <div class="time-inputs">
          <label>
            Hour
            <input
              type="number"
              v-model.number="currentHour12"
              min="1"
              max="12"
            />
          </label>
          <span class="colon">:</span>
          <label>
            Minute
            <input
              type="number"
              v-model.number="currentMinute"
              min="0"
              max="59"
            />
          </label>
          <label>
            AM/PM
            <select v-model="currentAmPm">
              <option value="AM">AM</option>
              <option value="PM">PM</option>
            </select>
          </label>
        </div>
        <button
          class="btn mini"
          type="button"
          @click="syncWithBrowserTime"
        >
          Sync with browser time
        </button>
      </div>

      <div class="time-group">
        <h3>Alarm time</h3>
        <div class="time-inputs">
          <label>
            Hour
            <input
              type="number"
              v-model.number="alarmHour12"
              min="1"
              max="12"
            />
          </label>
          <span class="colon">:</span>
          <label>
            Minute
            <input
              type="number"
              v-model.number="alarmMinute"
              min="0"
              max="59"
            />
          </label>
          <label>
            AM/PM
            <select v-model="alarmAmPm">
              <option value="AM">AM</option>
              <option value="PM">PM</option>
            </select>
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
    </div>

    <p class="led-state">
      Alarm status (UI): <span :class="alarmArmed ? 'on' : 'off'">
        {{ alarmArmed ? 'Armed (LED ON on board)' : 'Not armed' }}
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
