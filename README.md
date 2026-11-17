# Robomatrix 2026 – Prueba de hardware ESP32-S3

Código de prueba para la tarjeta de control con **ESP32-S3-WROOM-1U**.
Sirve para las categorías **Todoterreno** y **Robofut**.

## Hardware

- Tarjeta de control: ESP32-S3-WROOM-1U (antena externa U.FL)
- Drivers de motor: 2 × tarjeta IFX007T (conector JST-XH 6 pines)
- Driver de micromotores: TB6612FNG (en la tarjeta de control)
- Servo: MG996R
- LED: WS2812 (NeoPixel)

## Pines

| Función | GPIO |
|---|---|
| M1_IN1 / M1_IN2 / M1_INH | 13 / 12 / 11 |
| M2_IN1 / M2_IN2 / M2_INH | 18 / 17 / 16 |
| IS1 / IS2 | 4 / 5 |
| PWMA / AIN1 / AIN2 | 14 / 9 / 10 |
| PWMB / BIN1 / BIN2 | 15 / 40 / 39 |
| STBY | 41 |
| SERVO | 21 |
| NEOPIXEL | 38 |
| BOOT | 0 |
| USB D− / D+ | 19 / 20 |

## Conector JST-XH (control ↔ driver)

| Pin | Señal |
|---|---|
| 1 | IN1 |
| 2 | IN2 |
| 3 | INH |
| 4 | IS |
| 5 | GND |
| 6 | VS |

> **Atención:** verifique con un multímetro que el pin 1 de un lado llega al pin 1 del otro lado **antes** de conectar la batería.

## Configuración de Arduino IDE

- **Core:** esp32 by Espressif **3.x**
- **Board:** ESP32S3 Dev Module
- **USB CDC On Boot:** Enabled
- **USB Mode:** Hardware CDC and JTAG
- **Flash Size / PSRAM:** según el módulo
- **Monitor serial:** 115200

## Qué debe pasar

### Al arrancar

```
===== PRUEBA DE HARDWARE ESP32-S3 =====
Reset: POWERON (normal)
Chip: ESP32-S3 rev 0 | CPU 240 MHz
Flash: 8 MB | PSRAM: 8192 KB
```

### Ciclo de prueba (~20 s, se repite)

| Paso | Resultado esperado |
|---|---|
| [1] NeoPixel | Rojo → verde → azul → apagado |
| [2] Servo | 0° → 90° → 180° → 90° |
| [3] Motor 1 | Adelante → para → atrás → para |
| [4] Motor 2 | Adelante → para → atrás → para |
| [5] Micromotores | A y B adelante → para → atrás |
| Fin | NeoPixel verde durante 2 s |

En los pasos 3 y 4, el IS del motor que gira sube. El otro queda cerca de 0 mV.

## Solución de problemas

| Síntoma | Causa probable |
|---|---|
| `Reset: BROWNOUT` | Problema de alimentación |
| El encabezado aparece otra vez | Reset durante la prueba |
| Un motor no gira | Driver desconectado, INH sin señal o cable JST invertido |
| Un motor gira al revés | Cables del motor invertidos |
| IS siempre en 0 | Pin IS sin conexión o resistor incorrecto |
| No hay salida en el serial | `USB CDC On Boot` no está en Enabled |
| No entra en modo programación | Mantenga BOOT presionado, presione RST, suelte BOOT |
