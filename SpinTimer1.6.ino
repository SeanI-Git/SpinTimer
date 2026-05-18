// =============================================================================
//  SpinTimer — ESP32 Centrifuge Lid Timer
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
const unsigned long AP_TIMEOUT_MS = 10UL * 60UL * 1000UL; // 10 minutes

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
int          cfgMinutes       = 5;
int          cfgSeconds       = 0;
int          cfgTone          = 0;        // 0-9
LidBehaviour cfgLidBehav     = LID_RESET;
bool         cfgLedDuringCount = false;   // false=off, true=solid on while counting

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
// Tone definitions — 20 patterns: 0-9 Alert (piercing), 10-19 Calm (melodic)
// Each entry: { frequency_hz, duration_ms }  — 0 Hz = silence
// Patterns loop until lid opens.
// ---------------------------------------------------------------------------
struct ToneNote { uint16_t freq; uint16_t dur; };

// Tone 0 — Double Beep [Alert]
const ToneNote TONE0[] = { {2000,150},{0,80},{2000,150},{0,500} };
// Tone 1 — Triple Beep [Alert]
const ToneNote TONE1[] = { {2500,100},{0,60},{2500,100},{0,60},{2500,100},{0,500} };
// Tone 2 — Stutter [Alert]
const ToneNote TONE2[] = { {3000,50},{0,30},{3000,50},{0,30},{3000,50},{0,30},{3000,50},{0,30},{0,300} };
// Tone 3 — Warble [Alert]
const ToneNote TONE3[] = { {1500,60},{1200,60},{1500,60},{1200,60},{0,300} };
// Tone 4 — Siren [Alert]
const ToneNote TONE4[] = { {600,80},{700,80},{800,80},{900,80},{1000,80},{900,80},{800,80},{700,80},{0,200} };
// Tone 5 — Fast Pulse [Alert]
const ToneNote TONE5[] = { {2800,40},{0,20},{2800,40},{0,20},{2800,40},{0,20},{2800,40},{0,20},{2800,40},{0,20},{0,400} };
// Tone 6 — Two-Tone [Alert]
const ToneNote TONE6[] = { {2000,80},{1400,80},{2000,80},{1400,80},{2000,80},{1400,80},{0,350} };
// Tone 7 — Chirp [Alert]
const ToneNote TONE7[] = { {1000,50},{1400,50},{1800,50},{2200,50},{0,80},{1000,50},{1400,50},{1800,50},{2200,50},{0,500} };
// Tone 8 — Dive Bomb [Alert]
const ToneNote TONE8[] = { {3500,60},{3000,60},{2500,60},{2000,60},{1500,60},{1000,60},{0,400} };
// Tone 9 — Industrial [Alert]
const ToneNote TONE9[] = { {2200,300},{0,150},{2200,300},{0,150},{2200,300},{0,600} };
// Tone 10 — Rise [Calm]
const ToneNote TONE10[] = { {880,100},{1046,100},{1318,100},{1568,200},{0,400} };
// Tone 11 — Descend [Calm]
const ToneNote TONE11[] = { {1568,120},{1318,120},{1046,120},{880,120},{0,400} };
// Tone 12 — Ship Bell [Calm]
const ToneNote TONE12[] = { {1760,120},{0,60},{1760,120},{0,800} };
// Tone 13 — Gentle Chime [Calm]
const ToneNote TONE13[] = { {523,180},{0,60},{659,180},{0,60},{784,300},{0,700} };
// Tone 14 — Soft Arpeggio [Calm]
const ToneNote TONE14[] = { {262,200},{330,200},{392,200},{523,300},{0,600} };
// Tone 15 — Lullaby [Calm]
const ToneNote TONE15[] = { {330,250},{0,120},{294,250},{0,900} };
// Tone 16 — Trill [Calm]
const ToneNote TONE16[] = { {440,150},{494,150},{440,150},{494,150},{440,150},{0,600} };
// Tone 17 — Pentatonic [Calm]
const ToneNote TONE17[] = { {262,160},{294,160},{330,160},{392,160},{440,240},{0,600} };
// Tone 18 — Eve Bell [Calm]
const ToneNote TONE18[] = { {523,400},{0,100},{523,200},{0,200},{523,100},{0,800} };
// Tone 19 — Slow Waltz [Calm]
const ToneNote TONE19[] = { {392,300},{0,80},{494,200},{0,80},{523,400},{0,700} };

const ToneNote* TONES[] = {
  TONE0,TONE1,TONE2,TONE3,TONE4,TONE5,TONE6,TONE7,TONE8,TONE9,
  TONE10,TONE11,TONE12,TONE13,TONE14,TONE15,TONE16,TONE17,TONE18,TONE19
};
const uint8_t TONE_LEN[] = { 4,6,9,5,9,11,7,10,7,6,5,5,4,6,5,4,6,6,6,6 };

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
    setLED(cfgLedDuringCount);
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
  
  :root{--bg:#0a0f1a;--panel:#111827;--accent:#3b82f6;--accent2:#60a5fa;--text:#e2e8f0;--dim:#64748b;--green:#22c55e;}
  *{box-sizing:border-box;margin:0;padding:0;}
  body{background:var(--bg);color:var(--text);font-family:'Courier New',monospace;
       min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:24px 16px;}
  h1{font-family:'Courier New',monospace;font-size:1.6rem;color:var(--accent2);
     letter-spacing:.15em;text-transform:uppercase;margin-bottom:4px;}
  .sub{color:var(--dim);font-size:.75rem;letter-spacing:.1em;margin-bottom:28px;}
  .card{background:var(--panel);border:1px solid #1e293b;border-radius:12px;
        padding:22px 24px;width:100%;max-width:420px;margin-bottom:18px;}
  .card h2{font-family:'Courier New',monospace;font-size:.8rem;color:var(--accent);
           letter-spacing:.12em;text-transform:uppercase;margin-bottom:16px;
           border-bottom:1px solid #1e293b;padding-bottom:8px;}
  .row{display:flex;gap:16px;align-items:flex-end;}
  .field{flex:1;display:flex;flex-direction:column;gap:6px;}
  label{font-size:.7rem;color:var(--dim);letter-spacing:.08em;text-transform:uppercase;}
  select{background:#0f172a;border:1px solid #334155;color:var(--text);
         border-radius:6px;padding:10px 8px;font-family:'Courier New',monospace;
         font-size:.95rem;width:100%;cursor:pointer;appearance:none;text-align:center;}
  select:focus{outline:none;border-color:var(--accent);}
  .colon{font-size:1.4rem;color:var(--accent);padding-bottom:6px;flex-shrink:0;}
  /* ── Tone section ── */
  .tone-group-label{font-size:.68rem;letter-spacing:.1em;text-transform:uppercase;
                    font-weight:bold;margin:12px 0 6px;padding:4px 8px;border-radius:4px;}
  .alert-label{color:#fca5a5;background:#2d0a0a;border:1px solid #7f1d1d;}
  .calm-label {color:#93c5fd;background:#0a1628;border:1px solid #1e3a5f;}
  .tone-grid{display:grid;grid-template-columns:repeat(5,1fr);gap:6px;margin-bottom:4px;}
  .tone-cell{display:flex;flex-direction:column;gap:3px;}
  /* Alert tone buttons — red family */
  .tone-btn.alert{background:#1a0505;border:1px solid #7f1d1d;color:#f87171;}
  .tone-btn.alert:hover{border-color:#ef4444;color:#fca5a5;background:#2d0a0a;}
  .tone-btn.alert.active{background:#7f1d1d;border-color:#ef4444;color:#fff;}
  /* Calm tone buttons — blue family */
  .tone-btn.calm{background:#050d1a;border:1px solid #1e3a5f;color:#60a5fa;}
  .tone-btn.calm:hover{border-color:#3b82f6;color:#93c5fd;background:#0a1628;}
  .tone-btn.calm.active{background:#1e3a5f;border-color:#3b82f6;color:#fff;}
  /* Shared tone button base */
  .tone-btn{border-radius:6px;padding:7px 2px;font-family:'Courier New',monospace;
            font-size:.72rem;cursor:pointer;transition:all .15s;text-align:center;width:100%;}
  /* Preview buttons */
  .prev-btn{border-radius:5px;padding:4px 2px;font-size:.7rem;cursor:pointer;
            transition:all .15s;text-align:center;width:100%;font-family:'Courier New',monospace;}
  .prev-btn.alert-p{background:#0d0202;border:1px solid #450a0a;color:#f87171;}
  .prev-btn.alert-p:hover{border-color:#7f1d1d;background:#1a0505;}
  .prev-btn.calm-p {background:#020508;border:1px solid #0c1f3a;color:#60a5fa;}
  .prev-btn.calm-p:hover{border-color:#1e3a5f;background:#050d1a;}
  .prev-btn.playing{border-color:#f59e0b!important;color:#f59e0b!important;background:#1a1000!important;}
  /* Stop bar */
  .stop-bar{display:none;width:100%;max-width:420px;margin-bottom:12px;}
  .stop-bar.visible{display:block;}
  .stop-btn{width:100%;padding:10px;background:#7f1d1d;border:1px solid #ef4444;
            border-radius:8px;color:#fca5a5;font-family:'Courier New',monospace;
            font-size:.85rem;cursor:pointer;letter-spacing:.05em;}
  .stop-btn:hover{background:#991b1b;}
  /* Behaviour */
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
  /* Quick-set */
  .quick-row{display:flex;gap:10px;}
  .quick-btn{flex:1;padding:12px 4px;background:#0f172a;border:1px solid #334155;
             border-radius:8px;color:var(--accent2);font-family:'Courier New',monospace;
             font-size:.85rem;letter-spacing:.08em;cursor:pointer;transition:all .15s;text-align:center;}
  .quick-btn:hover{border-color:var(--accent);background:#1e293b;color:#fff;}
  .quick-btn:active{background:var(--accent);border-color:var(--accent2);color:#fff;}
  /* Save */
  .save-btn{width:100%;max-width:420px;padding:14px;background:var(--accent);
            border:none;border-radius:8px;color:#fff;font-family:'Courier New',monospace;
            font-size:.9rem;letter-spacing:.1em;text-transform:uppercase;cursor:pointer;
            transition:background .15s;}
  .save-btn:hover{background:var(--accent2);}
  .msg{display:none;margin-top:14px;padding:10px 16px;border-radius:6px;
       font-size:.8rem;text-align:center;}
  .msg.ok{background:#14532d;color:var(--green);display:block;}
  .msg.err{background:#450a0a;color:#f87171;display:block;}
</style>
</head>
<body>
<h1>&#9881; SpinTimer1</h1>
<div class="sub">CENTRIFUGE TIMER CONFIGURATION</div>

<!-- Quick-Set Presets -->
<div class="card">
  <h2>Quick Set</h2>
  <div class="quick-row">
    <button class="quick-btn" onclick="quickSet(3,0)">3 MIN</button>
    <button class="quick-btn" onclick="quickSet(5,0)">5 MIN</button>
    <button class="quick-btn" onclick="quickSet(8,0)">8 MIN</button>
  </div>
</div>

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
  <div class="tone-group-label alert-label">&#9888; Alert &mdash; piercing &amp; attention-grabbing</div>
  <div class="tone-grid" id="alertGrid"></div>
  <div class="tone-group-label calm-label">&#9834; Calm &mdash; melodic &amp; less intrusive</div>
  <div class="tone-grid" id="calmGrid"></div>
  <div style="margin-top:10px;font-size:.65rem;color:var(--dim);text-align:center;line-height:1.6;">
    Tap a name to select &bull; Tap &#9654; to preview on your device
  </div>
</div>

<!-- Stop preview bar -->
<div class="stop-bar" id="stopBar">
  <button class="stop-btn" onclick="stopPreview()">&#9646;&#9646; STOP PREVIEW</button>
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
        <span>First lid opening pauses. If closed then opened again, resets.</span>
      </div>
    </label>
  </div>
</div>

<!-- LED During Countdown -->
<div class="card">
  <h2>LED During Countdown</h2>
  <div class="behav-list">
    <label class="behav-opt" id="l0">
      <input type="radio" name="ledcount" value="0">
      <div class="behav-label">
        <strong>LED Off</strong>
        <span>LED stays off while the countdown is running.</span>
      </div>
    </label>
    <label class="behav-opt" id="l1">
      <input type="radio" name="ledcount" value="1">
      <div class="behav-label">
        <strong>LED On &mdash; Solid</strong>
        <span>LED stays solid on while the countdown is running.</span>
      </div>
    </label>
  </div>
</div>

<button class="save-btn" onclick="save()">SAVE &amp; ARM TIMER</button>
<div class="msg" id="msg"></div>

<script>
const $=id=>document.getElementById(id);

// ---------------------------------------------------------------------------
// Tone data — mirrors C++ ToneNote arrays exactly. 0-9 Alert, 10-19 Calm.
// ---------------------------------------------------------------------------
const TONES=[
  [[2000,150],[0,80],[2000,150],[0,500]],
  [[2500,100],[0,60],[2500,100],[0,60],[2500,100],[0,500]],
  [[3000,50],[0,30],[3000,50],[0,30],[3000,50],[0,30],[3000,50],[0,30],[0,300]],
  [[1500,60],[1200,60],[1500,60],[1200,60],[0,300]],
  [[600,80],[700,80],[800,80],[900,80],[1000,80],[900,80],[800,80],[700,80],[0,200]],
  [[2800,40],[0,20],[2800,40],[0,20],[2800,40],[0,20],[2800,40],[0,20],[2800,40],[0,20],[0,400]],
  [[2000,80],[1400,80],[2000,80],[1400,80],[2000,80],[1400,80],[0,350]],
  [[1000,50],[1400,50],[1800,50],[2200,50],[0,80],[1000,50],[1400,50],[1800,50],[2200,50],[0,500]],
  [[3500,60],[3000,60],[2500,60],[2000,60],[1500,60],[1000,60],[0,400]],
  [[2200,300],[0,150],[2200,300],[0,150],[2200,300],[0,600]],
  [[880,100],[1046,100],[1318,100],[1568,200],[0,400]],
  [[1568,120],[1318,120],[1046,120],[880,120],[0,400]],
  [[1760,120],[0,60],[1760,120],[0,800]],
  [[523,180],[0,60],[659,180],[0,60],[784,300],[0,700]],
  [[262,200],[330,200],[392,200],[523,300],[0,600]],
  [[330,250],[0,120],[294,250],[0,900]],
  [[440,150],[494,150],[440,150],[494,150],[440,150],[0,600]],
  [[262,160],[294,160],[330,160],[392,160],[440,240],[0,600]],
  [[523,400],[0,100],[523,200],[0,200],[523,100],[0,800]],
  [[392,300],[0,80],[494,200],[0,80],[523,400],[0,700]]
];

const ALERT_NAMES=['Dbl Beep','Triple','Stutter','Warble','Siren',
                   'Fast Pulse','Two-Tone','Chirp','Dive Bomb','Industrial'];
const CALM_NAMES =['Rise','Descend','Ship Bell','Gnt Chime','Soft Arp',
                   'Lullaby','Trill','Pentatonic','Eve Bell','Slow Waltz'];

// ---------------------------------------------------------------------------
// Web Audio preview
// ---------------------------------------------------------------------------
let audioCtx=null,previewTimeout=null,activePrevBtn=null;

function getAudioCtx(){
  if(!audioCtx) audioCtx=new(window.AudioContext||window.webkitAudioContext)();
  if(audioCtx.state==='suspended') audioCtx.resume();
  return audioCtx;
}

function stopPreview(){
  if(previewTimeout){clearTimeout(previewTimeout);previewTimeout=null;}
  if(audioCtx){audioCtx.close();audioCtx=null;}
  if(activePrevBtn){activePrevBtn.classList.remove('playing');activePrevBtn=null;}
  $('stopBar').classList.remove('visible');
}

function playPattern(pattern,btn){
  stopPreview();
  activePrevBtn=btn;
  btn.classList.add('playing');
  $('stopBar').classList.add('visible');
  const ctx=getAudioCtx();
  let cursor=0;
  function scheduleNote(idx){
    if(!audioCtx||audioCtx!==ctx) return;
    if(idx>=pattern.length){stopPreview();return;}
    const[freq,dur]=pattern[idx];
    const t=ctx.currentTime+cursor/1000;
    if(freq>0){
      const osc=ctx.createOscillator();
      const gain=ctx.createGain();
      osc.type='square';
      osc.frequency.setValueAtTime(freq,t);
      gain.gain.setValueAtTime(0,t);
      gain.gain.linearRampToValueAtTime(0.18,t+0.005);
      gain.gain.setValueAtTime(0.18,t+dur/1000-0.005);
      gain.gain.linearRampToValueAtTime(0,t+dur/1000);
      osc.connect(gain);gain.connect(ctx.destination);
      osc.start(t);osc.stop(t+dur/1000);
    }
    cursor+=dur;
    previewTimeout=setTimeout(()=>scheduleNote(idx+1),cursor-20);
  }
  scheduleNote(0);
}

// ---------------------------------------------------------------------------
// Build tone grids
// ---------------------------------------------------------------------------
function buildGrid(gridId,names,offset,btnClass,prevClass){
  const grid=$(gridId);
  names.forEach((n,i)=>{
    const idx=offset+i;
    const cell=document.createElement('div');
    cell.className='tone-cell';

    const b=document.createElement('button');
    b.className='tone-btn '+btnClass;
    b.textContent=n; b.dataset.i=idx;
    b.onclick=()=>{
      document.querySelectorAll('.tone-btn').forEach(x=>x.classList.remove('active'));
      b.classList.add('active');
    };

    const p=document.createElement('button');
    p.className='prev-btn '+prevClass;
    p.textContent='\u25B6'; p.title='Preview '+n;
    p.onclick=e=>{e.stopPropagation();playPattern(TONES[idx],p);};

    cell.appendChild(b);cell.appendChild(p);
    grid.appendChild(cell);
  });
}

buildGrid('alertGrid',ALERT_NAMES,0,'alert','alert-p');
buildGrid('calmGrid', CALM_NAMES, 10,'calm','calm-p');

// ---------------------------------------------------------------------------
// Dropdowns
// ---------------------------------------------------------------------------
['mins','secs'].forEach(id=>{
  const sel=$(id);
  for(let i=0;i<=60;i++){
    const o=document.createElement('option');
    o.value=i;o.textContent=String(i).padStart(2,'0');
    sel.appendChild(o);
  }
});

// ---------------------------------------------------------------------------
// Load config
// ---------------------------------------------------------------------------
fetch('/config').then(r=>r.json()).then(d=>{
  $('mins').value=d.minutes;
  $('secs').value=d.seconds;
  const btn=document.querySelector(`.tone-btn[data-i="${d.tone}"]`);
  if(btn) btn.classList.add('active');
  const radio=document.querySelector(`input[name=behav][value="${d.behav}"]`);
  if(radio){radio.checked=true;updateBehavHighlight(d.behav);}
  const ledRadio=document.querySelector(`input[name=ledcount][value="${d.ledcount}"]`);
  if(ledRadio){ledRadio.checked=true;updateLedHighlight(d.ledcount);}
});

// ---------------------------------------------------------------------------
// Behaviour highlight
// ---------------------------------------------------------------------------
document.querySelectorAll('input[name=behav]').forEach(r=>{
  r.addEventListener('change',()=>updateBehavHighlight(r.value));
});
function updateBehavHighlight(v){
  ['b0','b1','b2'].forEach((id,i)=>{
    $(id).classList.toggle('active',String(i)===String(v));
  });
}

// ---------------------------------------------------------------------------
// LED during countdown highlight
// ---------------------------------------------------------------------------
document.querySelectorAll('input[name=ledcount]').forEach(r=>{
  r.addEventListener('change',()=>updateLedHighlight(r.value));
});
function updateLedHighlight(v){
  ['l0','l1'].forEach((id,i)=>{
    $(id).classList.toggle('active',String(i)===String(v));
  });
}

// ---------------------------------------------------------------------------
// Quick-set presets
// ---------------------------------------------------------------------------
function quickSet(mins,secs){
  $('mins').value=mins;
  $('secs').value=secs;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------
function save(){
  stopPreview();
  const mins=parseInt($('mins').value);
  const secs=parseInt($('secs').value);
  const toneEl=document.querySelector('.tone-btn.active');
  const tone=toneEl?parseInt(toneEl.dataset.i):0;
  const behavEl=document.querySelector('input[name=behav]:checked');
  const behav=behavEl?parseInt(behavEl.value):0;
  const ledEl=document.querySelector('input[name=ledcount]:checked');
  const ledcount=ledEl?parseInt(ledEl.value):0;
  fetch('/save',{
    method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:`minutes=${mins}&seconds=${secs}&tone=${tone}&behav=${behav}&ledcount=${ledcount}`
  }).then(r=>r.text()).then(()=>{
    const m=$('msg');
    m.className='msg ok';m.textContent='\u2713 Settings saved! Close the lid to start timing.';
  }).catch(()=>{
    const m=$('msg');
    m.className='msg err';m.textContent='Error saving settings.';
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
                  ",\"behav\":"   + String((int)cfgLidBehav) +
                  ",\"ledcount\":" + String(cfgLedDuringCount ? 1 : 0) + "}";
    server.send(200, "application/json", json);
  });

  // Save config
  server.on("/save", HTTP_POST, []() {
    if (server.hasArg("minutes"))  cfgMinutes        = constrain(server.arg("minutes").toInt(), 0, 60);
    if (server.hasArg("seconds"))  cfgSeconds        = constrain(server.arg("seconds").toInt(), 0, 60);
    if (server.hasArg("tone"))     cfgTone           = constrain(server.arg("tone").toInt(),    0, 19);
    if (server.hasArg("behav"))    cfgLidBehav       = (LidBehaviour)constrain(server.arg("behav").toInt(), 0, 2);
    if (server.hasArg("ledcount")) cfgLedDuringCount = server.arg("ledcount").toInt() == 1;
    savePrefs();
    server.send(200, "text/plain", "OK");
    Serial.printf("Config saved: %dm %ds, tone %d, behav %d, ledcount %d\n",
                  cfgMinutes, cfgSeconds, cfgTone, (int)cfgLidBehav, (int)cfgLedDuringCount);
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
  // Apply counting LED preference after blip
  setLED(cfgLedDuringCount);
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
  cfgMinutes        = prefs.getInt("minutes", 5);
  cfgSeconds        = prefs.getInt("seconds", 0);
  cfgTone           = prefs.getInt("tone",    0);
  cfgLidBehav       = (LidBehaviour)prefs.getInt("behav", 0);
  cfgLedDuringCount = prefs.getBool("ledcount", false);
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
  prefs.putBool("ledcount", cfgLedDuringCount);
  prefs.end();
}

