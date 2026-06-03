#include <Arduino.h>
#include <SoftwareSerial.h>

// =====================================================
// UART GUI XUONG STM / HOVERBOARD
// UNO D4 RX <- TX STM / Hoverboard
// UNO D5 TX -> RX STM / Hoverboard
// =====================================================
#define HOVER_RX 4
#define HOVER_TX 5
SoftwareSerial HoverSerial(HOVER_RX, HOVER_TX);

#define HOVER_BAUD 115200
#define START_FRAME 0xABCD

// =====================================================
// THONG SO BANH XE
// =====================================================
float WHEEL_CIRCUMFERENCE_INCH = 2.75;
float WHEEL_BASE_CM = 35.0;
float TARGET_ANGLE = 90.0;

// =====================================================
// TOC DO QUAY
// =====================================================

// Toc do quay
int TURN_WHEEL_SPEED = 150;

// RPM uoc tinh khi TURN_WHEEL_SPEED = 150
float WHEEL_RPM_AT_TURN = 52.5;

// Xe dang quay gan 1 vong, nen giam thoi gian xuong 1/4
// Neu van qua 90 -> giam 0.22
// Neu thieu 90   -> tang 0.28
float TURN_TIME_SCALE = 0.25;

// Neu L/R bi nguoc thi doi thanh -1
int TURN_DIRECTION_SIGN = 1;

// =====================================================
// HOVERBOARD STRUCT
// =====================================================
typedef struct {
  uint16_t start;
  int16_t steer;
  int16_t speed;
  uint16_t checksum;
} SerialCommand;

SerialCommand Command;

// =====================================================
// GUI LENH XUONG STM
// =====================================================
void sendHover(int16_t steer, int16_t speed)
{
  Command.start = START_FRAME;
  Command.steer = steer;
  Command.speed = speed;
  Command.checksum = Command.start ^ Command.steer ^ Command.speed;

  HoverSerial.write((uint8_t *)&Command, sizeof(Command));
}

void motorStop()
{
  sendHover(0, 0);
}

// =====================================================
// DOI LEFT / RIGHT THANH STEER / SPEED
// left  = speed + steer
// right = speed - steer
// =====================================================
void driveWheels(int leftSpeed, int rightSpeed)
{
  int speed = (leftSpeed + rightSpeed) / 2;
  int steer = (leftSpeed - rightSpeed) / 2;

  sendHover(steer, speed);
}

// =====================================================
// QUAY TAI CHO
// dir =  1: quay phai
// dir = -1: quay trai
// =====================================================
void rotateInPlace(int dir, int wheelSpeed)
{
  dir = dir * TURN_DIRECTION_SIGN;

  if (dir > 0)
  {
    driveWheels(wheelSpeed, -wheelSpeed);
  }
  else
  {
    driveWheels(-wheelSpeed, wheelSpeed);
  }
}

void clearSerialInput()
{
  while (Serial.available())
  {
    Serial.read();
  }
}

// =====================================================
// TINH TOAN BANH XE
// =====================================================
float inchToCm(float inch)
{
  return inch * 2.54;
}

float getWheelCircumferenceCm()
{
  return inchToCm(WHEEL_CIRCUMFERENCE_INCH);
}

float getWheelArcLengthCm(float angleDeg)
{
  float turnRadius = WHEEL_BASE_CM / 2.0;
  return 2.0 * PI * turnRadius * angleDeg / 360.0;
}

float getWheelRevolutionForAngle(float angleDeg)
{
  return getWheelArcLengthCm(angleDeg) / getWheelCircumferenceCm();
}

unsigned long getTurnTimeMs(float angleDeg)
{
  float wheelRev = getWheelRevolutionForAngle(angleDeg);
  float wheelRPS = WHEEL_RPM_AT_TURN / 60.0;

  if (wheelRPS <= 0.01)
  {
    wheelRPS = 0.01;
  }

  float timeSec = wheelRev / wheelRPS;
  timeSec = timeSec * TURN_TIME_SCALE;

  return (unsigned long)(timeSec * 1000.0);
}

// =====================================================
// TEST QUAY 1 GIAY
// A = quay trai 1 giay
// D = quay phai 1 giay
// =====================================================
void testSpinFixed(int dir)
{
  clearSerialInput();

  motorStop();
  delay(200);

  Serial.println();
  Serial.println("==================================");
  Serial.print("TEST SPIN ");
  Serial.println(dir < 0 ? "LEFT 1 SECOND" : "RIGHT 1 SECOND");
  Serial.print("TURN_WHEEL_SPEED = ");
  Serial.println(TURN_WHEEL_SPEED);
  Serial.println("==================================");

  unsigned long startTime = millis();

  while (millis() - startTime < 1000)
  {
    rotateInPlace(dir, TURN_WHEEL_SPEED);
    delay(10);
  }

  motorStop();
  delay(300);

  clearSerialInput();

  Serial.println("TEST SPIN DONE");
}

// =====================================================
// QUAY 90 DO
// =====================================================
void turnCalculated90(int dir)
{
  clearSerialInput();

  motorStop();
  delay(300);

  float wheelCircumferenceCm = getWheelCircumferenceCm();
  float turnRadius = WHEEL_BASE_CM / 2.0;
  float arcLengthCm = getWheelArcLengthCm(TARGET_ANGLE);
  float wheelRev = getWheelRevolutionForAngle(TARGET_ANGLE);
  unsigned long turnTime = getTurnTimeMs(TARGET_ANGLE);

  Serial.println();
  Serial.println("==================================");
  Serial.print("START TURN ");
  Serial.println(dir < 0 ? "LEFT 90" : "RIGHT 90");

  Serial.print("TURN_WHEEL_SPEED = ");
  Serial.println(TURN_WHEEL_SPEED);

  Serial.print("Wheel circumference cm = ");
  Serial.println(wheelCircumferenceCm, 3);

  Serial.print("Wheel base cm = ");
  Serial.println(WHEEL_BASE_CM);

  Serial.print("Turn radius cm = ");
  Serial.println(turnRadius, 3);

  Serial.print("Arc length each wheel cm = ");
  Serial.println(arcLengthCm, 3);

  Serial.print("Wheel revolution need = ");
  Serial.println(wheelRev, 4);

  Serial.print("Wheel RPM setting = ");
  Serial.println(WHEEL_RPM_AT_TURN);

  Serial.print("TURN_TIME_SCALE = ");
  Serial.println(TURN_TIME_SCALE);

  Serial.print("Turn time ms = ");
  Serial.println(turnTime);

  Serial.println("NO MPU");
  Serial.println("NO LINE");
  Serial.println("TARGET = 90 DEG");
  Serial.println("WHEEL_BASE = 35 CM");
  Serial.println("TURN_SPEED = 150");
  Serial.println("==================================");

  unsigned long startTime = millis();
  unsigned long lastPrint = 0;

  while (millis() - startTime < turnTime)
  {
    rotateInPlace(dir, TURN_WHEEL_SPEED);

    if (millis() - lastPrint >= 100)
    {
      lastPrint = millis();

      Serial.print("Turning... ");
      Serial.print(millis() - startTime);
      Serial.print(" / ");
      Serial.println(turnTime);
    }

    delay(10);
  }

  motorStop();
  delay(500);

  clearSerialInput();

  Serial.println("==================================");
  Serial.println("TURN 90 DONE - STOP");
  Serial.println("==================================");
  Serial.println();
}

// =====================================================
// SETUP
// =====================================================
void setup()
{
  Serial.begin(9600);
  HoverSerial.begin(HOVER_BAUD);

  delay(1000);

  motorStop();

  Serial.println("==================================");
  Serial.println("CALCULATED WHEEL TURN 90 TEST READY");
  Serial.println("WHEEL_BASE = 35 CM");
  Serial.println("TURN_WHEEL_SPEED = 150");
  Serial.println("TURN_TIME_SCALE = 0.25");
  Serial.println("==================================");

  Serial.println("A = test quay trai 1 giay");
  Serial.println("D = test quay phai 1 giay");
  Serial.println("L = quay trai 90 do");
  Serial.println("R = quay phai 90 do");
  Serial.println("S = dung");
  Serial.println();

  Serial.print("TURN_WHEEL_SPEED = ");
  Serial.println(TURN_WHEEL_SPEED);

  Serial.print("WHEEL_RPM_AT_TURN = ");
  Serial.println(WHEEL_RPM_AT_TURN);

  Serial.print("TURN_TIME_SCALE = ");
  Serial.println(TURN_TIME_SCALE);

  Serial.print("Wheel circumference inch = ");
  Serial.println(WHEEL_CIRCUMFERENCE_INCH);

  Serial.print("Wheel circumference cm = ");
  Serial.println(getWheelCircumferenceCm(), 3);

  Serial.print("Wheel base cm = ");
  Serial.println(WHEEL_BASE_CM);

  Serial.print("Turn radius cm = ");
  Serial.println(WHEEL_BASE_CM / 2.0);

  Serial.print("Arc length for 90 deg cm = ");
  Serial.println(getWheelArcLengthCm(90.0), 3);

  Serial.print("Wheel rev for 90 deg = ");
  Serial.println(getWheelRevolutionForAngle(90.0), 4);

  Serial.print("Turn time ms = ");
  Serial.println(getTurnTimeMs(90.0));

  Serial.println("==================================");

  clearSerialInput();
}

// =====================================================
// LOOP
// =====================================================
void loop()
{
  if (Serial.available())
  {
    char c = Serial.read();

    if (c == '\n' || c == '\r')
    {
      return;
    }

    if (c == 'A' || c == 'a')
    {
      testSpinFixed(-1);
    }
    else if (c == 'D' || c == 'd')
    {
      testSpinFixed(1);
    }
    else if (c == 'L' || c == 'l')
    {
      turnCalculated90(-1);
    }
    else if (c == 'R' || c == 'r')
    {
      turnCalculated90(1);
    }
    else if (c == 'S' || c == 's')
    {
      motorStop();
      clearSerialInput();
      Serial.println("STOP");
    }
  }
}