// =============================================================
//  BALAM 2026 - Robofut | Robomatrix
//  ESP32-S3-WROOM-1U + 2x IFX007T + TB6612 + 8 NeoPixels
//  Control BLE por Bluepad32 - control tipo tanque
// =============================================================
#include <Bluepad32.h>
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

// ---------- Ajustes ----------
#define INV_M1      0      // 1 si el motor izquierdo gira al revés
#define INV_M2      1      // 1 si el motor derecho gira al revés
#define INV_MICRO   0      // 1 si los micromotores giran al revés
#define DEBUG       0      // 1 = imprime valores cada 200 ms

#define VEL_Y       1023    // 85 % (máximo)
#define VEL_B       784    // 77 %
#define VEL_A       600    // 59 %
#define VEL_MICRO   800    // 78 % (micromotores)

#define DEADZONE    40     // sticks (0..511)
#define GATILLO_MIN 50     // gatillos (0..1023)
#define SLEW_STEP   30     // rampa: cambio máximo cada 10 ms
#define BRILLO      40     // NeoPixel 0..255

// ---------- Pines: conector RF (M1 = izquierdo) ----------
#define M1_IN1    13
#define M1_IN2    12
#define M1_INH    11
// ---------- Pines: conector TT (M2 = derecho) ----------
#define M2_IN1    18
#define M2_IN2    17
#define M2_INH    16
// ---------- Pines: TB6612 (micromotores / paletas) ----------
#define PWMA      14
#define AIN1       9
#define AIN2      10
#define PWMB      15
#define BIN1      40
#define BIN2      39
#define STBY      41
// ---------- Otros ----------
#define SERVO_PIN 21      // no se usa en Robofut (queda en LOW)
#define NEO_PIN   38
#define NEO_COUNT  8

// ---------- LEDC: 20 kHz, 10 bits ----------
#define FREQ  20000
#define RES   10

Adafruit_NeoPixel tira(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);
ControllerPtr ctl = nullptr;

int velMax   = VEL_B;
int curM1    = 0;
int curM2    = 0;
int curMicro = 0;
uint32_t colorModo;

// =============================================================
//  Hardware
// =============================================================
void ifxSet(uint8_t in1, uint8_t in2, uint8_t inh, int v) {
  v = constrain(v, -VEL_Y, VEL_Y);
  if (v == 0) {
    ledcWrite(in1, 0); ledcWrite(in2, 0);
    digitalWrite(inh, LOW);
    return;
  }
  digitalWrite(inh, HIGH);
  if (v > 0) { ledcWrite(in2, 0); ledcWrite(in1, v); }
  else       { ledcWrite(in1, 0); ledcWrite(in2, -v); }
}

void m1Set(int v) { ifxSet(M1_IN1, M1_IN2, M1_INH, INV_M1 ? -v : v); }
void m2Set(int v) { ifxSet(M2_IN1, M2_IN2, M2_INH, INV_M2 ? -v : v); }

// v = 0 -> freno (IN1 = IN2 = HIGH), igual que motor.brake()
void tbSet(uint8_t pwm, uint8_t d1, uint8_t d2, int v) {
  v = constrain(v, -VEL_Y, VEL_Y);
  if (v > 0)      { digitalWrite(d1, HIGH); digitalWrite(d2, LOW);  ledcWrite(pwm, v);  }
  else if (v < 0) { digitalWrite(d1, LOW);  digitalWrite(d2, HIGH); ledcWrite(pwm, -v); }
  else            { digitalWrite(d1, HIGH); digitalWrite(d2, HIGH); ledcWrite(pwm, 0);  }
}

void microSet(int v) {
  if (INV_MICRO) v = -v;
  digitalWrite(STBY, HIGH);
  tbSet(PWMA, AIN1, AIN2, v);
  tbSet(PWMB, BIN1, BIN2, v);
}

void pararTodo() {
  curM1 = curM2 = curMicro = 0;
  m1Set(0);
  m2Set(0);
  microSet(0);
  digitalWrite(STBY, LOW);
}

int rampa(int cur, int target) {
  if (target > cur + SLEW_STEP) return cur + SLEW_STEP;
  if (target < cur - SLEW_STEP) return cur - SLEW_STEP;
  return target;
}

// Stick (-512..511) -> (-velMax..+velMax), arriba = positivo
int stickAVel(int v) {
  if (abs(v) < DEADZONE) return 0;
  return map(v, 511, -512, -velMax, velMax);
}

// =============================================================
//  NeoPixel
//   LEDs 0-2: motor izquierdo | 3-4: micromotores | 5-7: motor derecho
//   Color del modo = adelante, rojo = atrás
// =============================================================
void llenar(uint32_t c) {
  for (int i = 0; i < NEO_COUNT; i++) tira.setPixelColor(i, c);
  tira.show();
}

uint32_t colorVel(int v) {
  if (v == 0) return 0;
  return (v > 0) ? colorModo : tira.Color(255, 0, 0);
}

void actualizarTira() {
  static uint32_t t = 0;
  if (millis() - t < 50) return;
  t = millis();

  if (!ctl) {                              // sin control: rojo parpadeando
    static bool on = false;
    on = !on;
    llenar(on ? tira.Color(255, 0, 0) : 0);
    return;
  }

  tira.clear();
  int n1 = (curM1 == 0) ? 0 : map(abs(curM1), 1, VEL_Y, 1, 3);
  int n2 = (curM2 == 0) ? 0 : map(abs(curM2), 1, VEL_Y, 1, 3);
  for (int i = 0; i < n1; i++) tira.setPixelColor(i,     colorVel(curM1));
  for (int i = 0; i < n2; i++) tira.setPixelColor(7 - i, colorVel(curM2));

  uint32_t cm = (curMicro > 0) ? tira.Color(255, 255, 255)
              : (curMicro < 0) ? tira.Color(255, 0, 255) : 0;
  tira.setPixelColor(3, cm);
  tira.setPixelColor(4, cm);

  // Sin movimiento: LED central muestra el modo
  if (curM1 == 0 && curM2 == 0 && curMicro == 0) {
    tira.setPixelColor(3, colorModo);
    tira.setPixelColor(4, colorModo);
  }
  tira.show();
}

// =============================================================
//  Bluepad32
// =============================================================
void onConnected(ControllerPtr c) {
  if (ctl != nullptr) { c->disconnect(); return; }   // solo un control
  ctl = c;
  velMax = VEL_B;
  colorModo = tira.Color(0, 80, 255);
  Serial.printf("Conectado: %s\n", c->getModelName().c_str());
  llenar(tira.Color(0, 255, 0));
}

void onDisconnected(ControllerPtr c) {
  if (ctl == c) {
    ctl = nullptr;
    pararTodo();
    Serial.println("Desconectado -> todo parado");
  }
}

// =============================================================
//  Control
// =============================================================
void procesar() {
  // Modos de velocidad
  if (ctl->y()) { velMax = VEL_Y; colorModo = tira.Color(255, 200, 0); }
  if (ctl->b()) { velMax = VEL_B; colorModo = tira.Color(0, 80, 255); }
  if (ctl->a()) { velMax = VEL_A; colorModo = tira.Color(0, 255, 0); }

  // Tracción tipo tanque: stick izq. Y = motor izq., stick der. Y = motor der.
  int objM1 = stickAVel(ctl->axisRY());   // stick derecho -> M1
  int objM2 = stickAVel(ctl->axisY());    // stick izquierdo -> M2
  curM1 = rampa(curM1, objM1);
  curM2 = rampa(curM2, objM2);
  m1Set(curM1);
  m2Set(curM2);

  // Micromotores: RT (derecho) adelante, LT (izquierdo) atrás
  int rt = ctl->throttle();
  int lt = ctl->brake();
  if      (rt > GATILLO_MIN) curMicro =  VEL_MICRO;
  else if (lt > GATILLO_MIN) curMicro = -VEL_MICRO;
  else                       curMicro =  0;
  microSet(curMicro);

#if DEBUG
  static uint32_t t = 0;
  if (millis() - t > 200) {
    t = millis();
    Serial.printf("LY:%4d RY:%4d RT:%4d LT:%4d | M1:%4d M2:%4d Micro:%4d max:%4d\n",
                  ctl->axisY(), ctl->axisRY(), rt, lt,
                  curM1, curM2, curMicro, velMax);
  }
#endif
}

// =============================================================
//  Setup / Loop
// =============================================================
void setup() {
  const uint8_t dig[] = {M1_INH, M2_INH, AIN1, AIN2, BIN1, BIN2, STBY, SERVO_PIN};
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
  llenar(tira.Color(255, 0, 0));

  Serial.begin(115200);
  BP32.setup(&onConnected, &onDisconnected);
  BP32.enableVirtualDevice(false);
  // BP32.forgetBluetoothKeys();   // descomente si el control no conecta
  Serial.println("BALAM Robofut - esperando control...");
}

void loop() {
  BP32.update();
  if (ctl && ctl->isConnected()) procesar();
  actualizarTira();
  delay(10);
}
