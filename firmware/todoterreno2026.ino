// =============================================================
//  BALAM 2026 - Todoterreno | Robomatrix
//  ESP32-S3-WROOM-1U + IFX007T + Servo + 8 NeoPixels
//  Control BLE por Bluepad32
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

// ---------- Controles permitidos (MAC) ----------
const uint8_t allowedMacs[][6] = {
  {0x03, 0x25, 0x11, 0x14, 0x46, 0x7C},   // Steren
  {0x8C, 0x41, 0xF2, 0xB5, 0x0F, 0xD8},   // Xbox
};
const int NUM_MACS = sizeof(allowedMacs) / 6;

// ---------- Ajustes ----------
#define INV_MOTOR     0       // 1 si RT mueve hacia atrás
#define DEBUG         0       // 1 = imprime valores cada 200 ms

#define SERVO_IZQ     160     // ángulo con stick a la izquierda
#define SERVO_CENTRO  120
#define SERVO_DER     80      // ángulo con stick a la derecha

#define VEL_Y         870     // 85 % (máximo)
#define VEL_B         716     // 70 %
#define VEL_A         600     // 59 %

#define DEADZONE      40      // stick (0..511) y gatillos (0..1023)
#define SLEW_STEP     30      // rampa: cambio máximo cada 10 ms
#define BRILLO        40      // NeoPixel 0..255

// ---------- Pines: conector TT (M2) ----------
#define M_IN1     18
#define M_IN2     17
#define M_INH     16

// ---------- Pines que no se usan en Todoterreno (estado seguro) ----------
#define M1_IN1    13
#define M1_IN2    12
#define M1_INH    11
#define PWMA      14
#define AIN1       9
#define AIN2      10
#define PWMB      15
#define BIN1      40
#define BIN2      39
#define STBY      41

#define SERVO_PIN 21
#define NEO_PIN   38
#define NEO_COUNT  8

// ---------- LEDC ----------
#define CH_IN1     2    // timer 1 - 20 kHz
#define CH_IN2     3    // timer 1 - 20 kHz
#define CH_SERVO   6    // timer 3 - 50 Hz
#define MOTOR_FREQ 20000
#define MOTOR_RES  10
#define SERVO_FREQ 50
#define SERVO_RES  14

Adafruit_NeoPixel tira(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);
ControllerPtr ctl = nullptr;
int velMax   = VEL_Y;
int velActual = 0;
uint32_t colorModo;

// =============================================================
//  Hardware
// =============================================================
void pinesSeguros() {
  const uint8_t p[] = {M1_IN1, M1_IN2, M1_INH, PWMA, AIN1, AIN2,
                       PWMB, BIN1, BIN2, STBY, M_INH};
  for (uint8_t x : p) { pinMode(x, OUTPUT); digitalWrite(x, LOW); }
}

void motorInit() {
  ledcAttachChannel(M_IN1, MOTOR_FREQ, MOTOR_RES, CH_IN1);
  ledcAttachChannel(M_IN2, MOTOR_FREQ, MOTOR_RES, CH_IN2);
  ledcWrite(M_IN1, 0);
  ledcWrite(M_IN2, 0);
}

void motorSet(int v) {
  v = constrain(v, -VEL_Y, VEL_Y);
  if (INV_MOTOR) v = -v;
  if (v == 0) {
    ledcWrite(M_IN1, 0); ledcWrite(M_IN2, 0);
    digitalWrite(M_INH, LOW);
    return;
  }
  digitalWrite(M_INH, HIGH);
  if (v > 0) { ledcWrite(M_IN2, 0); ledcWrite(M_IN1, v); }
  else       { ledcWrite(M_IN1, 0); ledcWrite(M_IN2, -v); }
}

void servoInit() {
  ledcAttachChannel(SERVO_PIN, SERVO_FREQ, SERVO_RES, CH_SERVO);
}

void servoWrite(int ang) {
  ang = constrain(ang, min(SERVO_IZQ, SERVO_DER), max(SERVO_IZQ, SERVO_DER));
  uint32_t us = map(ang, 0, 180, 500, 2400);
  ledcWrite(SERVO_PIN, (us * ((1UL << SERVO_RES) - 1)) / 20000);
}

void llenar(uint32_t c) {
  for (int i = 0; i < NEO_COUNT; i++) tira.setPixelColor(i, c);
  tira.show();
}

void pararTodo() {
  velActual = 0;
  motorSet(0);
  servoWrite(SERVO_CENTRO);
}

int rampa(int cur, int target) {
  if (target > cur + SLEW_STEP) return cur + SLEW_STEP;
  if (target < cur - SLEW_STEP) return cur - SLEW_STEP;
  return target;
}

// =============================================================
//  Bluepad32
// =============================================================
bool macPermitida(const uint8_t* a) {
  for (int i = 0; i < NUM_MACS; i++)
    if (memcmp(a, allowedMacs[i], 6) == 0) return true;
  return false;
}

void onConnected(ControllerPtr c) {
  if (ctl != nullptr) { c->disconnect(); return; }   // solo un control a la vez

  ctl = c;
  velMax = VEL_Y;
  colorModo = tira.Color(255, 200, 0);
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

  // Tracción: RT adelante, LT atrás
  int rt = ctl->throttle();
  int lt = ctl->brake();
  if (rt < DEADZONE) rt = 0;
  if (lt < DEADZONE) lt = 0;
  int objetivo = map(rt, 0, 1023, 0, velMax) - map(lt, 0, 1023, 0, velMax);

  velActual = rampa(velActual, objetivo);
  motorSet(velActual);

  // Dirección: stick izquierdo X
  int x = ctl->axisX();
  if (abs(x) < DEADZONE) x = 0;
  servoWrite(map(x, -512, 511, SERVO_IZQ, SERVO_DER));

#if DEBUG
  static uint32_t t = 0;
  if (millis() - t > 200) {
    t = millis();
    Serial.printf("RT:%4d LT:%4d LX:%4d | vel:%4d max:%4d\n",
                  rt, lt, x, velActual, velMax);
  }
#endif
}

// NeoPixel: barra de velocidad en el color del modo
void actualizarTira() {
  static uint32_t t = 0;
  if (millis() - t < 50) return;
  t = millis();

  if (!ctl) {                      // sin control: rojo parpadeando
    static bool on = false;
    on = !on;
    llenar(on ? tira.Color(255, 0, 0) : 0);
    return;
  }

  int n = map(abs(velActual), 0, VEL_Y, 1, NEO_COUNT);
  tira.clear();
  for (int i = 0; i < n; i++) tira.setPixelColor(i, colorModo);
  if (velActual < 0) tira.setPixelColor(NEO_COUNT - 1, tira.Color(255, 0, 0));
  tira.show();
}

// =============================================================
//  Setup / Loop
// =============================================================
void setup() {
  pinesSeguros();
  motorInit();
  servoInit();
  pararTodo();

  tira.begin();
  tira.setBrightness(BRILLO);
  llenar(tira.Color(255, 0, 0));

  Serial.begin(115200);
  BP32.setup(&onConnected, &onDisconnected);
  BP32.forgetBluetoothKeys();
  BP32.enableVirtualDevice(false);
  Serial.println("BALAM Todoterreno - esperando control...");
}

void loop() {
  BP32.update();
  if (ctl && ctl->isConnected()) procesar();
  actualizarTira();
  delay(10);
}
