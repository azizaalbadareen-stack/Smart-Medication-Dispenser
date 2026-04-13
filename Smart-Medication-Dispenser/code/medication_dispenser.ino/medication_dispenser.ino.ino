#include <Servo.h>
#include <RTC.h>   // Arduino UNO R4 built-in RTC
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// If the screen is blank, change 0x27 to 0x3F
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// -------------------- PINS --------------------
const int SERVO_PIN  = 9;
const int RED_LED    = 7;  
const int BUZZER_PIN = 6;  
const int BUTTON_PIN = 2;  

// -------------------- SERVO SETTINGS ----------
const int HOME_ANGLE = 0;
// Slot angles: Slot 0 (0°), Slot 1 (45°), Slot 2 (90°), Slot 3 (135°)
const int SLOT_ANGLES[4] = {0, 45, 90, 135}; 
int slotIndex = -1; 

// -------------------- TIMING ------------------
const unsigned long DROP_TIME = 800;       
const unsigned long CONFIRMED_TIME = 3000; // Increased to 3 seconds so you can read "Stay Healthy"
const unsigned long BLINK_TIME = 300;      
const unsigned long BEEP_ON = 150;         
const unsigned long BEEP_OFF = 250;        

// -------------------- STATE MACHINE ----------
enum State { IDLE, DISPENSE, ALERT, CONFIRMED };
State state = IDLE;

Servo servo;
unsigned long stateStart = 0;
unsigned long lastBlink = 0;
unsigned long lastBeep = 0;
bool ledState = false;
bool beepState = false;

int lastMinute = -1;

// -------------------- RTC SYNC ----------------
Month monthFromString(const char* m) {
  if (!strcmp(m, "Jan")) return Month::JANUARY;
  if (!strcmp(m, "Feb")) return Month::FEBRUARY;
  if (!strcmp(m, "Mar")) return Month::MARCH;
  if (!strcmp(m, "Apr")) return Month::APRIL;
  if (!strcmp(m, "May")) return Month::MAY;
  if (!strcmp(m, "Jun")) return Month::JUNE;
  if (!strcmp(m, "Jul")) return Month::JULY;
  if (!strcmp(m, "Aug")) return Month::AUGUST;
  if (!strcmp(m, "Sep")) return Month::SEPTEMBER;
  if (!strcmp(m, "Oct")) return Month::OCTOBER;
  if (!strcmp(m, "Nov")) return Month::NOVEMBER;
  return Month::DECEMBER;
}

void syncRTC() {
  char d[] = __DATE__;   
  char t[] = __TIME__;   
  Month mon = monthFromString(d);
  int day  = atoi(d + 4);
  int year = atoi(d + 7);
  int hour = atoi(t);
  int min  = atoi(t + 3);
  int sec  = atoi(t + 6);
  RTCTime nowTime(year, mon, day, hour, min, sec, DayOfWeek::MONDAY, SaveLight::SAVING_TIME_INACTIVE);
  RTC.setTime(nowTime);
}

// -------------------- SETUP ------------------
void setup() {
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("System Starting");
  delay(800);
  
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  servo.attach(SERVO_PIN);
  servo.write(SLOT_ANGLES[0]); 

  Serial.begin(9600);
  RTC.begin();
  syncRTC();

  digitalWrite(RED_LED, HIGH);
  delay(200);
  digitalWrite(RED_LED, LOW);
  lcd.clear();
}

// -------------------- LOOP -------------------
void loop() {
  RTCTime now;
  RTC.getTime(now);
  int minute = now.getMinutes();
  int seconds = now.getSeconds();

  // -------------------- LCD DISPLAY LOGIC --------------------
  
  // 1. IDLE MODE: Show Time + Countdown
  if (state == IDLE) {
    // Line 1: Real Time
    lcd.setCursor(0,0);
    char buf[17];
    snprintf(buf, sizeof(buf), "Time: %02d:%02d:%02d", now.getHour(), now.getMinutes(), now.getSeconds());
    lcd.print(buf);
    
    // Line 2: Countdown to next minute (next pill)
    lcd.setCursor(0,1);
    lcd.print("Next Pill: ");
    int timeLeft = 60 - seconds;
    lcd.print(timeLeft);
    lcd.print("s   "); // Spaces to clear old numbers
  }
  
  // 2. ALERT MODE: Show "Take Medication"
  else if (state == ALERT) {
    lcd.setCursor(0,0);
    lcd.print("   ALERT!       "); // Spaces to center/clear
    lcd.setCursor(0,1);
    lcd.print("Take Medication ");
  }

  // 3. CONFIRMED MODE: Show "Stay Healthy :)"
  else if (state == CONFIRMED) {
    lcd.setCursor(0,0);
    lcd.print("   Done!        ");
    lcd.setCursor(0,1);
    lcd.print("Stay Healthy :) ");
  }

  // -------------------- STATE MACHINE LOGIC --------------------

  // ---------- IDLE ----------
  if (state == IDLE) {
    digitalWrite(RED_LED, LOW);
    noTone(BUZZER_PIN);

    // Trigger every new minute
    if (minute != lastMinute) {
      lastMinute = minute;
      state = DISPENSE;
      stateStart = millis();
      
      // Move to NEXT slot
      slotIndex = (slotIndex + 1) % 4; 
      servo.write(SLOT_ANGLES[slotIndex]);
    }
  }

  // ---------- DISPENSE ----------
  else if (state == DISPENSE) {
    if (millis() - stateStart >= DROP_TIME) {
      state = ALERT;
      stateStart = millis();
      lastBlink = millis();
      lastBeep = millis();
      ledState = true;
      digitalWrite(RED_LED, HIGH); 
    }
  }

  // ---------- ALERT ----------
  else if (state == ALERT) {
    // LED BLINK
    if (millis() - lastBlink >= BLINK_TIME) {
      lastBlink = millis();
      ledState = !ledState;         
      digitalWrite(RED_LED, ledState); 
    }

    // BUZZER BEEP
    if (!beepState && millis() - lastBeep >= BEEP_OFF) {
      beepState = true;
      lastBeep = millis();
      tone(BUZZER_PIN, 2000); 
    }
    if (beepState && millis() - lastBeep >= BEEP_ON) {
      beepState = false;
      lastBeep = millis();
      noTone(BUZZER_PIN);
    }

    // BUTTON CONFIRM
    if (digitalRead(BUTTON_PIN) == LOW) {
      digitalWrite(RED_LED, LOW);
      noTone(BUZZER_PIN);
      state = CONFIRMED;
      stateStart = millis();
      lcd.clear(); // Clear screen once so "Stay Healthy" looks clean
    }
  }

  // ---------- CONFIRMED ----------
  else if (state == CONFIRMED) {
    // Wait for the "Stay Healthy" message to show
    if (millis() - stateStart >= CONFIRMED_TIME) {
      
      // RESET LOGIC (Silent return to 0 after 4th pill)
      if (slotIndex == 3) {
         servo.write(HOME_ANGLE); 
      }

      state = IDLE;
      lcd.clear(); // Clear screen for the clock to return
    }
  }
}