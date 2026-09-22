#include <Arduino.h>
#include <Wire.h>
#include <MPU6050.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_bt.h>

// --- UUIDs del servicio BLE (estandar Nordic UART Service) ---
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" // el ESP32 recibe comandos aqui
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" // el ESP32 podria enviar datos aqui (no usado activamente)

// --- Nombre del dispositivo Bluetooth ---
// Cambia el texto de abajo por el nombre que quieras que aparezca al buscar el dispositivo BLE
#define NOMBRE_DISPOSITIVO "PON-TU-NOMBRE-AQUI"

BLECharacteristic *pTxCharacteristic;
BLEServer *pServerGlobal;
bool deviceConnected = false;
bool autenticado = false; // true una vez que el codigo secreto fue validado

// --- Modo escaneo: permite conectar un dispositivo nuevo sin el codigo, por tiempo limitado ---
bool modoEscaneo = false;
unsigned long inicioEscaneo = 0;
const unsigned long DURACION_ESCANEO = 30000; // 30 segundos
String bufferSerial = ""; // guarda lo que se escribe en el monitor serial hasta que se presiona Enter

// --- Pines de conexion al driver de motores TB6612FNG ---
const int pinSTBY = 13;   
const int pinAIN1 = 27;   
const int pinAIN2 = 26;   
const int pinPWMA = 14;   
const int canalPWMA = 0;  
const int pinBIN1 = 25;   
const int pinBIN2 = 33;  
const int pinPWMB = 32;  
const int canalPWMB = 1;  

int velocidadBase = 200; // velocidad actual (0-255), se actualiza desde el slider de la app

// --- Factores de compensacion si un motor gira mas rapido que el otro ---
// Ajusta estos valores (0.0 a 1.0) hasta que ambas ruedas giren a la misma velocidad
float factorMotorA = 1.0;
float factorMotorB = 0.85;

// --- Giroscopio/acelerometro MPU6050 ---
MPU6050 mpu;
int16_t ax, ay, az; // lecturas crudas del acelerometro
int16_t gx, gy, gz; // lecturas crudas del giroscopio

float anguloX = 0, anguloY = 0, anguloZ = 0; // angulos calculados (inclinacion X/Y, rotacion Z)
float anguloXImpreso = 0, anguloYImpreso = 0, anguloZImpreso = 0; // ultimo valor mostrado en pantalla
const float UMBRAL = 2.0; // grados minimos de cambio para considerar un movimiento real (filtra ruido)

unsigned long tiempoAnteriorGiro = 0;
unsigned long ultimaLecturaGiro = 0;
const unsigned long INTERVALO_LECTURA = 100; // lee el sensor cada 100ms

// Calcula la diferencia real entre dos angulos, manejando el cruce por 180/-180 grados
// (sin esto, pasar de 179 a -179 se veria como un salto de 358 grados en vez de 2)
float diferenciaAngular(float actual, float anterior) {
  float diferencia = actual - anterior;
  while (diferencia > 180) diferencia -= 360;
  while (diferencia < -180) diferencia += 360;
  return diferencia;
}

// Controla el motor A: velocidad positiva = adelante, negativa = atras
void motorA(int velocidad) {
  velocidad = velocidad * factorMotorA; // aplica compensacion de desbalance
  if (velocidad >= 0) {
    digitalWrite(pinAIN1, HIGH);
    digitalWrite(pinAIN2, LOW);
  } else {
    digitalWrite(pinAIN1, LOW);
    digitalWrite(pinAIN2, HIGH);
    velocidad = -velocidad; // el PWM siempre debe ser positivo, la direccion ya se puso arriba
  }
  ledcWrite(canalPWMA, velocidad);
}

// Controla el motor B: misma logica que motorA
void motorB(int velocidad) {
  velocidad = velocidad * factorMotorB;
  if (velocidad >= 0) {
    digitalWrite(pinBIN1, HIGH);
    digitalWrite(pinBIN2, LOW);
  } else {
    digitalWrite(pinBIN1, LOW);
    digitalWrite(pinBIN2, HIGH);
    velocidad = -velocidad;
  }
  ledcWrite(canalPWMB, velocidad);
}

// Detiene ambos motores
void detenerMotores() {
  motorA(0);
  motorB(0);
}

// --- Eventos de conexion/desconexion BLE ---
class MyServerCallbacks: public BLEServerCallbacks {
    // Se ejecuta cuando un dispositivo se conecta
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      // Si esta en modo escaneo, entra directo sin pedir el codigo secreto
      autenticado = modoEscaneo;
      Serial.println(modoEscaneo ? "Conectado en modo escaneo! Sin codigo." : "Conectado! Esperando codigo...");
    }
    // Se ejecuta cuando el dispositivo se desconecta
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      autenticado = false; // hay que volver a mandar el codigo en la siguiente conexion
      Serial.println("Desconectado! Reintentando...");
      detenerMotores(); // seguridad: para los motores si se pierde la conexion
      delay(50);
      pServer->startAdvertising(); // vuelve a anunciarse para que se pueda reconectar
    }
};

// --- Se ejecuta cada vez que llega un mensaje/comando desde la app ---
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue().c_str();
      if (value.length() > 0) {

        Serial.print("Mensaje completo: ");
        Serial.println(value);

        // Mientras no se haya validado el codigo, ignora cualquier otro comando
        if (!autenticado) {
          if (value == "2167") { // <-- cambia este codigo secreto si quieres
            autenticado = true;
            Serial.println("Codigo correcto! Comandos habilitados.");
          } else {
            Serial.println("Codigo incorrecto. Comando ignorado.");
          }
          return;
        }

        // --- Comandos de movimiento (nombres tal como los manda la app BLE Controller) ---
        if (value == "UP") {
          motorA(velocidadBase);
          motorB(velocidadBase);
          Serial.println("ADELANTE");
        }
        else if (value == "DOWN") {
          motorA(-velocidadBase);
          motorB(-velocidadBase);
          Serial.println("ATRAS");
        }
        else if (value == "LEFT") {
          // giro sobre su propio eje: una rueda adelante, la otra atras, a media velocidad
          motorA(-velocidadBase / 2);
          motorB(velocidadBase / 2);
          Serial.println("IZQUIERDA");
        }
        else if (value == "RIGHT") {
          motorA(velocidadBase / 2);
          motorB(-velocidadBase / 2);
          Serial.println("DERECHA");
        }
        else if (value == "C") {
          detenerMotores();
          Serial.println("STOP");
        }
        else if (value == "HORN") {
          Serial.println("BOCINA");
        }
        // El slider de velocidad manda algo como "Speed_45" (numero del 0 al 100)
        else if (value.startsWith("Speed_")) {
          int pos = value.lastIndexOf('_');
          String numeroTexto = value.substring(pos + 1);
          int valorSlider = numeroTexto.toInt();
          // Se limita el PWM real a 180 (no 255) para no exigir demasiada corriente de golpe
          velocidadBase = map(valorSlider, 0, 100, 0, 255);
          Serial.print("Velocidad base actualizada: ");
          Serial.println(velocidadBase);
        }
      }
    }
};

// Interpreta comandos escritos en el monitor serial de la PC (no desde la app)
void procesarComandoSerial(String comando) {
  comando.trim();
  if (comando == "escaneo") {
    modoEscaneo = true;
    inicioEscaneo = millis();
    Serial.println(">>> MODO ESCANEO ACTIVADO por 30 segundos <<<");
  }
}

void setup() {
  Serial.begin(115200);

  // --- Configuracion de pines del driver de motores ---
  pinMode(pinSTBY, OUTPUT);
  pinMode(pinAIN1, OUTPUT);
  pinMode(pinAIN2, OUTPUT);
  pinMode(pinBIN1, OUTPUT);
  pinMode(pinBIN2, OUTPUT);

  ledcSetup(canalPWMA, 5000, 8); // canal, frecuencia 5kHz, resolucion 8 bits (0-255)
  ledcAttachPin(pinPWMA, canalPWMA);
  ledcSetup(canalPWMB, 5000, 8);
  ledcAttachPin(pinPWMB, canalPWMB);

  digitalWrite(pinSTBY, HIGH); // habilita el driver (sin esto, no se mueve nada)

  // --- Inicializacion del giroscopio ---
  Wire.begin(21, 22); // SDA, SCL
  mpu.initialize();
  if (mpu.testConnection()) {
    Serial.println("MPU6050 conectado correctamente!");
  } else {
    Serial.println("Error: no se detecta el MPU6050");
  }
  tiempoAnteriorGiro = millis();
  ultimaLecturaGiro = millis();

  // --- Inicializacion de Bluetooth (BLE) ---
  BLEDevice::init(NOMBRE_DISPOSITIVO);

  // Sube la potencia de transmision al maximo para mejorar el alcance
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);

  pServerGlobal = BLEDevice::createServer();
  pServerGlobal->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServerGlobal->createService(SERVICE_UUID);

  // Caracteristica de notificacion (ESP32 -> app), no se usa activamente pero queda lista
  pTxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_TX,
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
  pTxCharacteristic->addDescriptor(new BLE2902());

  // Caracteristica de escritura (app -> ESP32), aqui llegan todos los comandos
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_RX,
                        BLECharacteristic::PROPERTY_WRITE
                      );
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();

  // Advertising mas rapido para que la app encuentre el dispositivo mas agil
  BLEAdvertising *pAdvertising = pServerGlobal->getAdvertising();
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->start();

  Serial.println("Esperando conexion BLE...");
  Serial.println("Escribe 'escaneo' aqui para permitir un nuevo dispositivo por 30s.");
}

void loop() {
  unsigned long ahora = millis();

  // --- Lee comandos escritos en el monitor serial de la PC (ej: "escaneo") ---
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      procesarComandoSerial(bufferSerial);
      bufferSerial = "";
    } else {
      bufferSerial += c;
    }
  }

  // Apaga el modo escaneo automaticamente despues de 30 segundos
  if (modoEscaneo && (ahora - inicioEscaneo >= DURACION_ESCANEO)) {
    modoEscaneo = false;
    Serial.println(">>> MODO ESCANEO DESACTIVADO <<<");
  }

  // --- Lectura del giroscopio cada 100ms (sin bloquear el resto del codigo) ---
  if (ahora - ultimaLecturaGiro >= INTERVALO_LECTURA) {
    float dt = (ahora - tiempoAnteriorGiro) / 1000.0; // tiempo transcurrido en segundos
    tiempoAnteriorGiro = ahora;
    ultimaLecturaGiro = ahora;

    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

    // Angulos de inclinacion (X, Y) calculados con el acelerometro
    float accelAnguloX = atan2(ay, az) * 180.0 / PI;
    float accelAnguloY = atan2(-ax, sqrt((long)ay*ay + (long)az*az)) * 180.0 / PI;

    // Angulo de rotacion (Z) calculado integrando el giroscopio
    float velocidadAngularZ = gz / 131.0; // 131 = sensibilidad del sensor en +-250 grados/seg
    float cambioZ = velocidadAngularZ * dt;

    // Solo actualiza si el cambio supera el umbral (filtra ruido/vibraciones pequenas)
    if (abs(diferenciaAngular(accelAnguloX, anguloX)) > UMBRAL) anguloX = accelAnguloX;
    if (abs(diferenciaAngular(accelAnguloY, anguloY)) > UMBRAL) anguloY = accelAnguloY;
    if (abs(cambioZ) > (UMBRAL * dt)) anguloZ += cambioZ;

    // Solo imprime en pantalla si hubo un cambio real desde la ultima vez mostrada
    if (abs(diferenciaAngular(anguloX, anguloXImpreso)) > UMBRAL ||
        abs(diferenciaAngular(anguloY, anguloYImpreso)) > UMBRAL ||
        abs(diferenciaAngular(anguloZ, anguloZImpreso)) > UMBRAL) {

      Serial.print("X: ");
      Serial.print(anguloX);
      Serial.print("  Y: ");
      Serial.print(anguloY);
      Serial.print("  Z: ");
      Serial.println(anguloZ);

      anguloXImpreso = anguloX;
      anguloYImpreso = anguloY;
      anguloZImpreso = anguloZ;
    }
  }
}
