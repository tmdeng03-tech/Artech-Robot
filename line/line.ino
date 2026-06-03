#include <Arduino.h>
#include <SoftwareSerial.h>

// =====================================================
// UART HOVERBOARD
// =====================================================
#define RXD2 4
#define TXD2 5

SoftwareSerial HoverSerial(RXD2, TXD2);

// =====================================================
// UART MPU NANO
// =====================================================
SoftwareSerial NanoSerial(6, 7);

// =====================================================
// UART COMMAND
// =====================================================
#define COMMAND_BAUD 9600

// =====================================================
#define HOVER_BAUD 115200
#define START_FRAME 0xABCD

// =====================================================
// LINE SENSOR
// =====================================================
#define OUT2 A1
#define OUT3 A2
#define OUT4 A3

// =====================================================
// SWITCH INPUT
// =====================================================
#define SWITCH_PIN 2

// =====================================================
// MOTOR
// =====================================================
int16_t speedValue = 0;
int16_t steerValue = 0;

// =====================================================
// MPU
// =====================================================
float currentX = 0;
float currentY = 0;
float currentZ = 0;

// =====================================================
// TURN CONTROL
// =====================================================
bool isTurning = false;
char currentTurnDirection = 0; // 'L', 'R', or '2' for 180°
float targetAngle = 0;

// =====================================================
// PID
// =====================================================
float Kp = 3.0;
float error = 0;
float pidValue = 0;

// =====================================================
// TIMING
// =====================================================
unsigned long lastSend = 0;
const int sendInterval = 20;

// =====================================================
// COMMAND STRUCT for HOVERBOARD
// =====================================================
typedef struct {
  uint16_t start;
  int16_t steer;
  int16_t speed;
  uint16_t checksum;
} SerialCommand;

SerialCommand Command;

// =====================================================
// COMMAND PROCESSING
// =====================================================
String commandSequence = "";      // Parsed commands: "T,R,L..."
int commandIndex = 0;              // Current command index
char currentCommand = 0;           // Current executing command
int intersectionCount = 0;         // Count intersections (checkpoints)
bool lastIntersectionState = false; // Track intersection edge
const int INTERSECTIONS_PER_COMMAND = 3;

// =====================================================
// STATE MACHINE
// =====================================================
enum SystemState {
  IDLE,
  EXECUTING,
  RETURNING,
  STOPPED
};

SystemState systemState = IDLE;
bool returnPath = false;           // Flag for return journey
String returnCommands = "";        // Commands for return journey

// =====================================================
// SEND HOVERBOARD
// =====================================================
void sendHover(int16_t steer, int16_t speed)
{
  Command.start = START_FRAME;
  Command.steer = steer;
  Command.speed = speed;
  Command.checksum =
      Command.start ^
      Command.steer ^
      Command.speed;

  HoverSerial.write(
      (uint8_t *)&Command,
      sizeof(Command));
}

// =====================================================
// SEND STATUS TO UART
// =====================================================
void sendStatus(String status)
{
  Serial.println(status);
}

// =====================================================
// PARSE UART COMMAND
// Format: aThRn -> parse as "T,R"
// =====================================================
void parseCommand(String rawCommand)
{
  commandSequence = "";
  commandIndex = 0;
  intersectionCount = 0;
  lastIntersectionState = false;
  
  // Remove start 'a' and end 'n'
  if(rawCommand.startsWith("a") && 
     rawCommand.endsWith("n"))
  {
    rawCommand = rawCommand.substring(1, rawCommand.length() - 1);
  }
  
  // Split by 'h' separator
  int lastIndex = 0;
  for(int i = 0; i < rawCommand.length(); i++)
  {
    if(rawCommand[i] == 'h' || i == rawCommand.length() - 1)
    {
      int endIndex = (i == rawCommand.length() - 1) ? i + 1 : i;
      String cmd = rawCommand.substring(lastIndex, endIndex);
      
      if(cmd.length() > 0 && 
         (cmd[0] == 'T' || cmd[0] == 'R' || cmd[0] == 'L'))
      {
        if(commandSequence.length() > 0)
          commandSequence += ",";
        commandSequence += cmd[0];
      }
      
      lastIndex = i + 1;
    }
  }
  
  Serial.print("Parsed Commands: ");
  Serial.println(commandSequence);
  
  systemState = EXECUTING;
  sendStatus("Unavailable");
  
  // Start executing first command
  if(commandSequence.length() > 0)
  {
    currentCommand = commandSequence[0];
    intersectionCount = 0;
  }
}

// =====================================================
// GENERATE RETURN COMMANDS
// Reverse the command sequence and invert turns
// Example: T,R -> L,T
// =====================================================
String generateReturnCommands(String forwardCommands)
{
  String reversed = "";
  int commaCount = 0;
  
  // Count commas to get array size
  for(int i = 0; i < forwardCommands.length(); i++)
  {
    if(forwardCommands[i] == ',') commaCount++;
  }
  
  int arraySize = commaCount + 1;
  
  // Extract commands into array
  char commands[arraySize];
  int idx = 0;
  int lastPos = 0;
  
  for(int i = 0; i <= forwardCommands.length(); i++)
  {
    if(forwardCommands[i] == ',' || i == forwardCommands.length())
    {
      commands[idx++] = forwardCommands[lastPos];
      lastPos = i + 1;
    }
  }
  
  // Build return sequence in reverse with inverted turns
  for(int i = arraySize - 1; i >= 0; i--)
  {
    if(i < arraySize - 1)
      reversed += ",";
    
    char cmd = commands[i];
    
    // Invert turns: R->L, L->R, T->T
    if(cmd == 'R')
      reversed += 'L';
    else if(cmd == 'L')
      reversed += 'R';
    else
      reversed += 'T';
  }
  
  return reversed;
}

// =====================================================
// READ MPU
// DATA: x10y20z90h
// =====================================================
void readAngle()
{
  if(NanoSerial.available())
  {
    String s = NanoSerial.readStringUntil('h');

    int xIndex = s.indexOf('x');
    int yIndex = s.indexOf('y');
    int zIndex = s.indexOf('z');

    // =============================================
    // X
    // =============================================
    if(xIndex >= 0 && yIndex > xIndex)
    {
      String xStr = s.substring(xIndex + 1, yIndex);
      currentX = xStr.toFloat();
    }

    // =============================================
    // Y
    // =============================================
    if(yIndex >= 0 && zIndex > yIndex)
    {
      String yStr = s.substring(yIndex + 1, zIndex);
      currentY = yStr.toFloat();
    }

    // =============================================
    // Z (YAW - rotation angle)
    // =============================================
    if(zIndex >= 0)
    {
      String zStr = s.substring(zIndex + 1);
      currentZ = zStr.toFloat();
    }
  }
}

// =====================================================
// RESET Z ANGLE
// =====================================================
void resetZ()
{
  currentZ = 0;
  NanoSerial.println("RESET");
}

// =====================================================
// START TURN
// =====================================================
void startTurn(char direction)
{
  isTurning = true;
  currentTurnDirection = direction;
  resetZ();
  
  if(direction == 'L')
  {
    targetAngle = 90;
    Serial.println("START LEFT 90");
  }
  else if(direction == 'R')
  {
    targetAngle = -90;
    Serial.println("START RIGHT 90");
  }
  else if(direction == '2')
  {
    targetAngle = -180; // or 180, we'll use abs value
    Serial.println("START 180 DEGREE TURN");
  }
}

// =====================================================
// PROCESS TURN WITH PID
// =====================================================
void processTurn()
{
  speedValue = 0;
  
  float absZ = abs(currentZ);
  float targetAbsAngle = abs(targetAngle);
  
  // =============================================
  // 0 -> 70 DEGREES: FAST TURN
  // =============================================
  if(absZ < 70)
  {
    if(currentTurnDirection == 'L')
      steerValue = -220;
    else if(currentTurnDirection == 'R')
      steerValue = 220;
    else if(currentTurnDirection == '2')
      steerValue = -220; // 180° turn left direction
  }
  
  // =============================================
  // 70 -> TARGET DEGREES: PID SMOOTH TURN
  // =============================================
  else
  {
    error = targetAbsAngle - absZ;
    pidValue = Kp * error;
    
    // =========================================
    // LIMIT PID OUTPUT
    // =========================================
    if(pidValue > 120)
      pidValue = 120;
    if(pidValue < 40)
      pidValue = 40;
    
    if(currentTurnDirection == 'L')
      steerValue = -pidValue;
    else if(currentTurnDirection == 'R')
      steerValue = pidValue;
    else if(currentTurnDirection == '2')
      steerValue = -pidValue; // 180° turn
  }
  
  // =============================================
  // TURN COMPLETE
  // =============================================
  if(absZ >= targetAbsAngle - 5) // Allow 5° tolerance
  {
    speedValue = 0;
    steerValue = 0;
    sendHover(0, 0);
    delay(300);
    
    isTurning = false;
    lastIntersectionState = false; // Reset intersection state
    
    // =========================================
    // AFTER 180° TURN, START RETURN COMMANDS
    // =========================================
    if(currentTurnDirection == '2')
    {
      systemState = EXECUTING;
      sendStatus("Unavailable");
      // Command sequence already set to return commands
    }
    else
    {
      // After 90° turn in normal execution
      commandIndex++;
      intersectionCount = 0;
      
      if(commandIndex < commandSequence.length())
      {
        currentCommand = commandSequence[commandIndex];
      }
      else
      {
        // All commands executed
        if(!returnPath)
        {
          sendStatus("ok");
          delay(500);
          systemState = IDLE;
          sendStatus("Available");
        }
        else
        {
          sendStatus("ok");
          delay(500);
          systemState = STOPPED;
          sendStatus("Available");
        }
      }
    }
  }
}

// =====================================================
// LINE FOLLOWING
// =====================================================
void lineFollow()
{
  int s2 = digitalRead(OUT2);
  int s3 = digitalRead(OUT3);
  int s4 = digitalRead(OUT4);
  
  // =============================================
  // DETECT INTERSECTION (all sensors = 0)
  // =============================================
  bool atIntersection = (s2 == 0 && s3 == 0 && s4 == 0);
  
  // =========================================
  // EDGE DETECTION: entering intersection
  // =========================================
  if(atIntersection && !lastIntersectionState)
  {
    intersectionCount++;
    lastIntersectionState = true;
    
    // =========================================
    // CHECKPOINT REACHED - PROCESS COMMAND
    // =========================================
    if(intersectionCount >= INTERSECTIONS_PER_COMMAND)
    {
      // Execute turn or continue straight
      if(currentCommand == 'T')
      {
        // "T" command: Continue straight to next intersection
        intersectionCount = 0;
        commandIndex++;
        
        if(commandIndex < commandSequence.length())
        {
          currentCommand = commandSequence[commandIndex];
        }
        else
        {
          // All commands executed - mission complete
          speedValue = 0;
          steerValue = 0;
          sendHover(0, 0);
          delay(300);
          
          if(!returnPath)
          {
            sendStatus("ok");
            delay(500);
            systemState = IDLE;
            sendStatus("Available");
          }
          else
          {
            sendStatus("ok");
            delay(500);
            systemState = STOPPED;
            sendStatus("Available");
          }
        }
      }
      else if(currentCommand == 'L' || currentCommand == 'R')
      {
        // "L" or "R" command: Perform turn at this intersection
        startTurn(currentCommand);
      }
      
      return;
    }
  }
  
  // =========================================
  // EXITING INTERSECTION
  // =========================================
  if(!atIntersection && lastIntersectionState)
  {
    lastIntersectionState = false;
  }
  
  // =========================================
  // UPDATE LAST STATE
  // =========================================
  if(atIntersection)
  {
    lastIntersectionState = true;
  }
  
  // =============================================
  // LINE FOLLOWING LOGIC (not at intersection)
  // =============================================
  if(!isTurning)
  {
    speedValue = 60;
    
    // =========================================
    // CENTER LINE
    // =========================================
    if(s3 == 0)
    {
      steerValue = 0;
    }
    
    // =========================================
    // LEFT DEVIATION
    // =========================================
    else if(s2 == 0)
    {
      steerValue = -60;
    }
    
    // =========================================
    // RIGHT DEVIATION
    // =========================================
    else if(s4 == 0)
    {
      steerValue = 60;
    }
    
    // =========================================
    // LINE LOST
    // =========================================
    else
    {
      speedValue = 0;
      steerValue = 0;
    }
  }
}

// =====================================================
// HANDLE SWITCH PRESS
// =====================================================
void handleSwitch()
{
  if(systemState == IDLE && returnPath == false)
  {
    Serial.println("SWITCH PRESSED - STARTING RETURN JOURNEY");
    
    sendStatus("Received");
    delay(500);
    
    // Generate return commands
    returnCommands = generateReturnCommands(commandSequence);
    Serial.print("Return Commands: ");
    Serial.println(returnCommands);
    
    // Store original commands for reference
    String originalCommands = commandSequence;
    
    // Set new sequence to return commands
    commandSequence = returnCommands;
    commandIndex = 0;
    intersectionCount = 0;
    lastIntersectionState = false;
    returnPath = true;
    
    // Start 180° turn
    startTurn('2');
    
    systemState = EXECUTING;
    sendStatus("Unavailable");
  }
}

// =====================================================
// PARSE INCOMING UART DATA
// =====================================================
void parseIncomingData()
{
  if(Serial.available())
  {
    String data = Serial.readStringUntil('\n');
    data.trim();
    
    // Check for command format: aXhXhXn
    if(data.startsWith("a") && data.endsWith("n"))
    {
      if(systemState == IDLE)
      {
        parseCommand(data);
      }
    }
  }
}

// =====================================================
// SETUP
// =====================================================
void setup()
{
  Serial.begin(COMMAND_BAUD);
  HoverSerial.begin(HOVER_BAUD);
  NanoSerial.begin(9600);
  
  pinMode(OUT2, INPUT);
  pinMode(OUT3, INPUT);
  pinMode(OUT4, INPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  
  Serial.println("SYSTEM INITIALIZED");
  Serial.println("Available");
}

// =====================================================
// LOOP
// =====================================================
void loop()
{
  // =============================================
  // READ MPU ANGLE
  // =============================================
  readAngle();
  
  // =============================================
  // PARSE INCOMING COMMANDS
  // =============================================
  parseIncomingData();
  
  // =============================================
  // CHECK SWITCH INPUT
  // =============================================
  if(digitalRead(SWITCH_PIN) == LOW)
  {
    delay(50); // Debounce
    if(digitalRead(SWITCH_PIN) == LOW)
    {
      handleSwitch();
      delay(300); // Prevent multiple triggers
    }
  }
  
  // =============================================
  // STATE MACHINE
  // =============================================
  if(systemState == EXECUTING || systemState == RETURNING)
  {
    if(isTurning)
    {
      // Execute turn with PID
      processTurn();
    }
    else
    {
      // Follow line
      lineFollow();
    }
  }
  
  // =============================================
  // SEND MOTOR COMMANDS
  // =============================================
  if(millis() - lastSend >= sendInterval)
  {
    lastSend = millis();
    sendHover(steerValue, speedValue);
  }
}