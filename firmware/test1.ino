// =============================================================
//  Prueba de hardware - ESP32-S3-WROOM-1
//  Robomatrix 2026: Todoterreno / Robofut
//  Core Arduino ESP32 3.x
// =============================================================
#include "esp_system.h"

// ---------- Pines: driver IFX007T Motor 1 (CN4) ----------
#define M1_IN1    13
#define M1_IN2    12
#define M1_INH    11
#define IS1        4

// ---------- Pines: driver IFX007T Motor 2 (CN5) ----------
#define M2_IN1    18
#define M2_IN2    17
#define M2_INH    16
#define IS2        5

// ---------- Pines: TB6612FNG (micromotores) ----------
#define PWMA      14
#define AIN1       9
#define AIN2      10
#define PWMB      15
#define BIN1      40
#define BIN2      39
#define STBY      41

// ---------- Periféricos ----------
#define SERVO_PIN 21
#define NEO_PIN   38

// ---------- Canales LEDC ----------
// ESP32-S3: 8 canales, 4 timers. Timer = canal / 2.
// Los canales que comparten timer deben tener la misma frecuencia.
#define CH_M1_IN1  0   // Timer 0 - 20 kHz
#define CH_M1_IN2  1   // Timer 0 - 20 kHz
#define CH_M2_IN1  2   // Timer 1 - 20 kHz
#define CH_M2_IN2  3   // Timer 1 - 20 kHz
#define CH_PWMA    4   // Timer 2 - 20 kHz
#define CH_PWMB    5   // Timer 2 - 20 kHz
#define CH_SERVO   6   // Timer 3 - 50 Hz (canal 7 libre)

#define MOTOR_FREQ   20000
#define MOTOR_RES    10            // 0..1023
#define MOTOR_MAX    1023

#define SERVO_FREQ   50
#define SERVO_RES    14            // 0..16383
#define SERVO_PERIOD_US 20000

// =============================================================
//  Funciones: IFX007T (puente H con 2 medios puentes)
// =============================================================
void ifxInit() {
  pinMode(M1_INH, OUTPUT);
  pinMode(M2_INH, OUTPUT);
  digitalWrite(M1_INH, LOW);
  digitalWrite(M2_INH, LOW);

  ledcAttachChannel(M1_IN1, MOTOR_FREQ, MOTOR_RES, CH_M1_IN1);
  ledcAttachChannel(M1_IN2, MOTOR_FREQ, MOTOR_RES, CH_M1_IN2);
  ledcAttachChannel(M2_IN1, MOTOR_FREQ, MOTOR_RES, CH_M2_IN1);
  ledcAttachChannel(M2_IN2, MOTOR_FREQ, MOTOR_RES, CH_M2_IN2);

  ledcWrite(M1_IN1, 0);
  ledcWrite(M1_IN2, 0);
  ledcWrite(M2_IN1, 0);
  ledcWrite(M2_IN2, 0);
}

// motor: 1 o 2 | speed: -1023 (atrás) .. +1023 (adelante)
void ifxSet(uint8_t motor, int speed) {
  speed = constrain(speed, -MOTOR_MAX, MOTOR_MAX);
  uint8_t in1 = (motor == 1) ? M1_IN1 : M2_IN1;
  uint8_t in2 = (motor == 1) ? M1_IN2 : M2_IN2;
  uint8_t inh = (motor == 1) ? M1_INH : M2_INH;

  if (speed == 0) {
    ledcWrite(in1, 0);
    ledcWrite(in2, 0);
    digitalWrite(inh, LOW);
    return;
  }
  digitalWrite(inh, HIGH);
  if (speed > 0) {
    ledcWrite(in2, 0);
    ledcWrite(in1, speed);
  } else {
    ledcWrite(in1, 0);
    ledcWrite(in2, -speed);
  }
}

// Rampa para evitar picos de corriente
void ifxRamp(uint8_t motor, int target, uint16_t ms) {
  const int steps = 50;
  for (int i = 0; i <= steps; i++) {
    ifxSet(motor, (long)target * i / steps);
    delay(ms / steps);
  }
}

// =============================================================
//  Funciones: TB6612FNG
// =============================================================
void tbInit() {
  pinMode(STBY, OUTPUT);
  digitalWrite(STBY, LOW);
  pinMode(AIN1, OUTPUT);
  pinMode(AIN2, OUTPUT);
  pinMode(BIN1, OUTPUT);
  pinMode(BIN2, OUTPUT);

  ledcAttachChannel(PWMA, MOTOR_FREQ, MOTOR_RES, CH_PWMA);
  ledcAttachChannel(PWMB, MOTOR_FREQ, MOTOR_RES, CH_PWMB);
  ledcWrite(PWMA, 0);
  ledcWrite(PWMB, 0);
}

// ch: 'A' o 'B' | speed: -1023 .. +1023
void tbSet(char ch, int speed) {
  speed = constrain(speed, -MOTOR_MAX, MOTOR_MAX);
  uint8_t p  = (ch == 'A') ? PWMA : PWMB;
  uint8_t d1 = (ch == 'A') ? AIN1 : BIN1;
  uint8_t d2 = (ch == 'A') ? AIN2 : BIN2;

  digitalWrite(STBY, HIGH);
  if (speed > 0)      { digitalWrite(d1, HIGH); digitalWrite(d2, LOW);  }
  else if (speed < 0) { digitalWrite(d1, LOW);  digitalWrite(d2, HIGH); }
  else                { digitalWrite(d1, LOW);  digitalWrite(d2, LOW);  }
  ledcWrite(p, abs(speed));
}

// =============================================================
//  Funciones: Servo
// =============================================================
void servoInit() {
  ledcAttachChannel(SERVO_PIN, SERVO_FREQ, SERVO_RES, CH_SERVO);
}

// angle: 0..180  (pulso 500..2500 us)
void servoWrite(int angle) {
  angle = constrain(angle, 0, 180);
  uint32_t us   = map(angle, 0, 180, 500, 2500);
  uint32_t duty = (us * ((1UL << SERVO_RES) - 1)) / SERVO_PERIOD_US;
  ledcWrite(SERVO_PIN, duty);
}

// =============================================================
//  Funciones: Sensado de corriente (IS)
// =============================================================
void isInit() {
  analogReadResolution(12);
  analogSetPinAttenuation(IS1, ADC_11db);   // rango ~0..3.1 V
  analogSetPinAttenuation(IS2, ADC_11db);
}

// =============================================================
//  Utilidades
// =============================================================
void neo(uint8_t r, uint8_t g, uint8_t b) {
  rgbLedWrite(NEO_PIN, r, g, b);   // usa RMT, no LEDC
}

const char* resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "POWERON (normal)";
    case ESP_RST_SW:       return "SOFTWARE";
    case ESP_RST_PANIC:    return "PANIC (error de firmware)";
    case ESP_RST_INT_WDT:  return "INT WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK WATCHDOG";
    case ESP_RST_BROWNOUT: return "BROWNOUT (caida de voltaje)";
    case ESP_RST_EXT:      return "EXTERNO (boton RST)";
    default:               return "OTRO";
  }
}

void printIS() {
  Serial.printf("  IS1 = %4d mV | IS2 = %4d mV\n",
                analogReadMilliVolts(IS1),
                analogReadMilliVolts(IS2));
}

// =============================================================
//  Setup
// =============================================================
void setup() {
  // 1. Primero, salidas en estado seguro
  ifxInit();
  tbInit();
  servoInit();
  isInit();

  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) { delay(10); }

  Serial.println("\n===== PRUEBA DE HARDWARE ESP32-S3 =====");
  Serial.printf("Reset: %s\n", resetReasonStr(esp_reset_reason()));
  Serial.printf("Chip: %s rev %d | CPU %d MHz\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getCpuFreqMHz());
  Serial.printf("Flash: %d MB | PSRAM: %d KB\n",
                ESP.getFlashChipSize() / (1024 * 1024),
                ESP.getPsramSize() / 1024);
}

// =============================================================
//  Loop: secuencia de prueba
// =============================================================
void loop() {
  // --- 1. NeoPixel ---
  Serial.println("\n[1] NeoPixel: rojo, verde, azul");
  neo(40, 0, 0); delay(500);
  neo(0, 40, 0); delay(500);
  neo(0, 0, 40); delay(500);
  neo(0, 0, 0);

  // --- 2. Servo ---
  Serial.println("[2] Servo: 0 -> 90 -> 180 -> 90");
  servoWrite(0);   delay(800);
  servoWrite(90);  delay(800);
  servoWrite(180); delay(800);
  servoWrite(90);  delay(800);

  // --- 3. IFX Motor 1 ---
  Serial.println("[3] IFX Motor 1: adelante / atras");
  neo(40, 20, 0);
  ifxRamp(1,  600, 300); delay(1000); printIS();
  ifxRamp(1,    0, 300); delay(300);
  ifxRamp(1, -600, 300); delay(1000); printIS();
  ifxRamp(1,    0, 300);

  // --- 4. IFX Motor 2 ---
  Serial.println("[4] IFX Motor 2: adelante / atras");
  ifxRamp(2,  600, 300); delay(1000); printIS();
  ifxRamp(2,    0, 300); delay(300);
  ifxRamp(2, -600, 300); delay(1000); printIS();
  ifxRamp(2,    0, 300);

  // --- 5. TB6612 ---
  Serial.println("[5] TB6612: canal A y B, adelante / atras");
  neo(0, 20, 40);
  tbSet('A',  600); tbSet('B',  600); delay(1000);
  tbSet('A',    0); tbSet('B',    0); delay(300);
  tbSet('A', -600); tbSet('B', -600); delay(1000);
  tbSet('A',    0); tbSet('B',    0);
  digitalWrite(STBY, LOW);

  // --- Fin ---
  neo(0, 40, 0);
  Serial.printf("Ciclo completo. Uptime: %lu s\n", millis() / 1000);
  delay(2000);
}
