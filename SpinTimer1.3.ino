// =============================================================================
//  SpinTimer1 — ESP32 Centrifuge Lid Timer
//  Hardware: ESP32-WROOM-32, PAM8403 amp on GPIO25, Blue LED on GPIO32,
//            NO Reed Switch on GPIO27 (internal pull-up, GND on leg 2)
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "driver/ledc.h"

// ---------------------------------------------------------------------------
// Pin Definitions
// ---------------------------------------------------------------------------
#define PIN_REED     27   // Reed switch (INPUT_PULLUP; LOW = lid closed)
#define PIN_LED      32   // Blue LED (active HIGH through 220Ω)
#define PIN_AUDIO    25   // Audio PWM to PAM8403 INL

// ---------------------------------------------------------------------------
// AP Credentials
// ---------------------------------------------------------------------------
const char* AP_SSID = "SpinTimer1";
const char* AP_PASS = "WashoeZephyr";
const unsigned long AP_TIMEOUT_MS = 5UL * 60UL * 1000UL; // 5 minutes

// ---------------------------------------------------------------------------
// LEDC (PWM) for audio — ESP32 Arduino core 3.x uses pin-based API
// ---------------------------------------------------------------------------
#define AUDIO_LEDC_RES   8          // 8-bit resolution

// ---------------------------------------------------------------------------
// Lid-open-during-countdown behaviour
// ---------------------------------------------------------------------------
enum LidBehaviour { LID_RESET = 0, LID_PAUSE = 1, LID_PAUSE_THEN_RESET = 2 };

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
enum SystemState {
  STATE_AP_MODE,        // WiFi AP up, waiting for config or timeout
  STATE_ARMED,          // WiFi off, waiting for lid to close
  STATE_COUNTING,       // Countdown active
  STATE_PAUSED,         // Countdown paused (lid opened mid-count)
  STATE_ALARM           // Timer expired, alarm sounding + LED flashing
};

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
Preferences prefs;
WebServer   server(80);

SystemState state          = STATE_AP_MODE;
unsigned long apStartMs    = 0;

// Settings (persisted)
int          cfgMinutes    = 5;
int          cfgSeconds    = 0;
int          cfgTone       = 0;        // 0-9
LidBehaviour cfgLidBehav  = LID_RESET;

// Countdown
unsigned long countdownMs  = 0;        // total countdown in ms
unsigned long remainingMs  = 0;        // ms remaining when paused / at start
unsigned long countStartMs = 0;        // millis() when counting last began

// Lid-open-once tracking (for LID_PAUSE_THEN_RESET)
bool lidOpenedOnce = false;

// LED flash
unsigned long ledLastToggleMs = 0;
bool          ledState        = false;

// Alarm tone timing
unsigned long tonePhaseStartMs = 0;
int           tonePhaseIdx     = 0;

// Debounce
bool          lastReedRaw      = HIGH;
bool          reedState        = HIGH;   // HIGH = open, LOW = closed
unsigned long reedDebounceMs   = 0;
#define DEBOUNCE_MS 50

// ---------------------------------------------------------------------------
// Tone definitions — 10 distinct alarm patterns
// Each entry: { frequency_hz, duration_ms }  — 0 Hz = silence
// Patterns loop until lid opens.
// ---------------------------------------------------------------------------
struct ToneNote { uint16_t freq; uint16_t dur; };

// Tone 0 — Classic double-beep
const ToneNote TONE0[] = { {2000,150},{0,80},{2000,150},{0,500} };
// Tone 1 — Rising arpeggio
const ToneNote TONE1[] = { {880,100},{1046,100},{1318,100},{1568,200},{0,400} };
// Tone 2 — Steady alarm
const ToneNote TONE2[] = { {1000,400},{0,200} };
// Tone 3 — Warble
const ToneNote TONE3[] = { {1500,60},{1200,60},{1500,60},{1200,60},{0,300} };
// Tone 4 — Fast triple-beep
const ToneNote TONE4[] = { {2500,100},{0,60},{2500,100},{0,60},{2500,100},{0,500} };
// Tone 5 — Low siren sweep (simulated with steps)
const ToneNote TONE5[] = { {600,80},{700,80},{800,80},{900,80},{1000,80},
                            {900,80},{800,80},{700,80},{0,200} };
// Tone 6 — Ship bell (two strikes)
const ToneNote TONE6[] = { {1760,120},{0,60},{1760,120},{0,800} };
// Tone 7 — Morse SOS feel
const ToneNote TONE7[] = { {1200,80},{0,40},{1200,80},{0,40},{1200,80},{0,120},
                            {1200,240},{0,40},{1200,240},{0,40},{1200,240},{0,120},
                            {1200,80},{0,40},{1200,80},{0,40},{1200,80},{0,400} };
// Tone 8 — Descending scale
const ToneNote TONE8[] = { {1568,120},{1318,120},{1046,120},{880,120},{0,400} };
// Tone 9 — Urgent stutter
const ToneNote TONE9[] = { {3000,50},{0,30},{3000,50},{0,30},{3000,50},{0,30},
                            {3000,50},{0,30},{0,300} };

const ToneNote* TONES[]  = { TONE0,TONE1,TONE2,TONE3,TONE4,
                              TONE5,TONE6,TONE7,TONE8,TONE9 };
const uint8_t   TONE_LEN[]= { 4,    5,    2,    5,    6,
                               9,    4,    19,   5,    10   };

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
void startAP();
void stopWiFi();
void setupWebServer();
void loadPrefs();
void savePrefs();
void playNote(uint16_t freq);
void stopNote();
void setLED(bool on);
void blinkLED(int times, int onMs, int offMs);
void readReed();
void enterArmed();
void enterCounting();
void enterAlarm();
void handleAP();
void handleCounting();
void handlePaused();
void handleAlarm();
void updateAlarmTone();

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Pins
  pinMode(PIN_REED, INPUT_PULLUP);
  pinMode(PIN_LED,  OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // LEDC audio channel (core 3.x API)
  ledcAttach(PIN_AUDIO, 1000, AUDIO_LEDC_RES);
  ledcWrite(PIN_AUDIO, 0);

  // Load saved settings
  loadPrefs();

  // Start AP
  startAP();
  setupWebServer();
  apStartMs = millis();
  state = STATE_AP_MODE;

  Serial.println("SpinTimer1 booted — AP mode active for 5 minutes.");
}

// ---------------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------------
void loop() {
  readReed();

  switch (state) {
    case STATE_AP_MODE:   handleAP();       break;
    case STATE_ARMED:     /* wait for lid */ break;
    case STATE_COUNTING:  handleCounting(); break;
    case STATE_PAUSED:    /* wait for lid */ break;
    case STATE_ALARM:     handleAlarm();    break;
  }

  // --- Lid-close logic (from ARMED or PAUSED) ---
  if (state == STATE_ARMED && reedState == LOW) {
    // Lid just closed → start counting
    enterCounting();
  }

  if (state == STATE_PAUSED && reedState == LOW) {
    // Resume
    countStartMs = millis();
    state = STATE_COUNTING;
    Serial.println("Resuming countdown.");
  }

  // WebServer only active during AP mode
  if (state == STATE_AP_MODE) {
    server.handleClient();
  }
}

// ---------------------------------------------------------------------------
// AP & Web Server
// ---------------------------------------------------------------------------
void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
}

void stopWiFi() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi disabled.");
}

// Web UI — full single-page config interface
const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SpinTimer1</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Orbitron:wght@400;700&display=swap');
  :root{--bg:#0a0f1a;--panel:#111827;--accent:#3b82f6;--accent2:#60a5fa;--text:#e2e8f0;--dim:#64748b;--green:#22c55e;}
  *{box-sizing:border-box;margin:0;padding:0;}
  body{background:var(--bg);color:var(--text);font-family:'Share Tech Mono',monospace;
       min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:24px 16px;}
  h1{font-family:'Orbitron',sans-serif;font-size:1.6rem;color:var(--accent2);
     letter-spacing:.15em;text-transform:uppercase;margin-bottom:4px;}
  .sub{color:var(--dim);font-size:.75rem;letter-spacing:.1em;margin-bottom:28px;}
  .card{background:var(--panel);border:1px solid #1e293b;border-radius:12px;
        padding:22px 24px;width:100%;max-width:420px;margin-bottom:18px;}
  .card h2{font-family:'Orbitron',sans-serif;font-size:.8rem;color:var(--accent);
           letter-spacing:.12em;text-transform:uppercase;margin-bottom:16px;
           border-bottom:1px solid #1e293b;padding-bottom:8px;}
  .row{display:flex;gap:16px;align-items:flex-end;}
  .field{flex:1;display:flex;flex-direction:column;gap:6px;}
  label{font-size:.7rem;color:var(--dim);letter-spacing:.08em;text-transform:uppercase;}
  select{background:#0f172a;border:1px solid #334155;color:var(--text);
         border-radius:6px;padding:10px 8px;font-family:'Share Tech Mono',monospace;
         font-size:.95rem;width:100%;cursor:pointer;appearance:none;text-align:center;}
  select:focus{outline:none;border-color:var(--accent);}
  .tone-grid{display:grid;grid-template-columns:repeat(5,1fr);gap:8px;}
  .tone-btn{background:#0f172a;border:1px solid #334155;color:var(--dim);
            border-radius:6px;padding:10px 4px;font-family:'Share Tech Mono',monospace;
            font-size:.8rem;cursor:pointer;transition:all .15s;text-align:center;}
  .tone-btn:hover{border-color:var(--accent);color:var(--accent);}
  .tone-btn.active{background:var(--accent);border-color:var(--accent);color:#fff;}
  .behav-list{display:flex;flex-direction:column;gap:8px;}
  .behav-opt{display:flex;align-items:flex-start;gap:10px;background:#0f172a;
             border:1px solid #334155;border-radius:6px;padding:10px 12px;
             cursor:pointer;transition:border-color .15s;}
  .behav-opt:hover{border-color:var(--accent);}
  .behav-opt.active{border-color:var(--accent);}
  .behav-opt input{accent-color:var(--accent);margin-top:2px;flex-shrink:0;}
  .behav-label{font-size:.8rem;line-height:1.4;}
  .behav-label strong{display:block;color:var(--text);margin-bottom:2px;}
  .behav-label span{color:var(--dim);font-size:.72rem;}
  .save-btn{width:100%;max-width:420px;padding:14px;background:var(--accent);
            border:none;border-radius:8px;color:#fff;font-family:'Orbitron',sans-serif;
            font-size:.9rem;letter-spacing:.1em;text-transform:uppercase;cursor:pointer;
            transition:background .15s;}
  .save-btn:hover{background:var(--accent2);}
  .msg{display:none;margin-top:14px;padding:10px 16px;border-radius:6px;
       font-size:.8rem;text-align:center;}
  .msg.ok{background:#14532d;color:var(--green);display:block;}
  .msg.err{background:#450a0a;color:#f87171;display:block;}
  .colon{font-size:1.4rem;color:var(--accent);padding-bottom:6px;flex-shrink:0;}
</style>
</head>
<body>
<h1>&#9881; SpinTimer1</h1>
<div class="sub">CENTRIFUGE TIMER CONFIGURATION</div>

<!-- Timer Duration -->
<div class="card">
  <h2>Timer Duration</h2>
  <div class="row">
    <div class="field">
      <label>Minutes</label>
      <select id="mins"></select>
    </div>
    <div class="colon">:</div>
    <div class="field">
      <label>Seconds</label>
      <select id="secs"></select>
    </div>
  </div>
</div>

<!-- Alarm Tone -->
<div class="card">
  <h2>Alarm Tone</h2>
  <div class="tone-grid" id="toneGrid"></div>
</div>

<!-- Lid Behaviour -->
<div class="card">
  <h2>Lid-Open Behaviour</h2>
  <div class="behav-list">
    <label class="behav-opt" id="b0">
      <input type="radio" name="behav" value="0">
      <div class="behav-label">
        <strong>Reset</strong>
        <span>Opening the lid cancels and resets the countdown.</span>
      </div>
    </label>
    <label class="behav-opt" id="b1">
      <input type="radio" name="behav" value="1">
      <div class="behav-label">
        <strong>Pause</strong>
        <span>Opening the lid pauses the countdown; closing resumes it.</span>
      </div>
    </label>
    <label class="behav-opt" id="b2">
      <input type="radio" name="behav" value="2">
      <div class="behav-label">
        <strong>Pause then Reset</strong>
        <span>First lid opening pauses. If closed then opened again, the countdown fully resets.</span>
      </div>
    </label>
  </div>
</div>

<button class="save-btn" onclick="save()">SAVE &amp; ARM TIMER</button>
<div class="msg" id="msg"></div>

<script>
const $ = id => document.getElementById(id);

// Populate dropdowns 0–60
['mins','secs'].forEach(id=>{
  const sel=$( id);
  for(let i=0;i<=60;i++){
    const o=document.createElement('option');
    o.value=i; o.textContent=String(i).padStart(2,'0');
    sel.appendChild(o);
  }
});

// Tone buttons
const toneNames=['Double Beep','Rise','Steady','Warble','Triple','Siren',
                  'Ship Bell','SOS','Descend','Stutter'];
const grid=$('toneGrid');
toneNames.forEach((n,i)=>{
  const b=document.createElement('button');
  b.className='tone-btn'; b.textContent=n; b.dataset.i=i;
  b.onclick=()=>{document.querySelectorAll('.tone-btn').forEach(x=>x.classList.remove('active'));
                  b.classList.add('active');};
  grid.appendChild(b);
});

// Load current values from ESP32
fetch('/config').then(r=>r.json()).then(d=>{
  $('mins').value=d.minutes;
  $('secs').value=d.seconds;
  document.querySelectorAll('.tone-btn')[d.tone].classList.add('active');
  const radio=document.querySelector(`input[name=behav][value="${d.behav}"]`);
  if(radio){radio.checked=true; updateBehavHighlight(d.behav);}
});

// Highlight active behaviour card
document.querySelectorAll('input[name=behav]').forEach(r=>{
  r.addEventListener('change',()=>updateBehavHighlight(r.value));
});
function updateBehavHighlight(v){
  ['b0','b1','b2'].forEach((id,i)=>{
    $(id).classList.toggle('active', String(i)===String(v));
  });
}

function save(){
  const mins=parseInt($('mins').value);
  const secs=parseInt($('secs').value);
  const toneEl=document.querySelector('.tone-btn.active');
  const tone=toneEl?parseInt(toneEl.dataset.i):0;
  const behavEl=document.querySelector('input[name=behav]:checked');
  const behav=behavEl?parseInt(behavEl.value):0;
  fetch('/save',{
    method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:`minutes=${mins}&seconds=${secs}&tone=${tone}&behav=${behav}`
  }).then(r=>r.text()).then(t=>{
    const m=$('msg');
    m.className='msg ok'; m.textContent='✓ Settings saved! Close the lid to start timing.'; 
  }).catch(()=>{
    const m=$('msg');
    m.className='msg err'; m.textContent='Error saving settings.';
  });
}
</script>
</body>
</html>
)rawhtml";

void setupWebServer() {
  // Serve config page
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", HTML_PAGE);
  });

  // Return current config as JSON
  server.on("/config", HTTP_GET, []() {
    String json = "{\"minutes\":" + String(cfgMinutes) +
                  ",\"seconds\":" + String(cfgSeconds) +
                  ",\"tone\":"    + String(cfgTone)    +
                  ",\"behav\":"   + String((int)cfgLidBehav) + "}";
    server.send(200, "application/json", json);
  });

  // Save config
  server.on("/save", HTTP_POST, []() {
    if (server.hasArg("minutes")) cfgMinutes  = constrain(server.arg("minutes").toInt(), 0, 60);
    if (server.hasArg("seconds")) cfgSeconds  = constrain(server.arg("seconds").toInt(), 0, 60);
    if (server.hasArg("tone"))    cfgTone     = constrain(server.arg("tone").toInt(),    0,  9);
    if (server.hasArg("behav"))   cfgLidBehav = (LidBehaviour)constrain(server.arg("behav").toInt(), 0, 2);
    savePrefs();
    server.send(200, "text/plain", "OK");
    Serial.printf("Config saved: %dm %ds, tone %d, behav %d\n",
                  cfgMinutes, cfgSeconds, cfgTone, (int)cfgLidBehav);
  });

  server.begin();
}

// ---------------------------------------------------------------------------
// AP-mode handler — checks timeout and reed switch
// ---------------------------------------------------------------------------
void handleAP() {
  bool timedOut = (millis() - apStartMs) >= AP_TIMEOUT_MS;
  bool lidClosed = (reedState == LOW);

  if (timedOut || lidClosed) {
    stopWiFi();
    if (lidClosed) {
      enterCounting();
    } else {
      enterArmed();
    }
  }
}

// ---------------------------------------------------------------------------
// ARMED state entry — brief 3-blink confirmation
// ---------------------------------------------------------------------------
void enterArmed() {
  state = STATE_ARMED;
  lidOpenedOnce = false;
  Serial.println("State: ARMED — waiting for lid close.");
  blinkLED(3, 120, 120);
}

// ---------------------------------------------------------------------------
// COUNTING state entry
// ---------------------------------------------------------------------------
void enterCounting() {
  countdownMs  = ((unsigned long)cfgMinutes * 60UL + (unsigned long)cfgSeconds) * 1000UL;
  remainingMs  = countdownMs;
  countStartMs = millis();
  lidOpenedOnce = false;
  state = STATE_COUNTING;
  // Brief confirmation blip — lets the user know the timer is running
  setLED(true);  delay(500);  setLED(false);
  Serial.printf("State: COUNTING — %lu ms\n", countdownMs);

  // Edge case: 0:00 → instant alarm
  if (countdownMs == 0) {
    enterAlarm();
  }
}

// ---------------------------------------------------------------------------
// COUNTING handler
// ---------------------------------------------------------------------------
void handleCounting() {
  unsigned long elapsed = millis() - countStartMs;
  if (elapsed >= remainingMs) {
    enterAlarm();
    return;
  }

  // Lid opened mid-count?
  if (reedState == HIGH) {
    switch (cfgLidBehav) {
      case LID_RESET:
        Serial.println("Lid opened — resetting.");
        stopNote();
        enterArmed();
        break;

      case LID_PAUSE:
        remainingMs -= elapsed;
        state = STATE_PAUSED;
        Serial.printf("Lid opened — paused with %lu ms remaining.\n", remainingMs);
        break;

      case LID_PAUSE_THEN_RESET:
        if (!lidOpenedOnce) {
          // First open → pause
          remainingMs -= elapsed;
          lidOpenedOnce = true;
          state = STATE_PAUSED;
          Serial.printf("Lid opened (1st time) — paused with %lu ms remaining.\n", remainingMs);
        } else {
          // Second open (lid was closed then opened again) → reset
          Serial.println("Lid opened (2nd time) — resetting.");
          stopNote();
          enterArmed();
        }
        break;
    }
  }
}

// ---------------------------------------------------------------------------
// ALARM state entry
// ---------------------------------------------------------------------------
void enterAlarm() {
  state = STATE_ALARM;
  tonePhaseIdx     = 0;
  tonePhaseStartMs = millis();
  ledLastToggleMs  = millis();
  ledState         = true;
  setLED(true);
  playNote(TONES[cfgTone][0].freq);
  Serial.println("State: ALARM");
}

// ---------------------------------------------------------------------------
// ALARM handler
// ---------------------------------------------------------------------------
void handleAlarm() {
  // Reset when lid opens
  if (reedState == HIGH) {
    stopNote();
    setLED(false);
    Serial.println("Lid opened — alarm cleared.");
    enterArmed();
    return;
  }

  // Advance tone pattern
  updateAlarmTone();

  // Flash LED every 250 ms
  if (millis() - ledLastToggleMs >= 250) {
    ledState = !ledState;
    setLED(ledState);
    ledLastToggleMs = millis();
  }
}

void updateAlarmTone() {
  const ToneNote* pattern = TONES[cfgTone];
  uint8_t         len     = TONE_LEN[cfgTone];
  unsigned long   dur     = pattern[tonePhaseIdx].dur;

  if (millis() - tonePhaseStartMs >= dur) {
    tonePhaseIdx = (tonePhaseIdx + 1) % len;  // loop pattern
    tonePhaseStartMs = millis();
    playNote(pattern[tonePhaseIdx].freq);
  }
}

// ---------------------------------------------------------------------------
// Reed switch debounce
// ---------------------------------------------------------------------------
void readReed() {
  bool raw = digitalRead(PIN_REED);
  if (raw != lastReedRaw) {
    reedDebounceMs = millis();
    lastReedRaw = raw;
  }
  if ((millis() - reedDebounceMs) >= DEBOUNCE_MS) {
    reedState = raw;
  }
}

// ---------------------------------------------------------------------------
// Audio helpers
// ---------------------------------------------------------------------------
void playNote(uint16_t freq) {
  if (freq == 0) {
    ledcWrite(PIN_AUDIO, 0);
  } else {
    ledcAttach(PIN_AUDIO, freq, AUDIO_LEDC_RES);
    ledcWrite(PIN_AUDIO, 128);  // 50% duty → square wave
  }
}

void stopNote() {
  ledcWrite(PIN_AUDIO, 0);
}

// ---------------------------------------------------------------------------
// LED helpers
// ---------------------------------------------------------------------------
void setLED(bool on) {
  digitalWrite(PIN_LED, on ? HIGH : LOW);
}

void blinkLED(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    setLED(true);  delay(onMs);
    setLED(false); delay(offMs);
  }
}

// ---------------------------------------------------------------------------
// Preferences (NVS flash storage)
// ---------------------------------------------------------------------------
void loadPrefs() {
  prefs.begin("spintimer", true);  // read-only
  cfgMinutes  = prefs.getInt("minutes", 5);
  cfgSeconds  = prefs.getInt("seconds", 0);
  cfgTone     = prefs.getInt("tone",    0);
  cfgLidBehav = (LidBehaviour)prefs.getInt("behav", 0);
  prefs.end();
  Serial.printf("Prefs loaded: %dm %ds, tone %d, behav %d\n",
                cfgMinutes, cfgSeconds, cfgTone, (int)cfgLidBehav);
}

void savePrefs() {
  prefs.begin("spintimer", false);  // read-write
  prefs.putInt("minutes", cfgMinutes);
  prefs.putInt("seconds", cfgSeconds);
  prefs.putInt("tone",    cfgTone);
  prefs.putInt("behav",   (int)cfgLidBehav);
  prefs.end();
}
