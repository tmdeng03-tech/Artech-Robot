#include <Arduino.h>
#include <SoftwareSerial.h>

// =====================================================
// UART MPU NANO
// UNO D6 RX <- Nano TX
// UNO D7 TX -> Nano RX
// =====================================================
#define MPU_RX 6
#define MPU_TX 7
SoftwareSerial MpuSerial(MPU_RX, MPU_TX);

// =====================================================
// UART HOVERBOARD
// UNO D4 RX <- TX STM
// UNO D5 TX -> RX STM
// =====================================================
#define HOVER_RX 4
#define HOVER_TX 5
SoftwareSerial HoverSerial(HOVER_RX, HOVER_TX);

#define HOVER_BAUD 115200
#define START_FRAME 0xABCD

// =====================================================
// LINE SENSOR
// OUT2 -> A1
// OUT3 -> A2
// OUT4 -> A3
// Co line       -> OUT = 0
// Khong co line -> OUT = 1
// =====================================================
#define OUT2_PIN A1
#define OUT3_PIN A2
#define OUT4_PIN A3
#define LINE_THRESHOLD 500

// =====================================================
// BUTTON RETURN
// 1 chan nut -> D2
// 1 chan nut -> GND
// =====================================================
#define BUTTON_PIN 2

// =====================================================
// SPEED CONFIG
// =====================================================
int BASE_SPEED = 100;
int LINE_INNER_SPEED = 40;
int LINE_OUTER_SPEED = 110;

int LINE_DIRECTION_SIGN = 1;

// =====================================================
// TURN CONFIG
// =====================================================
float WHEEL_CIRCUMFERENCE_INCH = 2.75;
float WHEEL_BASE_CM = 35.0;

// CHI CAN DOI BIEN NAY
// Vi du: 100, 150, 200
int TURN_WHEEL_SPEED = 150;

// He so tu test thuc te:
// speed = 150 thi RPM xap xi 52.5
// 52.5 / 150 = 0.35 RPM / 1 don vi speed
float RPM_PER_SPEED_UNIT = 0.35;

// He so hieu chinh co dinh sau khi da can chuan
// Doi TURN_WHEEL_SPEED thi khong can doi scale nay
float TURN_90_TIME_SCALE = 0.275;
float TURN_180_TIME_SCALE = 0.263;

float TURN_90_TARGET = 90.0;
float TURN_BACK_TARGET = 180.0;

int TURN_DIRECTION_SIGN = 1;

// =====================================================
// COMMAND BUFFER - TIET KIEM RAM
// =====================================================
#define MAX_CMD 12
#define INPUT_BUF_SIZE 32
#define MPU_BUF_SIZE 32

char routeCmd[MAX_CMD];
byte routeLen = 0;

char returnCmd[MAX_CMD];
byte returnLen = 0;

char inputBuffer[INPUT_BUF_SIZE];
byte inputIndex = 0;

char mpuBuffer[MPU_BUF_SIZE];
byte mpuIndex = 0;

// =====================================================
// STATES
// =====================================================
bool hasRoute = false;
bool robotBusy = false;
bool waitingReturn = false;
bool manualLineMode = false;
bool receivingCommand = false;
bool lineSensorEnabled = true;

// =====================================================
// MPU DATA
// =====================================================
int mpuX = 0;
int mpuY = 0;
int mpuZ = 0;

float currentYaw = 0;
float returnZeroYaw = 0;

bool mpuUpdated = false;

// =====================================================
// LINE DATA
// =====================================================
int raw2 = 0;
int raw3 = 0;
int raw4 = 0;

byte out2 = 1;
byte out3 = 1;
byte out4 = 1;
byte lineState = 0;

// =====================================================
// DEBUG
// =====================================================
unsigned long lastDebug = 0;
const unsigned long debugInterval = 500;

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
// MOTOR
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

void driveWheels(int leftSpeed, int rightSpeed)
{
  int speed = (leftSpeed + rightSpeed) / 2;
  int steer = (leftSpeed - rightSpeed) / 2;

  sendHover(steer, speed);
}

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

// =====================================================
// STATUS
// =====================================================
void sendStatus(const __FlashStringHelper *msg)
{
  Serial.println(msg);
}

void stopAll()
{
  motorStop();

  robotBusy = false;
  manualLineMode = false;
  receivingCommand = false;

  inputIndex = 0;
  inputBuffer[0] = '\0';

  Serial.println(F("STOP ALL"));
}

// =====================================================
// LINE ON / OFF
// =====================================================
void forceLineOff()
{
  lineSensorEnabled = false;

  raw2 = 0;
  raw3 = 0;
  raw4 = 0;

  out2 = 1;
  out3 = 1;
  out4 = 1;
  lineState = 0;

  Serial.println(F("LINE OFF"));
}

void forceLineOn()
{
  lineSensorEnabled = true;
  Serial.println(F("LINE ON"));
}

// =====================================================
// RESET
// =====================================================
void resetAllCommandData()
{
  routeLen = 0;
  returnLen = 0;
  hasRoute = false;
  waitingReturn = false;
  receivingCommand = false;
  manualLineMode = false;

  inputIndex = 0;
  inputBuffer[0] = '\0';

  mpuIndex = 0;
  mpuBuffer[0] = '\0';

  for (byte i = 0; i < MAX_CMD; i++)
  {
    routeCmd[i] = 0;
    returnCmd[i] = 0;
  }

  forceLineOn();
  motorStop();

  Serial.println(F("RESET DONE"));
}

void resetRuntimeBeforeReturn()
{
  receivingCommand = false;

  inputIndex = 0;
  inputBuffer[0] = '\0';

  mpuIndex = 0;
  mpuBuffer[0] = '\0';

  motorStop();

  Serial.println(F("RESET RUNTIME"));
}

// =====================================================
// MPU - CHI DOC TRUOC / SAU KHI QUAY
// =====================================================
void parseMPUBuffer()
{
  char *px = strchr(mpuBuffer, 'x');
  char *py = strchr(mpuBuffer, 'y');
  char *pz = strchr(mpuBuffer, 'z');

  if (px && py && pz)
  {
    mpuX = atoi(px + 1);
    mpuY = atoi(py + 1);
    mpuZ = atoi(pz + 1);

    currentYaw = mpuZ;
    mpuUpdated = true;
  }
}

void readMPU()
{
  MpuSerial.listen();

  while (MpuSerial.available())
  {
    char c = MpuSerial.read();

    if (c == 'h')
    {
      mpuBuffer[mpuIndex] = '\0';
      parseMPUBuffer();

      mpuIndex = 0;
      mpuBuffer[0] = '\0';
    }
    else if (c != '\n' && c != '\r')
    {
      if (mpuIndex < MPU_BUF_SIZE - 1)
      {
        mpuBuffer[mpuIndex++] = c;
        mpuBuffer[mpuIndex] = '\0';
      }
      else
      {
        mpuIndex = 0;
        mpuBuffer[0] = '\0';
      }
    }
  }
}

bool readMPUForCheck()
{
  mpuUpdated = false;

  unsigned long t = millis();

  while (millis() - t < 500)
  {
    readMPU();
    delay(5);
  }

  return mpuUpdated;
}

float normalizeError(float e)
{
  while (e > 180) e -= 360;
  while (e < -180) e += 360;
  return e;
}

float absFloat(float v)
{
  if (v < 0) return -v;
  return v;
}

// =====================================================
// LINE SENSOR
// =====================================================
void updateLineSensor()
{
  if (!lineSensorEnabled)
  {
    lineState = 0;
    return;
  }

  raw2 = analogRead(OUT2_PIN);
  raw3 = analogRead(OUT3_PIN);
  raw4 = analogRead(OUT4_PIN);

  out2 = raw2 < LINE_THRESHOLD ? 0 : 1;
  out3 = raw3 < LINE_THRESHOLD ? 0 : 1;
  out4 = raw4 < LINE_THRESHOLD ? 0 : 1;

  lineState = 0;

  if (out2 == 0) lineState |= 0b100;
  if (out3 == 0) lineState |= 0b010;
  if (out4 == 0) lineState |= 0b001;
}

byte readLineState()
{
  updateLineSensor();
  return lineState;
}

bool allSensorsZeroMarker()
{
  if (!lineSensorEnabled)
  {
    return false;
  }

  updateLineSensor();

  return out2 == 0 && out3 == 0 && out4 == 0;
}

// =====================================================
// DEBUG
// =====================================================
void printDebug()
{
  if (millis() - lastDebug >= debugInterval)
  {
    lastDebug = millis();

    if (lineSensorEnabled)
    {
      updateLineSensor();

      Serial.print(F("RAW A1:"));
      Serial.print(raw2);
      Serial.print(F(" A2:"));
      Serial.print(raw3);
      Serial.print(F(" A3:"));
      Serial.print(raw4);

      Serial.print(F(" OUT:"));
      Serial.print(out2);
      Serial.print(out3);
      Serial.print(out4);

      Serial.print(F(" ST:"));
      Serial.print(lineState, BIN);
    }
    else
    {
      Serial.print(F("LINE OFF"));
    }

    Serial.print(F(" BUSY:"));
    Serial.print(robotBusy ? F("Y") : F("N"));

    Serial.print(F(" MAN:"));
    Serial.print(manualLineMode ? F("Y") : F("N"));

    Serial.print(F(" WAIT:"));
    Serial.println(waitingReturn ? F("Y") : F("N"));
  }
}

void realtimeUpdate()
{
  if (lineSensorEnabled)
  {
    updateLineSensor();
  }

  printDebug();
}

// =====================================================
// TURN CALCULATION
// =====================================================
float getWheelCircumferenceCm()
{
  return WHEEL_CIRCUMFERENCE_INCH * 2.54;
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

float getTimeScaleForAngle(float angleDeg)
{
  if (angleDeg > 150.0)
  {
    return TURN_180_TIME_SCALE;
  }

  return TURN_90_TIME_SCALE;
}

// Tu tinh RPM theo TURN_WHEEL_SPEED
float getWheelRPMFromSpeed(int wheelSpeed)
{
  if (wheelSpeed < 0)
  {
    wheelSpeed = -wheelSpeed;
  }

  float rpm = wheelSpeed * RPM_PER_SPEED_UNIT;

  if (rpm < 1.0)
  {
    rpm = 1.0;
  }

  return rpm;
}

// Tu tinh thoi gian quay theo:
// - goc can quay
// - khoang cach 2 banh
// - chu vi banh
// - toc do TURN_WHEEL_SPEED
unsigned long getTurnTimeMs(float angleDeg)
{
  float wheelRev = getWheelRevolutionForAngle(angleDeg);

  float wheelRPM = getWheelRPMFromSpeed(TURN_WHEEL_SPEED);
  float wheelRPS = wheelRPM / 60.0;

  if (wheelRPS <= 0.01)
  {
    wheelRPS = 0.01;
  }

  float timeSec = wheelRev / wheelRPS;

  timeSec *= getTimeScaleForAngle(angleDeg);

  return (unsigned long)(timeSec * 1000.0);
}

// =====================================================
// EMERGENCY STOP
// =====================================================
bool checkEmergencyStop()
{
  if (Serial.available())
  {
    char c = Serial.read();

    if (c == 's' || c == 'S')
    {
      Serial.println(F("EMERGENCY STOP"));
      stopAll();
      return true;
    }
  }

  return false;
}

// =====================================================
// TURN
// Quay theo tinh toan banh xe.
// Moi lan quay reset goc ve 0 bang startYaw.
// MPU chi dung de in goc kiem chung.
// =====================================================
void turnCalculatedNoLine(int dir, float degree)
{
  motorStop();
  delay(250);

  forceLineOff();
  delay(150);

  bool gotStartMPU = readMPUForCheck();
  float startYaw = currentYaw;

  float relativeStart = 0.0;

  unsigned long turnTime = getTurnTimeMs(degree);
  float autoRPM = getWheelRPMFromSpeed(TURN_WHEEL_SPEED);

  Serial.println(F("========== TURN START =========="));
  Serial.print(F("TARGET DEG:"));
  Serial.println(degree);

  Serial.print(F("DIR:"));
  Serial.println(dir > 0 ? F("RIGHT") : F("LEFT"));

  Serial.print(F("TURN SPEED:"));
  Serial.println(TURN_WHEEL_SPEED);

  Serial.print(F("AUTO RPM:"));
  Serial.println(autoRPM);

  Serial.print(F("TURN TIME MS:"));
  Serial.println(turnTime);

  if (gotStartMPU)
  {
    Serial.print(F("MPU RAW START Z:"));
    Serial.println(startYaw);

    Serial.print(F("RESET TURN ANGLE TO:"));
    Serial.println(relativeStart);
  }
  else
  {
    Serial.println(F("MPU START ERROR"));
  }

  unsigned long startTime = millis();

  while (millis() - startTime < turnTime)
  {
    if (checkEmergencyStop())
    {
      return;
    }

    rotateInPlace(dir, TURN_WHEEL_SPEED);
    delay(10);
  }

  motorStop();
  delay(400);

  bool gotEndMPU = readMPUForCheck();
  float endYaw = currentYaw;

  float relativeEnd = normalizeError(endYaw - startYaw);
  float absTurnAngle = absFloat(relativeEnd);
  float errorAngle = absTurnAngle - degree;

  Serial.println(F("========== TURN RESULT =========="));

  Serial.print(F("TARGET DEG:"));
  Serial.println(degree);

  Serial.print(F("TURN SPEED:"));
  Serial.println(TURN_WHEEL_SPEED);

  Serial.print(F("AUTO RPM:"));
  Serial.println(autoRPM);

  Serial.print(F("MPU RAW START Z:"));
  Serial.println(startYaw);

  Serial.print(F("MPU RAW END Z:"));
  Serial.println(endYaw);

  Serial.print(F("RELATIVE START DEG:"));
  Serial.println(relativeStart);

  Serial.print(F("RELATIVE END DEG:"));
  Serial.println(relativeEnd);

  Serial.print(F("ABS TURN DEG:"));
  Serial.println(absTurnAngle);

  Serial.print(F("ERROR DEG:"));
  Serial.println(errorAngle);

  if (!gotEndMPU)
  {
    Serial.println(F("WARNING MPU NOT UPDATED"));
  }

  Serial.println(F("================================="));
}

// =====================================================
// FOLLOW LINE
// =====================================================
void followLineStep()
{
  if (!lineSensorEnabled)
  {
    motorStop();
    return;
  }

  byte state = readLineState();

  int leftSpeed = 0;
  int rightSpeed = 0;

  switch (state)
  {
    case 0b010:
      leftSpeed = BASE_SPEED;
      rightSpeed = BASE_SPEED;
      break;

    case 0b110:
    case 0b100:
      leftSpeed = LINE_INNER_SPEED;
      rightSpeed = LINE_OUTER_SPEED;
      break;

    case 0b011:
    case 0b001:
      leftSpeed = LINE_OUTER_SPEED;
      rightSpeed = LINE_INNER_SPEED;
      break;

    case 0b111:
      leftSpeed = BASE_SPEED;
      rightSpeed = BASE_SPEED;
      break;

    case 0b000:
      leftSpeed = BASE_SPEED;
      rightSpeed = BASE_SPEED;
      break;

    default:
      leftSpeed = BASE_SPEED;
      rightSpeed = BASE_SPEED;
      break;
  }

  leftSpeed *= LINE_DIRECTION_SIGN;
  rightSpeed *= LINE_DIRECTION_SIGN;

  driveWheels(leftSpeed, rightSpeed);
}

// =====================================================
// MARKER FUNCTIONS
// =====================================================
bool goStraightThrough3Markers()
{
  forceLineOn();

  byte markerCount = 0;
  bool lastMarker = false;

  unsigned long startEscape = millis();

  while (allSensorsZeroMarker() && millis() - startEscape < 800)
  {
    if (checkEmergencyStop()) return false;

    realtimeUpdate();
    followLineStep();
    delay(5);
  }

  unsigned long routeStartTime = millis();
  const unsigned long ROUTE_TIMEOUT = 15000;

  while (markerCount < 3)
  {
    if (checkEmergencyStop()) return false;

    if (millis() - routeStartTime > ROUTE_TIMEOUT)
    {
      Serial.println(F("LINE TIMEOUT"));
      motorStop();
      robotBusy = false;
      return false;
    }

    realtimeUpdate();

    bool markerNow = allSensorsZeroMarker();

    if (markerNow && !lastMarker)
    {
      markerCount++;

      Serial.print(F("MARKER:"));
      Serial.println(markerCount);

      motorStop();
      delay(80);
    }

    lastMarker = markerNow;

    followLineStep();

    delay(5);
  }

  motorStop();
  delay(120);

  return true;
}

bool goStraightUntilOneMarker()
{
  forceLineOn();

  Serial.println(F("FINAL MARKER WAIT"));

  bool lastMarker = false;

  unsigned long startEscape = millis();

  while (allSensorsZeroMarker() && millis() - startEscape < 1000)
  {
    if (checkEmergencyStop()) return false;

    realtimeUpdate();
    followLineStep();
    delay(5);
  }

  unsigned long finalStartTime = millis();
  const unsigned long FINAL_TIMEOUT = 10000;

  while (true)
  {
    if (checkEmergencyStop()) return false;

    if (millis() - finalStartTime > FINAL_TIMEOUT)
    {
      Serial.println(F("FINAL TIMEOUT"));
      motorStop();
      robotBusy = false;
      return false;
    }

    realtimeUpdate();

    bool markerNow = allSensorsZeroMarker();

    if (markerNow && !lastMarker)
    {
      Serial.println(F("FINAL MARKER"));

      motorStop();
      delay(300);

      forceLineOff();

      bool gotMPU = readMPUForCheck();
      returnZeroYaw = currentYaw;

      if (gotMPU)
      {
        Serial.print(F("RETURN ZERO YAW:"));
        Serial.println(returnZeroYaw);
      }
      else
      {
        Serial.println(F("RETURN ZERO MPU ERROR"));
      }

      break;
    }

    lastMarker = markerNow;

    followLineStep();

    delay(5);
  }

  motorStop();
  delay(200);

  return true;
}

// =====================================================
// TURN FUNCTIONS
// =====================================================
void turnRight90()
{
  turnCalculatedNoLine(1, TURN_90_TARGET);
  forceLineOn();
}

void turnLeft90()
{
  turnCalculatedNoLine(-1, TURN_90_TARGET);
  forceLineOn();
}

void turnBack180LeftCalculatedNoLine()
{
  Serial.println(F("TURN BACK 180"));

  motorStop();
  delay(300);

  forceLineOff();
  delay(300);

  turnCalculatedNoLine(-1, TURN_BACK_TARGET);

  motorStop();
  delay(300);

  Serial.println(F("TURN BACK DONE"));
}

// =====================================================
// ROUTE
// =====================================================
void printRoute(const __FlashStringHelper *label, char *cmdList, byte len)
{
  Serial.print(label);
  Serial.print(F(": a"));

  for (byte i = 0; i < len; i++)
  {
    Serial.print(cmdList[i]);

    if (i < len - 1)
    {
      Serial.print('h');
    }
  }

  Serial.println('n');
}

void buildReturnRoute()
{
  returnLen = 0;

  for (int i = routeLen - 1; i >= 0; i--)
  {
    char c = routeCmd[i];

    if (c == 'R') c = 'L';
    else if (c == 'L') c = 'R';
    else c = 'T';

    if (returnLen < MAX_CMD)
    {
      returnCmd[returnLen++] = c;
    }
  }
}

void parseCommandString(const char *s)
{
  routeLen = 0;
  returnLen = 0;
  hasRoute = false;
  waitingReturn = false;

  byte len = strlen(s);

  if (len < 3)
  {
    Serial.println(F("CMD SHORT"));
    return;
  }

  if (s[0] != 'a')
  {
    Serial.println(F("CMD NO a"));
    return;
  }

  if (s[len - 1] != 'n')
  {
    Serial.println(F("CMD NO n"));
    return;
  }

  for (byte i = 1; i < len - 1; i++)
  {
    char c = s[i];

    if (c == 'h') continue;

    if (c == 'T' || c == 'R' || c == 'L')
    {
      if (routeLen < MAX_CMD)
      {
        routeCmd[routeLen++] = c;
      }
    }
  }

  if (routeLen > 0)
  {
    hasRoute = true;
    waitingReturn = false;

    buildReturnRoute();

    printRoute(F("ROUTE"), routeCmd, routeLen);
    printRoute(F("RETURN"), returnCmd, returnLen);
  }
  else
  {
    Serial.println(F("CMD NO T/R/L"));
  }
}

bool executeOneCommand(char cmd)
{
  Serial.print(F("EXEC:"));
  Serial.println(cmd);

  forceLineOn();

  bool ok = goStraightThrough3Markers();

  if (!ok)
  {
    return false;
  }

  if (cmd == 'R')
  {
    turnRight90();
  }
  else if (cmd == 'L')
  {
    turnLeft90();
  }
  else if (cmd == 'T')
  {
    Serial.println(F("STRAIGHT ONLY"));
  }

  return true;
}

void executeRoute(char *cmdList, byte len)
{
  robotBusy = true;
  manualLineMode = false;
  waitingReturn = false;

  sendStatus(F("Unavailable"));

  bool ok = true;

  for (byte i = 0; i < len; i++)
  {
    ok = executeOneCommand(cmdList[i]);

    if (!ok)
    {
      break;
    }
  }

  if (ok)
  {
    ok = goStraightUntilOneMarker();
  }

  motorStop();

  if (ok)
  {
    sendStatus(F("ok"));
    sendStatus(F("Available"));

    robotBusy = false;
    waitingReturn = true;
    manualLineMode = false;

    Serial.println(F("WAIT RETURN BUTTON"));
    Serial.print(F("returnLen="));
    Serial.println(returnLen);
  }
  else
  {
    sendStatus(F("ERROR"));
    sendStatus(F("Available"));

    robotBusy = false;
    waitingReturn = false;
    manualLineMode = false;
  }
}

// =====================================================
// SERIAL COMMAND
// =====================================================
void readCommandUART()
{
  while (Serial.available())
  {
    char c = Serial.read();

    if (c == '\n' || c == '\r')
    {
      continue;
    }

    if (c == 's' || c == 'S')
    {
      stopAll();
      return;
    }

    if (c == 'f' || c == 'F')
    {
      Serial.println(F("MANUAL LINE"));
      forceLineOn();

      manualLineMode = true;
      robotBusy = false;
      receivingCommand = false;

      inputIndex = 0;
      inputBuffer[0] = '\0';

      return;
    }

    if (c == 'a')
    {
      receivingCommand = true;
      inputIndex = 0;

      inputBuffer[inputIndex++] = c;
      inputBuffer[inputIndex] = '\0';

      Serial.println(F("CMD START"));
      continue;
    }

    if (receivingCommand)
    {
      if (inputIndex < INPUT_BUF_SIZE - 1)
      {
        inputBuffer[inputIndex++] = c;
        inputBuffer[inputIndex] = '\0';
      }

      if (c == 'n')
      {
        receivingCommand = false;

        Serial.print(F("CMD:"));
        Serial.println(inputBuffer);

        if (robotBusy || waitingReturn)
        {
          sendStatus(F("Unavailable"));

          inputIndex = 0;
          inputBuffer[0] = '\0';
        }
        else
        {
          parseCommandString(inputBuffer);

          if (hasRoute)
          {
            executeRoute(routeCmd, routeLen);
          }
          else
          {
            Serial.println(F("CMD ERROR"));
            resetAllCommandData();
          }
        }
      }
    }
  }
}

// =====================================================
// BUTTON RETURN
// =====================================================
void checkButton()
{
  static bool lastBtn = HIGH;

  bool btn = digitalRead(BUTTON_PIN);

  if (lastBtn == HIGH && btn == LOW)
  {
    delay(50);

    if (digitalRead(BUTTON_PIN) == LOW)
    {
      Serial.println(F("BTN PRESSED"));

      Serial.print(F("busy="));
      Serial.print(robotBusy ? F("Y") : F("N"));
      Serial.print(F(" wait="));
      Serial.print(waitingReturn ? F("Y") : F("N"));
      Serial.print(F(" hasRoute="));
      Serial.print(hasRoute ? F("Y") : F("N"));
      Serial.print(F(" returnLen="));
      Serial.println(returnLen);

      if (!robotBusy && waitingReturn && returnLen > 0)
      {
        sendStatus(F("received"));

        char tempReturnCmd[MAX_CMD];
        byte tempReturnLen = returnLen;

        for (byte i = 0; i < MAX_CMD; i++)
        {
          tempReturnCmd[i] = 0;
        }

        for (byte i = 0; i < tempReturnLen; i++)
        {
          tempReturnCmd[i] = returnCmd[i];
        }

        robotBusy = true;
        waitingReturn = false;
        manualLineMode = false;

        sendStatus(F("Unavailable"));

        motorStop();
        delay(300);

        resetRuntimeBeforeReturn();
        delay(200);

        forceLineOff();
        delay(300);

        turnBack180LeftCalculatedNoLine();

        forceLineOn();
        delay(500);

        Serial.println(F("START RETURN ROUTE"));

        bool ok = true;

        for (byte i = 0; i < tempReturnLen; i++)
        {
          ok = executeOneCommand(tempReturnCmd[i]);

          if (!ok)
          {
            break;
          }
        }

        motorStop();

        if (ok)
        {
          sendStatus(F("ok"));
        }
        else
        {
          sendStatus(F("ERROR"));
        }

        sendStatus(F("Available"));

        robotBusy = false;

        resetAllCommandData();
      }
      else
      {
        Serial.println(F("BTN IGNORED"));
      }
    }
  }

  lastBtn = btn;
}

// =====================================================
// SETUP
// =====================================================
void setup()
{
  Serial.begin(9600);

  delay(1000);

  Serial.println();
  Serial.println(F("UNO BOOT"));

  MpuSerial.begin(9600);
  HoverSerial.begin(HOVER_BAUD);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(OUT2_PIN, INPUT);
  pinMode(OUT3_PIN, INPUT);
  pinMode(OUT4_PIN, INPUT);

  motorStop();

  resetAllCommandData();

  sendStatus(F("UNO READY"));
  sendStatus(F("Available"));

  Serial.println(F("f=line test"));
  Serial.println(F("s=stop"));
  Serial.println(F("cmd example: aThLn"));
  Serial.println(F("D2=return button"));

  Serial.print(F("TURN SPEED:"));
  Serial.println(TURN_WHEEL_SPEED);

  Serial.print(F("AUTO RPM:"));
  Serial.println(getWheelRPMFromSpeed(TURN_WHEEL_SPEED));

  Serial.print(F("TURN90 ms:"));
  Serial.println(getTurnTimeMs(90.0));

  Serial.print(F("TURN180 ms:"));
  Serial.println(getTurnTimeMs(180.0));
}

// =====================================================
// LOOP
// =====================================================
void loop()
{
  readCommandUART();

  if (!robotBusy)
  {
    checkButton();
  }

  if (manualLineMode && !robotBusy)
  {
    forceLineOn();
    followLineStep();
    printDebug();
    delay(5);
    return;
  }

  realtimeUpdate();
}