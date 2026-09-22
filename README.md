# Robot de Fútbol RC

Robot de fútbol robótico (15x15cm) para RoboMatrix Santo Domingo 2026, categoría RoboFut.

## Hardware

- ESP32 (Bluetooth Low Energy)
- Driver de motores TB6612FNG
- 2 motores TT con ruedas de goma
- Giroscopio/acelerómetro MPU6050
- Batería LiPo 2S (7.4V)
- Buck converter LM2596 (7.4V para motores) + regulación separada de 5V para lógica

## Conexiones

### Alimentación

| Desde | Hacia |
|---|---|
| LiPo 2S (7.4V) | VM del TB6612 (motores, directo, sin regular) |
| ESP32 (pin 5V) | VCC del TB6612 (lógica) |
| GND (LiPo, LM2596, TB6612, ESP32, MPU6050) | **Todos unidos en un mismo punto de tierra común** |

### TB6612FNG ↔ ESP32 (motores)

| Pin TB6612 | GPIO ESP32 |
|---|---|
| STBY | 13 |
| AIN1 | 27 |
| AIN2 | 26 |
| PWMA | 14 |
| BIN1 | 25 |
| BIN2 | 33 |
| PWMB | 32 |
| AO1 / AO2 | Terminales motor A (izquierdo) |
| BO1 / BO2 | Terminales motor B (derecho) |

### MPU6050 ↔ ESP32 (I2C)

| Pin MPU6050 | Conecta a |
|---|---|
| VCC | 3.3V del ESP32 (no 5V) |
| GND | GND común |
| SCL | GPIO 22 |
| SDA | GPIO 21 |

### Recomendaciones físicas

- Capacitor **electrolítico 100-470µF** entre VM y GND, lo más cerca posible del TB6612 — reduce caídas de voltaje al arrancar motores.
- Capacitores **cerámicos 100nF** en las terminales de cada motor — reducen ruido eléctrico.
- Mantener los cables de SDA/SCL alejados o cruzados en ángulo recto (no paralelos) respecto a los cables de motores, para evitar interferencia en las lecturas del MPU6050.
- Usar cable más grueso (no jumpers finos) para las conexiones de GND y VM que llevan la corriente de los motores — un jumper delgado puede generar suficiente resistencia para causar reinicios del ESP32 a máxima potencia.
- Evitar que cables pasen sobre la zona de la antena del ESP32, ya que reduce el alcance Bluetooth.

## App de control

**BLE Controller** (Circuitmagic) — Android. Se conecta al dispositivo Bluetooth y usa los controles de dirección, botones extra y el slider de velocidad.

## Código de seguridad

El robot **no acepta ningún comando de movimiento hasta que reciba el código correcto**. Esto evita que otro equipo controle tu robot por accidente o a propósito.

- **Código:** `2167`
- **Cómo se activa:** configura uno de los botones libres de la app (ej. el botón **A**) para que mande el texto `2167` en vez de una acción de movimiento. La primera vez que te conectes, presiona ese botón una sola vez — después de eso, todos los comandos normales (mover, detener, etc.) funcionan sin volver a pedirlo, hasta que el Bluetooth se desconecte.
- Si el Bluetooth se desconecta y vuelve a conectar, hay que presionar el botón del código de nuevo antes de mover el robot.

### Modo escaneo (para conectar un dispositivo nuevo)

Si necesitas conectar un celular distinto sin saber el código, escribe `escaneo` en el monitor serial del ESP32 (por USB) y presiona Enter. Durante los siguientes **30 segundos**, el próximo dispositivo que se conecte entra sin pedir el código.

## Comandos

| Comando enviado | Acción |
|---|---|
| `UP` | Avanza adelante |
| `DOWN` | Retrocede |
| `LEFT` | Gira a la izquierda (sobre su propio eje) |
| `RIGHT` | Gira a la derecha (sobre su propio eje) |
| `C` | Detiene los motores |
| `HORN` | Bocina (reservado, sin función de sonido implementada aún) |
| `Speed_XX` | Ajusta la velocidad base (XX = 0 a 100, el slider de la app) |
| `2167` | Código de seguridad — habilita el resto de comandos |

## Notas de calibración

- `factorMotorA` / `factorMotorB` en el código compensan que un motor gire más rápido que el otro — ajustar según pruebas físicas.
- El PWM máximo real (255 = 100%) puede necesitar limitarse según la capacidad de corriente de tu fuente/buck converter — si notas reinicios del ESP32 a velocidades altas, reduce el límite superior en la línea `map(valorSlider, 0, 100, 0, 255)` del código.
