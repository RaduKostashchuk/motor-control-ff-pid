#include <Arduino.h>
#define MAX_CMD_LENGTH 12
#define MAX_CMD_PARTS 4

class Reader
{
  private:
    enum class State
    {
      Idle,
      Reading,
      Discarding
    };
    const char terminator = '\n';
    const char *cmdTooLong = "COMMAND TOO LONG";
    static constexpr size_t commandBufferSize = MAX_CMD_LENGTH;
    size_t commandLength;
    int lastRead;
    State fsmState;
    char command[commandBufferSize];
    bool hasCommand;

    void readUart()
    {
      lastRead = Serial.available() ? Serial.read() : -1;
      if (lastRead == '\r')
        lastRead = -1;
    }

    void handleIdle()
    {
      if (lastRead == -1 || lastRead == terminator)
        return;
      
      commandLength = 0;
      fsmState = State::Reading;
      handleReading();
    }

    void handleReading()
    {
      if (lastRead == -1)
        return;

      commandLength++;
      if (commandLength == commandBufferSize)
      {
        if (lastRead != terminator)
        {
          fsmState = State::Discarding;
          return;
        }
      }
      else
      {
        command[commandLength - 1] = (char)lastRead;
      }

      if (lastRead == terminator)
      {
        command[commandLength - 1] = '\0';
        hasCommand = true;
        fsmState = State::Idle;
      }
    }

    void handleDiscarding()
    {
      if (lastRead == terminator)
      {
        fsmState = State::Idle;
        Serial.println(cmdTooLong);
      }
    }

  public:
    Reader()
    {
      fsmState = State::Idle;
      hasCommand = false;
      commandLength = 0;
    }

    void read()
    {
      readUart();

      switch(fsmState)
      {
        case State::Idle:
          handleIdle();
          break;
    
        case State::Reading:
          handleReading();
          break;

        case State::Discarding:
          handleDiscarding();
      }
    }

    const char* tryPopCommand()
    {
      if (hasCommand)
      {
        hasCommand = false;
        return command;
      }

      return nullptr;
    }
};

enum class CommandType
{
  Motor,
  Stop,
  Status,
  AppMode,
  InvalidMotor,
  InvalidStop,
  InvalidMode,
  Unknown,
  None
};

enum class ModeType
{
  Auto,
  Manual
};

struct Command
{
  CommandType type = CommandType::None;
  ModeType modeType = ModeType::Manual;
  bool hasMotorNumber = false;
  long motorNumber = 0;
  long target = 0;
};

class Parser
{
  private:
    struct Token
    {
      char content[MAX_CMD_LENGTH] = {};
      size_t length = 0;
    };

    const char *motorCmd = "MOTOR";
    const char *stopCmd = "STOP";
    const char *modeCmd = "MODE";
    const char *autoModeCmd = "AUTO";
    const char *manualModeCmd = "MANUAL";
    const char *statusCmd = "STATUS";

  public:
    Command parse(const char* rawCmd)
    {
      const uint8_t statusCmdLen = 6;
      Token tokens[MAX_CMD_PARTS] = {};
      tokenize(tokens, rawCmd);

      Command command;
      command.type = CommandType::Unknown; 
      if (strcmp(tokens[0].content, motorCmd) == 0)
      {
        parseMotorCmd(tokens, command);
      }  
      else if (strcmp(tokens[0].content, stopCmd) == 0)
      {
        parseStopCmd(tokens, command);
      }
      else if (strcmp(tokens[0].content, modeCmd) == 0)
      {
        parseModeCmd(tokens, command);
      }
      else if (strcmp(rawCmd, statusCmd) == 0 && tokens[0].length == statusCmdLen)
      {
        command.type = CommandType::Status;
      }

      return command;
    }

    void tokenize(Token * tokens, const char* rawCmd)
    {
      size_t i = 0;
      size_t tokenCount = 1;
      while (*rawCmd != '\0' && tokenCount <= MAX_CMD_PARTS)
      {
        if (*rawCmd == ' ')
        {
          (*tokens).length = i;
          (*tokens).content[i] = '\0';
          tokenCount++;
          tokens++;
          i = 0;
        }
        else
        {
          (*tokens).content[i++] = *rawCmd;
          (*tokens).length = i;
        }

        rawCmd++;
      }
    }

    void parseModeCmd(Token * tokens, Command& command)
    {
      const uint8_t modeCmdLen = 4;
      command.type = CommandType::InvalidMode;
      if ((*tokens).length != modeCmdLen || (*(tokens + 2)).length != 0)
        return;

      const uint8_t autoCmdLen = 4;
      const uint8_t manualCmdLen = 6;
      Token secondArg = *(tokens + 1);
      if (strcmp(secondArg.content, autoModeCmd) == 0 && secondArg.length == autoCmdLen)
      {
        command.modeType = ModeType::Auto;
        command.type = CommandType::AppMode;
      }
      else if (strcmp(secondArg.content, manualModeCmd) == 0 && secondArg.length == manualCmdLen)
      {
        command.modeType = ModeType::Manual;
        command.type = CommandType::AppMode;
      }
    }

    void parseMotorCmd(Token * tokens, Command& command)
    {
      const uint8_t motorCmdLen = 5;
      command.type = CommandType::InvalidMotor;
      if ((*tokens).length != motorCmdLen || (*(tokens + 2)).length == 0 || (*(tokens + 3)).length != 0)
        return;

      char * secondArg = (*(tokens + 1)).content;
      long motorNumber = 0;
      if (tryGetLong(secondArg, motorNumber))
        command.motorNumber = motorNumber;
      else
        return;

      char * thirdArg = (*(tokens + 2)).content;
      long target = 0;
      if (tryGetLong(thirdArg, target))
      {
        command.type = CommandType::Motor;
        command.target = target;
      }
    }

    void parseStopCmd(Token * tokens, Command& command)
    {
      const uint8_t stopCmdLen = 4;
      command.type = CommandType::InvalidStop;
      if ((*tokens).length != stopCmdLen || (*(tokens + 2)).length != 0)
        return;

      Token * secondArg = tokens + 1;
      long motorNumber = 0;
      if ((*secondArg).length == 0)
      {
        command.type = CommandType::Stop;
      }
      else if (tryGetLong((*secondArg).content, motorNumber))
      {
        command.type = CommandType::Stop;
        command.motorNumber = motorNumber;
        command.hasMotorNumber = true;
      }
    }

    bool tryGetLong(char *arg, long& number)
    {
      char *end = NULL;
      number = strtol(arg, &end, 10);
      return end == arg || *end != '\0' ? false : true;
    }
};

class Encoder
{
  private:
    const byte _channelAPin;
    const byte _channelBPin;
    volatile long position = 0;

    static Encoder* instance1;
    static Encoder* instance2;

    static void updateTicks1()
    {
      if (instance1)
        instance1->position += digitalRead(instance1->_channelBPin) == HIGH ? 1 : -1; 
    }

    static void updateTicks2()
    {
      if (instance2)
        instance2->position += digitalRead(instance2->_channelBPin) == HIGH ? 1 : -1; 
    }

  public:
    Encoder(byte channelAPin, byte channelBPin) : _channelAPin(channelAPin), _channelBPin(channelBPin)
    {
      if (!instance1)
      {
        instance1 = this;
        attachInterrupt(digitalPinToInterrupt(_channelAPin), updateTicks1, RISING);
      }
      else if (!instance2)
      {
        instance2 = this;
        attachInterrupt(digitalPinToInterrupt(_channelAPin), updateTicks2, RISING);
      }
      else
      {
        Serial.println("ERROR TOO MANY ENCODER INSTANCES");
      }
      
      pinMode(_channelAPin, INPUT_PULLUP);
      pinMode(_channelBPin, INPUT_PULLUP);

      position = 0;
    }

    long getPosition()
    {
      long currentPosition;
      noInterrupts();
      currentPosition = position;
      interrupts();

      return currentPosition;
    }
};

Encoder* Encoder::instance1 = nullptr;
Encoder* Encoder::instance2 = nullptr;

class Motor
{
  private:
    const byte _powerPin;
    const byte _directionPin1;
    const byte _directionPin2;

  public:
    Motor(byte powerPin, byte directionPin1, byte directionPin2) : _powerPin(powerPin), _directionPin1(directionPin1), _directionPin2(directionPin2)
    {
      pinMode(_powerPin, OUTPUT);
      pinMode(_directionPin1, OUTPUT);
      pinMode(_directionPin2, OUTPUT);

      digitalWrite(_directionPin1, LOW);
      digitalWrite(_directionPin2, LOW);
      analogWrite(_powerPin, 0);
    }

    void update(int16_t pwm)
    {
      if (pwm == 0)
      {
        digitalWrite(_directionPin1, LOW);
        digitalWrite(_directionPin2, LOW);
      }
      else if (pwm > 0)
      {
        digitalWrite(_directionPin1, LOW);
        digitalWrite(_directionPin2, HIGH);
      }
      else
      {
        digitalWrite(_directionPin1, HIGH);
        digitalWrite(_directionPin2, LOW);
      }
      analogWrite(_powerPin, pwm);
    }
};

class Estimator
{
  private:
    long lastPosition;
    unsigned long updateRpmStart;
    const float ticksPerRevolution = 495.0;
    const float alpha = 0.3;
    float rpm;
    float filteredRpm;

    void estimateRpm(long currentPosition)
    {
      unsigned long now = millis();
      long deltaTicks = currentPosition - lastPosition;
      unsigned long deltaTime = now - updateRpmStart;
      if (deltaTime == 0) // guard against division by zero
        return;
      
      float deltaTimeSec = static_cast<float>(deltaTime) / 1000.0f;
      float ticksPerSec = static_cast<float>(deltaTicks) / deltaTimeSec;
      rpm = 60 * ticksPerSec / ticksPerRevolution;
      filteredRpm += alpha * (rpm - filteredRpm); // low pass filter for D

      updateRpmStart = now;
    }

  public:
    Estimator()
    {
      lastPosition = 0;
      updateRpmStart = 0;
    }

    void estimate(long currentPosition)
    {
      estimateRpm(currentPosition);
      lastPosition = currentPosition;
    }

    float getRpm()
    {
      return rpm;
    }

    float getFilteredRpm()
    {
      return filteredRpm;
    }
};

struct DriveState
{
  float cont_error;
  float cont_p;
  float cont_i;
  float cont_d;
  float cont_uff;
  int16_t cont_pwm;
  float traj_targetRpm;
  float esti_currentRpm;
  float app_desiredRpm;
};

class Controller
{
  private:
    const int16_t minPwm = -255;
    const int16_t maxPwm = 255;
    const float kp = 1;
    const float ki = 1;
    const float kd = 0;
    const float iDeadBand = 1.5;
    const float alpha = 0.3;
    const float ffCoef1 = 2.64;
    const float ffCoef2 = -1;
    float targetRpm;
    float error;
    float p;
    float i;
    float d;
    float rawDerivative;
    float filteredDerivative;
    float previousRpm;
    float currentRpm;
    float filteredRpm;
    float uff;
    unsigned long lastControllerRun;
    unsigned long lastCalcDRun;
    float iSum;
    bool newMeasurement;

    void calcI(float deltaT)
    {   
      float currentI = error * deltaT;
      float rawOutput = uff + p + (iSum + currentI) * ki + d;

      if ((rawOutput > maxPwm && error > 0) || (rawOutput < minPwm && error < 0)) //anti windup
        return;

      if (fabsf(error) <= iDeadBand)
        return;
    
      iSum += currentI;
      i = iSum * ki;
    }

    void calcD(unsigned long now)
    { 
      if (kd == 0 || !newMeasurement) // calculate only once for single estimator measurement
        return;

      if (lastCalcDRun == 0)
      {
        lastCalcDRun = now;
        newMeasurement = false;
        previousRpm = currentRpm;
        return;
      }

      unsigned long deltaMs = now - lastCalcDRun;
      if (deltaMs == 0) //division by zero guard
        return;

      if (fabsf(error) <= iDeadBand)
      {
        d = 0;
        return;
      }

      float deltaT = static_cast<float>(deltaMs) / 1000.0f; // seconds
      rawDerivative = (previousRpm - currentRpm) / deltaT;
      // filteredDerivative += alpha * (rawDerivative - filteredDerivative); // low pass filter

      const float maxD = 30;
      rawDerivative = constrain(rawDerivative, -maxD, maxD); // d clamping

      d = kd * rawDerivative;
      previousRpm = currentRpm;
      lastCalcDRun = now;
      newMeasurement = false;
    }

  public:
    Controller()
    {
      targetRpm = 0;
      error = 0;
      p = 0;
      i = 0;
      d = 0;
      rawDerivative = 0;
      filteredDerivative = 0;
      previousRpm = 0;
      currentRpm = 0;
      filteredRpm = 0;
      uff = 0;
      lastControllerRun = 0;
      lastCalcDRun = 0;
      iSum = 0;
      newMeasurement = false;
    }

    void setTargetRpm(float target)
    {
      targetRpm = target;
    }

    void setCurrentRpm(float current)
    {
      currentRpm = current;
    }

    void setFilteredRpm(float filtered)
    {
      filteredRpm = filtered;
    }

    void NewMeasurementOn()
    {
      newMeasurement = true;
    }

    void fillDriveState(DriveState& state)
    {
      state.cont_error = error;
      state.cont_p = p;
      state.cont_i = i;
      state.cont_d = d;
      state.cont_uff = uff;
      state.traj_targetRpm = targetRpm;
      state.esti_currentRpm = currentRpm;
    }

    int16_t control()
    {
      unsigned long now = millis();
      if (targetRpm == 0)
      {
        lastControllerRun = now;
        return 0;
      }
      
      error = targetRpm - currentRpm;
      p = error * kp;
      uff = ffCoef1 * targetRpm + ffCoef2;
      unsigned long deltaMs = now - lastControllerRun;
      if (deltaMs == 0) //division by zero guard
        return 0;

      float deltaT = static_cast<float>(deltaMs) / 1000.0f; // seconds
      calcD(now);
      calcI(deltaT);
      lastControllerRun = now;

      return constrain(uff + p + i + d, minPwm, maxPwm);
    }

    void reset()
    {
      targetRpm = 0;
      error = 0;
      p = 0;
      i = 0;
      d = 0;
      rawDerivative = 0;
      filteredDerivative = 0;
      previousRpm = 0;
      uff = 0;
      lastControllerRun = 0;
      lastCalcDRun = 0;
      iSum = 0;
      newMeasurement = false;
    }
};

class TrajectoryGenerator
{
  private:
    const float targetAcceleration = 5; // rpms per sec
    const float minRpm = 10;
    float targetRpm;
    unsigned long lastRun;
    float lastRpm = 0;

  public:
    TrajectoryGenerator()
    {
      lastRun = 0;
      targetRpm = 0;
      lastRpm = 0;
    }

    void setTargetRpm(float target)
    {
      targetRpm = target;
    }

    float generateRpm()
    {
      unsigned long now = millis();
      if (lastRun == 0)
      {
        lastRun = now;
        return 0;
      }
      
      float outRpm = 0;
      if (targetRpm < minRpm)
      {
        outRpm = 0;
      }
      else
      {
        float deltaSec = static_cast<float>(now - lastRun) / 1000.0f;
        outRpm = targetAcceleration * deltaSec + lastRpm;
        if (outRpm > targetRpm)
          outRpm = targetRpm;
        else if (outRpm < minRpm)
          outRpm = minRpm;
      }
      
      lastRun = now;
      lastRpm = outRpm;
      return outRpm;
    }

    void reset()
    {
      lastRun = 0;
      targetRpm = 0;
      lastRpm = 0;
    }
};

Reader reader;
Parser parser;
Encoder encoder1(2, 4);
Motor motor1(5, 6, 7);
Estimator estimator1;
Controller controller1;
TrajectoryGenerator trajectory1;
ModeType appMode = ModeType::Auto;
Command lastCmd;
DriveState driveState1;

unsigned const long estimatorInterval = 100;
unsigned long estimatorStart = 0;

unsigned const long controllerInterval = 50;
unsigned long controllerStart = 0;

unsigned const long trajectoryGenInterval = 100;
unsigned long trajectoryGenStart = 0;

unsigned const long printStatusInterval = 300000;
unsigned long printStatusStart = 0;

unsigned const long printTelemetryInterval = 20;
unsigned long printTelemetryStart = 0;

const char *successAck = "OK";
const char *unknownCmd = "ERROR UNKNOWN COMMAND";
const char *invalidMotorCmd = "ERROR USAGE MOTOR <motor number> <pwm OR rpm>";
const char *invalidStopCmd = "ERROR USAGE STOP <motor number> OR STOP";
const char *invalidModeCmd = "ERROR USAGE MODE <AUTO|MANUAL>";
const int16_t maxPwm = 255;
const int16_t minPwm = -255;
const int8_t maxRpm = 95;
const int8_t minRpm = -95;

void setup() {
  Serial.begin(115200); 
}

void printFullStatus()
{
  Serial.print("Application mode:  ");
  Serial.print(appMode == ModeType::Auto ? "Auto" : "Manual");

  Serial.print(";  Last motor 1 cmd:  ");
  switch (appMode)
  {
    case ModeType::Auto:
      Serial.print("Target Rpm: ");
      Serial.print(driveState1.app_desiredRpm);

      break;
    case ModeType::Manual:
      Serial.print("Pwm: ");
      Serial.print(driveState1.cont_pwm);
  }

  Serial.println();

  Serial.print("Encoder 1:  ");
  Serial.print("Position: ");
  Serial.print(encoder1.getPosition());

  Serial.print(" ||  Estimator 1:  ");
  Serial.print(";  RPM: ");
  Serial.print(estimator1.getRpm());

  Serial.println();
}

void executeModeCmd()
{
  if (lastCmd.modeType == ModeType::Auto)
    appMode = ModeType::Auto;
  else
    appMode = ModeType::Manual;

  Serial.println(successAck);
}

void executeMotorCmd()
{
  bool invalidParams = (lastCmd.motorNumber != 1 && lastCmd.motorNumber != 2)
    || (appMode == ModeType::Auto && (lastCmd.target > maxRpm || lastCmd.target < minRpm))
    || (appMode == ModeType::Manual && (lastCmd.target > maxPwm || lastCmd.target < minPwm));

  if (invalidParams)
  {
    Serial.println(invalidMotorCmd);
    return;
  }

  if (lastCmd.motorNumber == 1)
  {
    if (appMode == ModeType::Auto)
    {
      driveState1.app_desiredRpm = static_cast<float>(lastCmd.target);
      trajectory1.reset();
      trajectory1.setTargetRpm(driveState1.app_desiredRpm);
    }
    else if (appMode == ModeType::Manual)
    {
      driveState1.cont_pwm = static_cast<int16_t>(lastCmd.target);
      motor1.update(driveState1.cont_pwm);
    }
  }
  Serial.println(successAck);
}

void executeStopCmd()
{
  if (lastCmd.hasMotorNumber && lastCmd.motorNumber != 1 && lastCmd.motorNumber != 2)
  {
    Serial.println(invalidStopCmd);
    return;
  }

  if (!lastCmd.hasMotorNumber)
  {
    controller1.reset();
    trajectory1.setTargetRpm(0);
    motor1.update(0);
    driveState1.app_desiredRpm = 0;
    driveState1.traj_targetRpm = 0;
    driveState1.cont_pwm = 0;
  }
  else if (lastCmd.motorNumber == 1)
  {
    controller1.reset();
    trajectory1.setTargetRpm(0);
    motor1.update(0);
    driveState1.traj_targetRpm = 0;
    driveState1.app_desiredRpm = 0;
    driveState1.cont_pwm = 0;
  }

  Serial.println(successAck);
}

void execute()
{
  switch (lastCmd.type)
  {
    case CommandType::Status:
      printFullStatus();
      break;
    
    case CommandType::AppMode:
      executeModeCmd();
      break;

    case CommandType::Motor:
      executeMotorCmd();
      break;
    
    case CommandType::Stop:
      executeStopCmd();
      break;
    
    case CommandType::InvalidMotor:
      Serial.println(invalidMotorCmd);
      break;

    case CommandType::InvalidMode:
      Serial.println(invalidModeCmd);
      break;

    case CommandType::InvalidStop:
      Serial.println(invalidStopCmd);
      break;

    case CommandType::Unknown:
      Serial.println(unknownCmd);
      break;

    case CommandType::None:
      Serial.println("None");
      break;
  }
}

void loop() {
  reader.read();

  const char * rawCmd = reader.tryPopCommand();
  if (rawCmd)
  {
    lastCmd = parser.parse(rawCmd);
    execute();
  }

  unsigned long now = millis();
  if (now - estimatorStart >= estimatorInterval)
  {
    controller1.NewMeasurementOn();
    estimator1.estimate(encoder1.getPosition());
    controller1.setCurrentRpm(estimator1.getRpm());
    controller1.setFilteredRpm(estimator1.getFilteredRpm());
    estimatorStart = now;
  }

  // if (now - trajectoryGenStart >= trajectoryGenInterval && appMode == ModeType::Auto)
  // {
  //   controller1.setTargetRpm(trajectory1.generateRpm());
  //   trajectoryGenStart = now;
  // }

  if (now - controllerStart >= controllerInterval && appMode == ModeType::Auto)
  {
    controller1.setTargetRpm(driveState1.app_desiredRpm);
    driveState1.cont_pwm = controller1.control();
    motor1.update(driveState1.cont_pwm);

    controller1.fillDriveState(driveState1);
    controllerStart = now;
  }

  if (now - printStatusStart >= printStatusInterval)
  {
    printFullStatus();
    printStatusStart = now;
  }

  if (now - printTelemetryStart >= printTelemetryInterval)
  {
    Serial.print("T:");
    Serial.print(now);
    Serial.print(",");
    Serial.print(driveState1.app_desiredRpm);
    Serial.print(",");
    Serial.print(driveState1.traj_targetRpm);
    Serial.print(",");
    Serial.print(driveState1.esti_currentRpm);
    Serial.print(",");
    Serial.print(driveState1.cont_error);
    Serial.print(",");
    Serial.print(driveState1.cont_p);
    Serial.print(",");
    Serial.print(driveState1.cont_i);
    Serial.print(",");
    Serial.print(driveState1.cont_d);
    Serial.print(",");
    Serial.print(driveState1.cont_uff);
    Serial.print(",");
    Serial.println(driveState1.cont_pwm);

    printTelemetryStart = now;
  }
}