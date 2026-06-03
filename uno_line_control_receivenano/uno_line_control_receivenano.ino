#include <Arduino.h>
#include <SoftwareSerial.h>

// =====================================================
// UART NHAN MPU TU NANO
// UNO D6 RX <- Nano D6 TX
// UNO D7 TX -> Nano D5 RX
// =====================================================
#define MPU_RX 6
#define MPU_TX 7
SoftwareSerial MpuSerial(MPU_RX, MPU_TX);

// =====================================================
// UART GUI XUONG STM / HOVERBOARD
// UNO D4 RX <- TX STM / Hoverboard
// UNO D5 TX -> RX STM / Hoverboard
// =====================================================
#define HOVER_RX 4
#define HOVER_TX 5
SoftwareSerial HoverSerial(HOVER_RX, HOVER_TX);

// =====================================================
// THANH DO LINE 5 MAT - CHI DUNG OUT2 OUT3 OUT4
// OUT2 -> A1 = L
// OUT3 -> A2 = M
// OUT4 -> A3 = R
//
// Khong co line -> OUT = 1
// Co line       -> OUT = 0
// =====================================================
#define OUT2_PIN A1
#define OUT3_PIN A2
#define OUT4_PIN A3

#define LINE_THRESHOLD 500

// =====================================================
// CONG TAC
// =====================================================
#define BUTTON_PIN 2

// =====================================================
// HOVERBOARD / STM CONFIG
// =====================================================
#define HOVER_BAUD 115200
#define START_FRAME 0xABCD

typedef struct {
  uint16_t start;
  int16_t steer;
  int16_t speed;
  uint16_t checksum;
} SerialCommand;

SerialCommand Command;

// =====================================================
// TOC DO
// =====================================================
int BASE_SPEED = 60;
int LINE_TURN_SPEED = 60;

// Quay tại chỗ: 2 bánh quay ngược chiều nhau
int TURN_MAX_WHEEL_SPEED = 110;
int TURN_MIN_WHEEL_SPEED = 70;

float TURN_TOLERANCE = 1.0;

// Xe đang quay thiếu 90 độ thì bù thêm
float TURN_EXTRA_DEGREE = 6.0;

// Nếu quay trái/phải bị ngược thì đổi thành -1
int TURN_DIRECTION_SIGN = 1;

// Nếu dò line bị ngược hướng thì đổi thành -1
int LINE_DIRECTION_SIGN = 1;

// PID quay góc
float TURN_KP = 1.55;
float TURN_KD = 0.35;

// =====================================================
// MPU DATA NHAN TU NANO
// Nano gui dang: x0y0z90h
// =====================================================
String mpuBuffer = "";

int mpuX = 0;
int mpuY = 0;
int mpuZ = 0;

float currentYaw = 0;
unsigned long lastMPUTime = 0;

// =====================================================
// LINE DATA
// =====================================================
int raw2 = 0;
int raw3 = 0;
int raw4 = 0;

int out2 = 1;
int out3 = 1;
int out4 = 1;

int lineState = 0;

// =====================================================
// DEBUG REALTIME
// =====================================================
unsigned long lastDebug = 0;
const unsigned long debugInterval = 100;

// =====================================================
// LENH DI CHUYEN
// =====================================================
#define MAX_CMD 30

char routeCmd[MAX_CMD];
int routeLen = 0;

char returnCmd[MAX_CMD];
int returnLen = 0;

bool hasRoute = false;
bool robotBusy = false;

String inputBuffer = "";
bool receivingCommand = false;

// =====================================================
// GUI LENH DONG CO
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
// DIEU KHIEN 2 BANH TRAI / PHAI
//
// left  = speed + steer
// right = speed - steer
//
// Muon leftSpeed, rightSpeed:
// speed = (left + right) / 2
// steer = (left - right) / 2
// =====================================================
void driveWheels(int leftSpeed, int rightSpeed)
{
  int speed = (leftSpeed + rightSpeed) / 2;
  int steer = (leftSpeed - rightSpeed) / 2;

  sendHover(steer, speed);
}

// =====================================================
// QUAY TAI CHO BANG 2 BANH NGUOC CHIEU
//
// Rẽ phải:
//   bánh trái tiến
//   bánh phải lùi
//
// Rẽ trái:
//   bánh trái lùi
//   bánh phải tiến
//
// QUAN TRONG:
// speed = 0 để hai bánh quay ngược chiều nhau thật sự.
// =====================================================
void rotateInPlace(int dir, int wheelSpeed)
{
  dir = dir * TURN_DIRECTION_SIGN;

  if (dir > 0)
  {
    // Quay phải: trái tiến, phải lùi
    driveWheels(wheelSpeed, -wheelSpeed);
  }
  else
  {
    // Quay trái: trái lùi, phải tiến
    driveWheels(-wheelSpeed, wheelSpeed);
  }
}

// =====================================================
// GUI TRANG THAI
// =====================================================
void sendStatus(const char *msg)
{
  Serial.println(msg);
}

// =====================================================
// RESET SAU KHI CHAY XONG LENH
// =====================================================
void resetAllCommandData()
{
  routeLen = 0;
  returnLen = 0;
  hasRoute = false;
  receivingCommand = false;
  inputBuffer = "";

  for (int i = 0; i < MAX_CMD; i++)
  {
    routeCmd[i] = 0;
    returnCmd[i] = 0;
  }

  motorStop();

  Serial.println("RESET DONE - READY FOR NEW COMMAND");
}

// =====================================================
// DOC MPU TU NANO
// =====================================================
void readMPU()
{
  MpuSerial.listen();

  while (MpuSerial.available())
  {
    char c = MpuSerial.read();

    if (c == 'h')
    {
      int ix = mpuBuffer.indexOf('x');
      int iy = mpuBuffer.indexOf('y');
      int iz = mpuBuffer.indexOf('z');

      if (ix != -1 && iy != -1 && iz != -1)
      {
        String sx = mpuBuffer.substring(ix + 1, iy);
        String sy = mpuBuffer.substring(iy + 1, iz);
        String sz = mpuBuffer.substring(iz + 1);

        mpuX = sx.toInt();
        mpuY = sy.toInt();
        mpuZ = sz.toInt();

        currentYaw = mpuZ;
        lastMPUTime = millis();
      }

      mpuBuffer = "";
    }
    else if (c != '\n' && c != '\r')
    {
      mpuBuffer += c;

      if (mpuBuffer.length() > 40)
      {
        mpuBuffer = "";
      }
    }
  }
}

// =====================================================
// CHUAN HOA GOC -180 DEN 180
// =====================================================
float normalizeError(float e)
{
  while (e > 180) e -= 360;
  while (e < -180) e += 360;
  return e;
}

// =====================================================
// DOC LINE
// raw thap -> co line -> out = 0
// raw cao  -> khong line -> out = 1
// =====================================================
void updateLineSensor()
{
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

int readLineState()
{
  updateLineSensor();
  return lineState;
}

// Moc / nga tu: OUT2 OUT3 OUT4 deu co line
bool allSensorsZeroMarker()
{
  updateLineSensor();

  return out2 == 0 &&
         out3 == 0 &&
         out4 == 0;
}

// =====================================================
// HIEN THI REALTIME
// =====================================================
void printDebug()
{
  if (millis() - lastDebug >= debugInterval)
  {
    lastDebug = millis();

    updateLineSensor();

    Serial.print("MPU ");
    Serial.print("X:");
    Serial.print(mpuX);
    Serial.print(" Y:");
    Serial.print(mpuY);
    Serial.print(" Z:");
    Serial.print(mpuZ);

    Serial.print(" || RAW ");
    Serial.print("OUT2/A1:");
    Serial.print(raw2);
    Serial.print(" OUT3/A2:");
    Serial.print(raw3);
    Serial.print(" OUT4/A3:");
    Serial.print(raw4);

    Serial.print(" || OUT ");
    Serial.print("OUT2:");
    Serial.print(out2);
    Serial.print(" OUT3:");
    Serial.print(out3);
    Serial.print(" OUT4:");
    Serial.print(out4);

    Serial.print(" || LMR:");
    Serial.print(out2);
    Serial.print(out3);
    Serial.print(out4);

    Serial.print(" || STATE:");
    Serial.print(lineState, BIN);

    Serial.print(" || ROBOT:");
    Serial.println(robotBusy ? "Unavailable" : "Available");
  }
}

// =====================================================
// CAP NHAT REALTIME
// =====================================================
void realtimeUpdate()
{
  readMPU();
  updateLineSensor();
  printDebug();
}

// =====================================================
// DOI MPU CAP NHAT TRUOC KHI QUAY
// =====================================================
void waitMPUStable()
{
  unsigned long t = millis();

  while (millis() - t < 300)
  {
    realtimeUpdate();
    delay(5);
  }
}

// =====================================================
// DO LINE 1 BUOC
//
// Di thang: 2 banh cung toc do 60
// Lech line: 1 banh dung, 1 banh quay toc do co dinh 60
// =====================================================
void followLineStep()
{
  int state = readLineState();

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
      // Line ben trai -> re trai
      leftSpeed = 0;
      rightSpeed = LINE_TURN_SPEED;
      break;

    case 0b011:
    case 0b001:
      // Line ben phai -> re phai
      leftSpeed = LINE_TURN_SPEED;
      rightSpeed = 0;
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
// DI THANG QUA 3 MOC 000
// =====================================================
void goStraightThrough3Markers()
{
  int markerCount = 0;
  bool lastMarker = false;

  unsigned long startEscape = millis();

  while (allSensorsZeroMarker() && millis() - startEscape < 800)
  {
    realtimeUpdate();
    followLineStep();
    delay(5);
  }

  while (markerCount < 3)
  {
    realtimeUpdate();

    bool markerNow = allSensorsZeroMarker();

    if (markerNow && !lastMarker)
    {
      markerCount++;

      Serial.print("MARKER COUNT:");
      Serial.println(markerCount);

      motorStop();
      delay(60);
    }

    lastMarker = markerNow;

    followLineStep();

    delay(5);
  }

  motorStop();
  delay(120);
}

// =====================================================
// PID QUAY GOC TAI CHO
//
// dir = 1  : quay phai
// dir = -1 : quay trai
// degree = 90 hoac 180
//
// Khi quay:
// - Rẽ trái: bánh trái lùi, bánh phải tiến
// - Rẽ phải: bánh trái tiến, bánh phải lùi
// =====================================================
void turnByAngle(int dir, float degree)
{
  motorStop();
  delay(150);

  waitMPUStable();

  float startYaw = currentYaw;

  float targetRelative = dir * (degree + TURN_EXTRA_DEGREE);

  float lastErr = targetRelative;
  unsigned long lastTime = millis();
  unsigned long turnStartTime = millis();

  Serial.print("TURN START | Start Z:");
  Serial.print(startYaw);
  Serial.print(" Target Relative:");
  Serial.println(targetRelative);

  while (true)
  {
    realtimeUpdate();

    float turned = normalizeError(currentYaw - startYaw);
    float err = normalizeError(targetRelative - turned);

    if (abs(err) <= TURN_TOLERANCE)
    {
      break;
    }

    unsigned long now = millis();
    float dt = (now - lastTime) / 1000.0;
    if (dt <= 0) dt = 0.01;

    lastTime = now;

    float dErr = (err - lastErr) / dt;
    lastErr = err;

    float pidOutput = TURN_KP * err + TURN_KD * dErr;

    int wheelSpeed = abs(pidOutput);

    if (wheelSpeed > TURN_MAX_WHEEL_SPEED) wheelSpeed = TURN_MAX_WHEEL_SPEED;
    if (wheelSpeed < TURN_MIN_WHEEL_SPEED) wheelSpeed = TURN_MIN_WHEEL_SPEED;

    int turnDir = err > 0 ? 1 : -1;

    rotateInPlace(turnDir, wheelSpeed);

    Serial.print("TURN PID | Start:");
    Serial.print(startYaw);
    Serial.print(" Now:");
    Serial.print(currentYaw);
    Serial.print(" Turned:");
    Serial.print(turned);
    Serial.print(" Err:");
    Serial.print(err);
    Serial.print(" WheelSpeed:");
    Serial.println(wheelSpeed);

    if (millis() - turnStartTime > 8000)
    {
      Serial.println("TURN TIMEOUT - CHECK MPU / MOTOR DIRECTION / STM MIXER");
      break;
    }

    delay(10);
  }

  motorStop();
  delay(120);

  realtimeUpdate();

  float finalTurned = normalizeError(currentYaw - startYaw);
  float finalErr = normalizeError(targetRelative - finalTurned);

  Serial.print("TURN CHECK | Turned:");
  Serial.print(finalTurned);
  Serial.print(" Final Err:");
  Serial.println(finalErr);

  unsigned long correctStart = millis();

  while (abs(finalErr) > TURN_TOLERANCE && millis() - correctStart < 1200)
  {
    realtimeUpdate();

    finalTurned = normalizeError(currentYaw - startYaw);
    finalErr = normalizeError(targetRelative - finalTurned);

    int correctDir = finalErr > 0 ? 1 : -1;

    rotateInPlace(correctDir, TURN_MIN_WHEEL_SPEED);

    Serial.print("TURN CORRECT | Turned:");
    Serial.print(finalTurned);
    Serial.print(" Err:");
    Serial.println(finalErr);

    delay(10);
  }

  motorStop();

  Serial.print("TURN DONE | Current Z:");
  Serial.println(currentYaw);

  delay(200);
}

void turnRight90()
{
  turnByAngle(1, 90);
}

void turnLeft90()
{
  turnByAngle(-1, 90);
}

void turnBack180()
{
  turnByAngle(1, 180);
}

// =====================================================
// THUC THI 1 LENH
// =====================================================
void executeOneCommand(char cmd)
{
  Serial.print("EXECUTE CMD:");
  Serial.println(cmd);

  goStraightThrough3Markers();

  if (cmd == 'R')
  {
    Serial.println("START TURN RIGHT 90");
    turnRight90();
  }
  else if (cmd == 'L')
  {
    Serial.println("START TURN LEFT 90");
    turnLeft90();
  }
  else if (cmd == 'T')
  {
    Serial.println("GO STRAIGHT ONLY");
  }
}

// =====================================================
// TAO LENH QUAY VE
// =====================================================
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
      returnCmd[returnLen] = c;
      returnLen++;
    }
  }
}

// =====================================================
// PHAN TICH LENH DANG aThLn
// =====================================================
void parseCommandString(String s)
{
  routeLen = 0;
  hasRoute = false;

  if (s.length() < 3) return;
  if (s[0] != 'a') return;
  if (s[s.length() - 1] != 'n') return;

  for (int i = 1; i < s.length() - 1; i++)
  {
    char c = s[i];

    if (c == 'h') continue;

    if (c == 'T' || c == 'R' || c == 'L')
    {
      if (routeLen < MAX_CMD)
      {
        routeCmd[routeLen] = c;
        routeLen++;
      }
    }
  }

  if (routeLen > 0)
  {
    hasRoute = true;
    buildReturnRoute();

    Serial.print("ROUTE:");

    for (int i = 0; i < routeLen; i++)
    {
      Serial.print(routeCmd[i]);
      if (i < routeLen - 1) Serial.print(",");
    }

    Serial.println();

    Serial.print("RETURN ROUTE:");

    for (int i = 0; i < returnLen; i++)
    {
      Serial.print(returnCmd[i]);
      if (i < returnLen - 1) Serial.print(",");
    }

    Serial.println();
  }
}

// =====================================================
// THUC THI TUYEN DI
// =====================================================
void executeRoute(char *cmdList, int len)
{
  robotBusy = true;
  sendStatus("Unavailable");

  for (int i = 0; i < len; i++)
  {
    executeOneCommand(cmdList[i]);
  }

  motorStop();

  sendStatus("ok");
  sendStatus("Available");

  robotBusy = false;

  resetAllCommandData();
}

// =====================================================
// DOC LENH TU SERIAL
// =====================================================
void readCommandUART()
{
  while (Serial.available())
  {
    char c = Serial.read();

    if (c == 'a')
    {
      receivingCommand = true;
      inputBuffer = "";
      inputBuffer += c;
    }
    else if (receivingCommand)
    {
      inputBuffer += c;

      if (c == 'n')
      {
        receivingCommand = false;

        Serial.print("RECEIVED COMMAND STRING:");
        Serial.println(inputBuffer);

        if (robotBusy)
        {
          sendStatus("Unavailable");
          inputBuffer = "";
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
            Serial.println("COMMAND ERROR");
            resetAllCommandData();
          }
        }
      }
    }
  }
}

// =====================================================
// KIEM TRA CONG TAC
// =====================================================
void checkButton()
{
  static bool lastBtn = HIGH;

  bool btn = digitalRead(BUTTON_PIN);

  if (lastBtn == HIGH && btn == LOW)
  {
    delay(30);

    if (digitalRead(BUTTON_PIN) == LOW)
    {
      if (!robotBusy && hasRoute)
      {
        sendStatus("Received");

        robotBusy = true;
        sendStatus("Unavailable");

        turnBack180();

        for (int i = 0; i < returnLen; i++)
        {
          executeOneCommand(returnCmd[i]);
        }

        motorStop();

        sendStatus("ok");
        sendStatus("Available");

        robotBusy = false;

        resetAllCommandData();
      }
      else
      {
        Serial.println("NO OLD ROUTE");
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

  MpuSerial.begin(9600);
  HoverSerial.begin(HOVER_BAUD);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(OUT2_PIN, INPUT);
  pinMode(OUT3_PIN, INPUT);
  pinMode(OUT4_PIN, INPUT);

  delay(1000);

  MpuSerial.listen();

  resetAllCommandData();

  sendStatus("UNO READY");
  sendStatus("Available");
}


// =====================================================
// LOOP
// =====================================================
void loop()
{
  realtimeUpdate();

  if (!robotBusy)
  {
    readCommandUART();
    checkButton();
  }
}