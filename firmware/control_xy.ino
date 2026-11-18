// =============================================================
//  Robomatrix 2026 - Firmware app RemoteXY (BLE)
//  ESP32-S3-WROOM-1U
// =============================================================
#define REMOTEXY_MODE__ESP32CORE_BLE
#include <BLEDevice.h>
#define REMOTEXY_BLUETOOTH_NAME "Robomatrix26"
#include <RemoteXY.h>

// ---------- PEGUE AQUÍ el arreglo generado por el editor ----------
#pragma pack(push, 1)
uint8_t RemoteXY_CONF[] = {
  // <-- reemplace con el arreglo de remotexy.com
  0
};

// La estructura debe coincidir con la del editor.
// Si el editor genera otro orden o tipos, use la del editor.
struct {
  int8_t  joy_x;       // -100..100
  int8_t  joy_y;       // -100..100
  uint8_t btn_fwd;     // 0 / 1
  uint8_t btn_rev;     // 0 / 1
  uint8_t connect_flag;
} RemoteXY;
#pragma pack(pop)

// ---------- Categoría ----------
#define TODOTERRENO 0
#define ROBOFUT     1
#define CATEGORIA   ROBOFUT

#define TT_MOTOR    2

// ---------- Inversión de giro ----------
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

// ---------- Ajustes ----------
#define DEADZONE      8       // zona muerta del joystick (0..100)
#define SLEW_STEP     40
#define SERVO_CENTER  90
#define SERVO_RANGE   35
#define MICRO_SPEED   800
#define LOOP_MS       10

int curM1 = 0, curM2 = 0;
bool wasConnected = false;
uint32_t lastLoop = 0;

// =============================================================
//  Hardware (igual que el firmware Xbox)
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

int slew(int cur, int target) {
  if (target > cur + SLEW_STEP) return cur + SLEW_STEP;
  if (target < cur - SLEW_STEP) return cur - SLEW_STEP;
  return target;
}

// Joystick (-100..100) -> (-1023..1023) con zona muerta
int joyToSpeed(int v) {
  if (abs(v) < DEADZONE) return 0;
  return constrain(v * MOTOR_MAX / 100, -MOTOR_MAX, MOTOR_MAX);
}

// =============================================================
//  Control
// =============================================================
void processApp() {
#if CATEGORIA == TODOTERRENO
  int target = joyToSpeed(RemoteXY.joy_y);
  int cur = (TT_MOTOR == 1) ? curM1 : curM2;
  cur = slew(cur, target);
  if (TT_MOTOR == 1) curM1 = cur; else curM2 = cur;
  ifxSet(TT_MOTOR, cur);

  int x = RemoteXY.joy_x;
  if (abs(x) < DEADZONE) x = 0;
  servoWrite(SERVO_CENTER + map(x, -100, 100, -SERVO_RANGE, SERVO_RANGE));

#else  // ROBOFUT
  int thr  = joyToSpeed(RemoteXY.joy_y);
  int turn = joyToSpeed(RemoteXY.joy_x);

  int tL = constrain(thr + turn, -MOTOR_MAX, MOTOR_MAX);
  int tR = constrain(thr - turn, -MOTOR_MAX, MOTOR_MAX);
  curM1 = slew(curM1, tL);
  curM2 = slew(curM2, tR);
  ifxSet(1, curM1);
  ifxSet(2, curM2);

  int micro = 0;
  if (RemoteXY.btn_fwd) micro =  MICRO_SPEED;
  if (RemoteXY.btn_rev) micro = -MICRO_SPEED;
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
  neo(40, 0, 0);

  RemoteXY_Init();
  Serial.println("Esperando app RemoteXY...");
}

void loop() {
  RemoteXY_Handler();   // llamar seguido, sin delay() largos

  if (millis() - lastLoop < LOOP_MS) return;
  lastLoop = millis();

  if (RemoteXY.connect_flag) {
    if (!wasConnected) {
      wasConnected = true;
      Serial.println("App conectada");
      neo(0, 40, 0);
    }
    processApp();
  } else if (wasConnected) {
    wasConnected = false;
    stopAll();   // failsafe
    Serial.println("App desconectada -> motores parados");
    neo(40, 0, 0);
  }
}
