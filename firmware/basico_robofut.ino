// =============================================================
//  BALAM 2026 - Robofut | Prueba base sin control
//  2 motores IFX007T + 2 micromotores TB6612 + 8 NeoPixels
// =============================================================
#include <Adafruit_NeoPixel.h>

// ===== Compatibilidad core ESP32 2.x / 3.x =====
#if ESP_ARDUINO_VERSION_MAJOR < 3
static int8_t _pinCh[49];
static bool   _pinChInit = false;
static void _initPinCh() {
  if (!_pinChInit) {
    for (int i = 0; i < 49; i++) _pinCh[i] = -1;
    _pinChInit = true;
  }
}
bool ledcAttachChannel(uint8_t pin, uint32_t freq, uint8_t res, uint8_t ch) {
  _initPinCh();
  ledcSetup(ch, freq, res);
  ledcAttachPin(pin, ch);
  _pinCh[pin] = ch;
  return true;
}
static void _ledcWritePin(uint8_t pin, uint32_t duty) {
  _initPinCh();
  if (_pinCh[pin] >= 0) ledcWrite((uint8_t)_pinCh[pin], duty);
}
#define ledcWrite(pin, duty) _ledcWritePin((pin), (duty))
#endif
// ===============================================

// ---------- Pines: conector RF (M1) ----------
#define M1_IN1    13
#define M1_IN2    12
#define M1_INH    11
// ---------- Pines: conector TT (M2) ----------
#define M2_IN1    18
#define M2_IN2    17
#define M2_INH    16
// ---------- Pines: TB6612 ----------
#define PWMA      14
#define AIN1       9
#define AIN2      10
#define PWMB      15
#define BIN1      40
#define BIN2      39
#define STBY      41
// ---------- NeoPixel ----------
#define NEO_PIN   38
#define NEO_COUNT  8
#define BRILLO    40

// ---------- LEDC (20 kHz, 10 bits) ----------
#define FREQ  20000
#define RES   10
#define VMAX  870          // 85 % máximo

// ---------- Secuencia ----------
const int velocidades[] = {410, 610, 870};   // 40 %, 60 %, 85 %
#define T_VEL      1500    // ms en cada velocidad
#define T_PAUSA     700    // ms parado entre sentidos
#define RAMPA_MS    300    // duración de cada rampa

Adafruit_NeoPixel tira(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

// Motores: 0 = M1 (RF), 1 = M2 (TT), 2 = Micro A, 3 = Micro B
const char* nombre[] = {"M1 (RF)", "M2 (TT)", "Micro A", "Micro B"};
int velActual[4] = {0, 0, 0, 0};

// =============================================================
//  Hardware
// =============================================================
void ifxSet(uint8_t in1, uint8_t in2, uint8_t inh, int v) {
  v = constrain(v, -VMAX, VMAX);
  if (v == 0) {
    ledcWrite(in1, 0); ledcWrite(in2, 0);
    digitalWrite(inh, LOW);
    return;
  }
  digitalWrite(inh, HIGH);
  if (v > 0) { ledcWrite(in2, 0); ledcWrite(in1, v); }
  else       { ledcWrite(in1, 0); ledcWrite(in2, -v); }
}

void tbSet(uint8_t pwm, uint8_t d1, uint8_t d2, int v) {
  v = constrain(v, -VMAX, VMAX);
  if (v > 0)      { digitalWrite(d1, HIGH); digitalWrite(d2, LOW);  }
  else if (v < 0) { digitalWrite(d1, LOW);  digitalWrite(d2, HIGH); }
  else            { digitalWrite(d1, LOW);  digitalWrite(d2, LOW);  }
  ledcWrite(pwm, abs(v));
}

void motorSet(int m, int v) {
  velActual[m] = v;
  switch (m) {
    case 0: ifxSet(M1_IN1, M1_IN2, M1_INH, v); break;
    case 1: ifxSet(M2_IN1, M2_IN2, M2_INH, v); break;
    case 2: tbSet(PWMA, AIN1, AIN2, v); break;
    case 3: tbSet(PWMB, BIN1, BIN2, v); break;
  }
  // STBY activo solo si algún micromotor se mueve
  digitalWrite(STBY, (velActual[2] != 0 || velActual[3] != 0) ? HIGH : LOW);
}

void pararTodo() {
  for (int m = 0; m < 4; m++) motorSet(m, 0);
}

// =============================================================
//  NeoPixel
//   LEDs 0-1: M1 | 2-3: M2 | 4-5: Micro A | 6-7: Micro B
//   Verde = adelante, Rojo = atrás, brillo = velocidad
// =============================================================
void actualizarTira() {
  tira.clear();
  for (int m = 0; m < 4; m++) {
    int v = velActual[m];
    uint8_t b = map(abs(v), 0, VMAX, 0, 255);
    uint32_t c = 0;
    if (v > 0) c = tira.Color(0, b, 0);
    if (v < 0) c = tira.Color(b, 0, 0);
    tira.setPixelColor(m * 2,     c);
    tira.setPixelColor(m * 2 + 1, c);
  }
  tira.show();
}

void llenar(uint32_t c) {
  for (int i = 0; i < NEO_COUNT; i++) tira.setPixelColor(i, c);
  tira.show();
}

// =============================================================
//  Rampa de un motor (o de todos si m = -1)
// =============================================================
void rampa(int m, int desde, int hasta) {
  const int pasos = 30;
  for (int i = 1; i <= pasos; i++) {
    int v = desde + (long)(hasta - desde) * i / pasos;
    if (m < 0) for (int k = 0; k < 4; k++) motorSet(k, v);
    else motorSet(m, v);
    actualizarTira();
    delay(RAMPA_MS / pasos);
  }
}

// Un sentido completo: sube por las 3 velocidades y baja a 0
void sentido(int m, int signo) {
  int prev = 0;
  for (int v : velocidades) {
    int obj = signo * v;
    Serial.printf("  %s -> %s %d%%\n",
                  m < 0 ? "TODOS" : nombre[m],
                  signo > 0 ? "adelante" : "atras",
                  v * 100 / 1023);
    rampa(m, prev, obj);
    prev = obj;
    delay(T_VEL);
  }
  rampa(m, prev, 0);
  delay(T_PAUSA);
}

// =============================================================
//  Setup / Loop
// =============================================================
void setup() {
  const uint8_t dig[] = {M1_INH, M2_INH, AIN1, AIN2, BIN1, BIN2, STBY};
  for (uint8_t p : dig) { pinMode(p, OUTPUT); digitalWrite(p, LOW); }

  ledcAttachChannel(M1_IN1, FREQ, RES, 0);
  ledcAttachChannel(M1_IN2, FREQ, RES, 1);
  ledcAttachChannel(M2_IN1, FREQ, RES, 2);
  ledcAttachChannel(M2_IN2, FREQ, RES, 3);
  ledcAttachChannel(PWMA,   FREQ, RES, 4);
  ledcAttachChannel(PWMB,   FREQ, RES, 5);
  pararTodo();

  tira.begin();
  tira.setBrightness(BRILLO);
  llenar(tira.Color(0, 0, 255));      // azul = inicio

  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  Serial.println("\n=== BALAM Robofut - prueba base ===");
  delay(2000);                         // tiempo para alejar las manos
}

void loop() {
  // 1. Cada motor por separado
  for (int m = 0; m < 4; m++) {
    Serial.printf("\n[%s]\n", nombre[m]);
    sentido(m, +1);
    sentido(m, -1);
  }

  // 2. Todos juntos
  Serial.println("\n[TODOS JUNTOS]");
  sentido(-1, +1);
  sentido(-1, -1);

  // Fin de ciclo
  pararTodo();
  llenar(tira.Color(0, 0, 255));
  Serial.println("\nCiclo completo. Reinicia en 3 s...");
  delay(3000);
}
