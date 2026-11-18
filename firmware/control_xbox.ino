// =============================================================
//  Robomatrix 2026 - Firmware control Xbox (Bluepad32, BLE)
//  ESP32-S3-WROOM-1U
// =============================================================
#include <Bluepad32.h>

// ---------- Categoría ----------
#define TODOTERRENO 0
#define ROBOFUT     1
#define CATEGORIA   ROBOFUT

#define TT_MOTOR    2      // Driver usado en Todoterreno (CN5, alimenta el servo)

// ---------- Inversión de giro (cambie a 1 si un motor gira al revés) ----------
#define INV_M1   0
#define INV_M2   0
#define INV_TBA  0
#define INV_TBB  0

// ---------- Pines ----------
#define M1_IN1    13
#define M1_IN2    12
#define M1_INH    11
#define M2_IN1    18
#define M2_IN2    17
#define M2_INH    16
#define PWMA      14
#define AIN1       9
#define AIN2      10
#define PWMB      15
#define BIN1      40
#define BIN2      39
#define STBY      41
#define SERVO_PIN 21
#define NEO_PIN   38

// ---------- LEDC ----------
#define CH_M1_IN1  0
#define CH_M1_IN2  1
#define CH_M2_IN1  2
#define CH_M2_IN2  3
#define CH_PWMA    4
#define CH_PWMB    5
#define CH_SERVO   6

#define MOTOR_FREQ 20000
#define MOTOR_RES  10
#define MOTOR_MAX  1023
#define SERVO_FREQ 50
#define SERVO_RES  14

// ---------- Ajustes de manejo ----------
#define DEADZONE      40      // zona muerta de sticks (0..511)
#define SLEW_STEP     40      // cambio máximo por ciclo (rampa)
#define SERVO_CENTER  90
#define SERVO_RANGE   35      // grados a cada lado
#define MICRO_SPEED   800     // velocidad de micromotores (0..1023)
#define TIMEOUT_MS    300     // sin datos -> parar

ControllerPtr ctl = nullptr;
uint32_t lastData = 0;
int curM1 = 0, curM2 = 0;

// =============================================================
//  Hardware
// =============================================================
void ifxInit() {
  pinMode(M1_INH, OUTPUT); digitalWrite(M1_INH, LOW);
  pinMode(M2_INH, OUTPUT); digitalWrite(M2_INH, LOW);
  ledcAttachChannel(M1_IN1, MOTOR_FREQ, MOTOR_RES, CH_M1_IN1);
  ledcAttachChannel(M1_IN2, MOTOR_FREQ, MOTOR_RES, CH_M1_IN2);
  ledcAttachChannel(M2_IN1, MOTOR_FREQ, MOTOR_RES, CH_M2_IN1);
  ledcAttachChannel(M2_IN2, MOTOR_FREQ, MOTOR_RES, CH_M2_IN2);
  ledcWrite(M1_IN1, 0); ledcWrite(M1_IN2, 0);
  ledcWrite(M2_IN1, 0); ledcWrite(M2_IN2, 0);
}

void ifxSet(uint8_t motor, int speed) {
  speed = constrain(speed, -MOTOR_MAX, MOTOR_MAX);
  if ((motor == 1 && INV_M1) || (motor == 2 && INV_M2)) speed = -speed;
  uint8_t in1 = (motor == 1) ? M1_IN1 : M2_IN1;
  uint8_t in2 = (motor == 1) ? M1_IN2 : M2_IN2;
  uint8_t inh = (motor == 1) ? M1_INH : M2_INH;
  if (speed == 0) {
    ledcWrite(in1, 0); ledcWrite(in2, 0);
    digitalWrite(inh, LOW);
    return;
  }
  digitalWrite(inh, HIGH);
  if (speed > 0) { ledcWrite(in2, 0); ledcWrite(in1, speed); }
  else           { ledcWrite(in1, 0); ledcWrite(in2, -speed); }
}

void tbInit() {
  pinMode(STBY, OUTPUT); digitalWrite(STBY, LOW);
  pinMode(AIN1, OUTPUT); pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT); pinMode(BIN2, OUTPUT);
  ledcAttachChannel(PWMA, MOTOR_FREQ, MOTOR_RES, CH_PWMA);
  ledcAttachChannel(PWMB, MOTOR_FREQ, MOTOR_RES, CH_PWMB);
  ledcWrite(PWMA, 0); ledcWrite(PWMB, 0);
}

void tbSet(char ch, int speed) {
  speed = constrain(speed, -MOTOR_MAX, MOTOR_MAX);
  if ((ch == 'A' && INV_TBA) || (ch == 'B' && INV_TBB)) speed = -speed;
  uint8_t p  = (ch == 'A') ? PWMA : PWMB;
  uint8_t d1 = (ch == 'A') ? AIN1 : BIN1;
  uint8_t d2 = (ch == 'A') ? AIN2 : BIN2;
  if (speed > 0)      { digitalWrite(d1, HIGH); digitalWrite(d2, LOW);  }
  else if (speed < 0) { digitalWrite(d1, LOW);  digitalWrite(d2, HIGH); }
  else                { digitalWrite(d1, LOW);  digitalWrite(d2, LOW);  }
  ledcWrite(p, abs(speed));
}

void servoInit() {
  ledcAttachChannel(SERVO_PIN, SERVO_FREQ, SERVO_RES, CH_SERVO);
}

void servoWrite(int angle) {
  angle = constrain(angle, 0, 180);
  uint32_t us   = map(angle, 0, 180, 500, 2500);
  uint32_t duty = (us * ((1UL << SERVO_RES) - 1)) / 20000;
  ledcWrite(SERVO_PIN, duty);
}

void neo(uint8_t r, uint8_t g, uint8_t b) { rgbLedWrite(NEO_PIN, r, g, b); }

void stopAll() {
  curM1 = 0; curM2 = 0;
  ifxSet(1, 0); ifxSet(2, 0);
  tbSet('A', 0); tbSet('B', 0);
  digitalWrite(STBY, LOW);
  servoWrite(SERVO_CENTER);
}

// Rampa: limita el cambio por ciclo para evitar picos de corriente
int slew(int cur, int target) {
  if (target > cur + SLEW_STEP) return cur + SLEW_STEP;
  if (target < cur - SLEW_STEP) return cur - SLEW_STEP;
  return target;
}

// Stick (-511..512) -> (-1023..1023) con zona muerta
int stickToSpeed(int v) {
  if (abs(v) < DEADZONE) return 0;
  return constrain(v * 2, -MOTOR_MAX, MOTOR_MAX);
}

// =============================================================
//  Bluepad32
// =============================================================
void onConnected(ControllerPtr c) {
  if (ctl == nullptr) {
    ctl = c;
    lastData = millis();
    Serial.printf("Control conectado: %s\n", c->getModelName().c_str());
    neo(0, 40, 0);
  } else {
    c->disconnect();   // solo un control a la vez
  }
}

void onDisconnected(ControllerPtr c) {
  if (ctl == c) {
    ctl = nullptr;
    stopAll();
    Serial.println("Control desconectado -> motores parados");
    neo(40, 0, 0);
  }
}

void processController() {
#if CATEGORIA == TODOTERRENO
  // RT acelera, LT reversa (0..1023)
  int target = ctl->throttle() - ctl->brake();
  if (abs(target) < DEADZONE) target = 0;

  int cur = (TT_MOTOR == 1) ? curM1 : curM2;
  cur = slew(cur, target);
  if (TT_MOTOR == 1) curM1 = cur; else curM2 = cur;
  ifxSet(TT_MOTOR, cur);

  // Stick izquierdo X -> dirección
  int x = ctl->axisX();
  if (abs(x) < DEADZONE) x = 0;
  servoWrite(SERVO_CENTER + map(x, -511, 512, -SERVO_RANGE, SERVO_RANGE));

#else  // ROBOFUT
  // Arcade: stick izq. Y = avance, stick der. X = giro
  int thr  = -stickToSpeed(ctl->axisY());   // arriba = negativo en Bluepad32
  int turn =  stickToSpeed(ctl->axisRX());

  int tL = constrain(thr + turn, -MOTOR_MAX, MOTOR_MAX);
  int tR = constrain(thr - turn, -MOTOR_MAX, MOTOR_MAX);
  curM1 = slew(curM1, tL);
  curM2 = slew(curM2, tR);
  ifxSet(1, curM1);
  ifxSet(2, curM2);

  // RB adelante, LB atrás -> micromotores
  int micro = 0;
  if (ctl->r1()) micro =  MICRO_SPEED;
  if (ctl->l1()) micro = -MICRO_SPEED;
  digitalWrite(STBY, micro != 0 ? HIGH : LOW);
  tbSet('A', micro);
  tbSet('B', micro);
#endif
}

// =============================================================
//  Setup / Loop
// =============================================================
void setup() {
  ifxInit();
  tbInit();
  servoInit();
  stopAll();

  Serial.begin(115200);
  neo(40, 0, 0);   // rojo = sin control

  BP32.setup(&onConnected, &onDisconnected);
  // BP32.forgetBluetoothKeys();   // descomente para borrar controles emparejados

  Serial.println("Esperando control Xbox...");
}

void loop() {
  bool updated = BP32.update();

  if (ctl && ctl->isConnected()) {
    if (updated && ctl->hasData()) {
      lastData = millis();
      processController();
    }
    if (millis() - lastData > TIMEOUT_MS) {
      stopAll();   // failsafe: sin datos recientes
    }
  }

  delay(10);
}
