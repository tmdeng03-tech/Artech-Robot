#include <MPU6050_tockn.h>
#include <Wire.h>
#include <SoftwareSerial.h>

// =====================================================
// MPU6050
// SDA -> A4 Nano
// SCL -> A5 Nano
// =====================================================
MPU6050 mpu6050(Wire);

// =====================================================
// UART NANO <-> UNO
// Nano D5 = RX  <- UNO D7 TX
// Nano D6 = TX  -> UNO D6 RX
// =====================================================
SoftwareSerial mySerial(5, 6);

// =====================================================
// DATA STRING
// =====================================================
String sendData = "";

// =====================================================
// TIME SEND
// =====================================================
unsigned long lastSend = 0;
const unsigned long sendInterval = 50;   // 50ms gui 1 lan

// =====================================================
// LOW PASS FILTER
// alpha cang nho loc cang muot nhung cham hon
// alpha cang lon phan ung nhanh hon nhung nhieu rung hon
// Nen dung 0.15 -> 0.30
// =====================================================
float alpha = 0.20;

float filterX = 0;
float filterY = 0;
float filterZ = 0;

bool firstRead = true;

void setup()
{
  Serial.begin(9600);
  mySerial.begin(9600);

  Wire.begin();

  mpu6050.begin();

  // Giu MPU dung yen khi calibrate
  mpu6050.calcGyroOffsets(true);

  Serial.println("NANO MPU READY");
  Serial.println("LOW PASS FILTER ENABLED");
}

void loop()
{
  // Cap nhat MPU6050 lien tuc
  mpu6050.update();

  // Doc goc raw tu MPU
  float rawX = mpu6050.getAngleX();
  float rawY = mpu6050.getAngleY();
  float rawZ = mpu6050.getAngleZ();

  // =====================================================
  // LOC THONG THAP
  // filtered = alpha * raw + (1 - alpha) * filtered_old
  // =====================================================
  if (firstRead)
  {
    filterX = rawX;
    filterY = rawY;
    filterZ = rawZ;
    firstRead = false;
  }
  else
  {
    filterX = alpha * rawX + (1.0 - alpha) * filterX;
    filterY = alpha * rawY + (1.0 - alpha) * filterY;
    filterZ = alpha * rawZ + (1.0 - alpha) * filterZ;
  }

  // Gui du lieu dinh ky
  if (millis() - lastSend >= sendInterval)
  {
    lastSend = millis();

    int x = (int)filterX;
    int y = (int)filterY;
    int z = (int)filterZ;

    // =================================================
    // TAO CHUOI
    // Dang: x90y90z45h
    // h la ky tu ket thuc goi du lieu
    // =================================================
    sendData = "x" +
               String(x) +
               "y" +
               String(y) +
               "z" +
               String(z) +
               "h";

    // Gui UART sang UNO
    mySerial.println(sendData);

    // Debug Serial Monitor Nano
    Serial.print("RAW Z:");
    Serial.print(rawZ);

    Serial.print(" | FILTER Z:");
    Serial.print(filterZ);

    Serial.print(" | Da gui: ");
    Serial.println(sendData);
  }
}