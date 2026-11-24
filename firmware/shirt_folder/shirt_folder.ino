#include <Arduino.h>                        // פונקציות בסיסיות של ארדואינו (pinMode, digitalWrite, millis וכו') — נכללות לקוד הליבה
#include <Servo.h>                          // ספריית Servo לשליטה במנועי סרוו (attach, write, detach)
#include <SAMDUE_PWM.h>                     // ספריית PWM ייעודית ל-Arduino Due לצורך setPWM בתדר/מקדמת מילוי נתונים

// ===== פינים =====
const uint8_t BTN_PIN = 3;                  // לחצן אל GND (נשתמש ב-INPUT_PULLUP, כלומר לא לחוץ=HIGH, לחוץ=LOW)
const uint8_t S1_PIN = 12, S2_PIN = 11, S3_PIN = 10; // שלושה פיני יציאה לשליטת הסרווים S1,S2,S3
const uint8_t LED_RED_PIN = 26, LED_GREEN_PIN = 27, BUZZER_PIN = 4; // פינים ללדים (אדום/ירוק) ולבאזר
const uint8_t POT_PIN = A0;                 // כניסת האנלוג (פוטנציומטר): A0 — מתח 0..3.3V ב-Due

// ===== 7-סגמנט CA — A..G,DP =====
const uint8_t segA=30, segB=31, segC=32, segD=33, segE=34, segF=35, segG=36, segDP=37; // מיפוי רגלי מקטעים a..g ו־נקודה עשרונית ל־D30..D37
inline void SEG_ON(uint8_t p){ digitalWrite(p, LOW); }   // Common Anode: לוגיקה הפוכה — LOW מדליק מקטע (זרם זורם מהאנודה המשותפת דרך הנגד)
inline void SEG_OFF(uint8_t p){ digitalWrite(p, HIGH);}  // HIGH מכבה את המקטע (אין הפרש פוטנציאלים דרך הנגד)
void segAllOff()
{ 
SEG_OFF(segA); SEG_OFF(segB); SEG_OFF(segC); SEG_OFF(segD); // כיבוי כל המקטעים: a,b,c,d
SEG_OFF(segE); SEG_OFF(segF); SEG_OFF(segG); SEG_OFF(segDP);// המשך כיבוי: e,f,g ו־נקודה עשרונית 
}
void showDigitCA(uint8_t d) // מציג ספרה 0..9 על 7-סגמנט מסוג Common Anode לפי תבנית מקטעים
{                 
  segAllOff();                               // קודם כל מכבים הכל כדי למנוע "מריחה" בין ספרות
  switch(d)                                 // בוחרים אילו מקטעים להדליק בהתאם לספרה
  {
    case 0: SEG_ON(segA); SEG_ON(segB); SEG_ON(segC); SEG_ON(segD); SEG_ON(segE); SEG_ON(segF); break; // ספרה 0: מדליקים a,b,c,d,e,f (g כבוי)
    case 1: SEG_ON(segB); SEG_ON(segC); break;                                                             // ספרה 1: רק b,c
    case 2: SEG_ON(segA); SEG_ON(segB); SEG_ON(segG); SEG_ON(segE); SEG_ON(segD); break;                   // ספרה 2: a,b,g,e,d
    case 3: SEG_ON(segA); SEG_ON(segB); SEG_ON(segC); SEG_ON(segD); SEG_ON(segG); break;                   // ספרה 3: a,b,c,d,g
    case 4: SEG_ON(segF); SEG_ON(segG); SEG_ON(segB); SEG_ON(segC); break;                                 // ספרה 4: f,g,b,c
    case 5: SEG_ON(segA); SEG_ON(segF); SEG_ON(segG); SEG_ON(segC); SEG_ON(segD); break;                   // ספרה 5: a,f,g,c,d
    case 6: SEG_ON(segA); SEG_ON(segF); SEG_ON(segE); SEG_ON(segD); SEG_ON(segC); SEG_ON(segG); break;     // ספרה 6: a,f,e,d,c,g
    case 7: SEG_ON(segA); SEG_ON(segB); SEG_ON(segC); break;                                               // ספרה 7: a,b,c
    case 8: SEG_ON(segA); SEG_ON(segB); SEG_ON(segC); SEG_ON(segD); SEG_ON(segE); SEG_ON(segF); SEG_ON(segG); break; // ספרה 8: כל המקטעים
    case 9: SEG_ON(segA); SEG_ON(segB); SEG_ON(segC); SEG_ON(segD); SEG_ON(segF); SEG_ON(segG); break;     // ספרה 9: a,b,c,d,f,g
    default: break;                               // ערך לא חוקי: לא מדליקים כלום
  }
}
void countdown5to1()
{
   for(int s=5; s>=1; --s){ showDigitCA(s); delay(1000); } segAllOff(); // ספירה לאחור 5→1 בהצגה של שנייה לכל ספרה, בסוף מכבים
 }
// ===== שליטה בעוצמת הבאזר דרך הפוטנציומטר =====
volatile uint8_t g_potDutyPct = 50;          // אחראי לקריאה דל ערך הבאזר סתם הגדרנו חמישים כרגע 

// קריאה "רגועה" של A0 (ממוצע 4 דגימות)
inline uint16_t readPot12()                 // פונקציה לקריאת הפוטנציומטר ברזולוציית 12 ביט עם ממוצע כדי להפחית רעש
{ 
  uint32_t sum=0; for(int i=0;i<4;i++){ sum+=analogRead(POT_PIN); delayMicroseconds(120); } // קורא 4 פעמים עם השהייה קצרה ומסכֵם
  return sum/4; // 0..4095                         // ממוצע → מחזיר ערך בתחום 0..4095 (ADC של Due)
}

// עדכון duty% לפי מצב הפוטנציומטר:ת
inline void updateBuzzerDutyFromPot() // ממפה את קריאת ה-ADC ל ושומר ב־g_potDutyPct
{        
  uint16_t raw = readPot12();                 // קריאה מסוננת: 0..4095
  long d = map((long)raw, 0L, 4095L, 1L, 99L); // הטווח הרצוי בשביל למנוע עיוותים 
  if (d < 1) d = 1; else if (d > 100) d = 99;  // טווח הרצוי (בטיחותי יותר לסאונד/הספק)
  g_potDutyPct = (uint8_t)d;                  // שמירה כ-uint8_t
}

// ===== באזר (SAMDUE_PWM) =====
SAMDUE_PWM* _buzzPWM = nullptr;               // מצביע לאובייקט PWM; ניצור בפעם הראשונה
static void buzz(unsigned freq, unsigned ms)// מפעיל באזר בתדר 'freq' למשך 'ms' מילישניות עם duty% מהפוטנציומטר
{ 
  const float duty = (float)g_potDutyPct;     // המרה של הערך ששמרנו בפונקציה הקודמת למספר עשרוני 
  if (_buzzPWM == nullptr) 
  {
  _buzzPWM = new SAMDUE_PWM(BUZZER_PIN, freq, duty);
  }
  _buzzPWM->setPWM(BUZZER_PIN, freq, duty);   // START: הפעלת PWM בתדר ובעוצמה שנבחרו
  delay(ms);                                   // השהייה למשך הנגינה
  _buzzPWM->setPWM(BUZZER_PIN, freq, 0.0f);   // STOP: כיבוי הבאזר ע"י קביעת duty ל־0%
}
inline void beepLoud() { buzz(3200, 180); delay(80); buzz(2500, 220); } // רצף "רועם": 3.2kHz ואז 2.5kHz עם הפסקה קצרה

// ===== סרווים =====
Servo s1,s2,s3;                               // יצירת שלושה אובייקטי Servo
const int NEUTRAL_ANGLE=180;                  // זווית "מנוחה" — כאן 180°
const int S1_TARGET=80, S2_TARGET=60, S3_TARGET=60; // יעדי תנועה לכל סרוו (לוחות קיפול שונים)
const int STEP_DEG=2, HOLD_AT_TARGET_MS=300;  // מהירות (גודל צעד בזווית) והחזקה ביעד למשך 300ms
int STEP_DELAY_MS=12;                         // השהייה בין צעדים (קובע מהירות תנועה בפועל)

void smoothMove(Servo &sv, int fromA, int toA, int step=STEP_DEG, int stepDelay=STEP_DELAY_MS)// תנועה חלקה: זזים בצעדים קטנים מהתחלה לסוף
{ 
  if(fromA<toA)
  {
     for(int a=fromA;a<=toA;a+=step){ sv.write(a); delay(stepDelay);} // אם היעד גדול — עולים בזווית בצעדים
     } 
  else   
      {
     for(int a=fromA;a>=toA;a-=step){ sv.write(a); delay(stepDelay);// אחרת — יורדים בזווית בצעדים
     }
 }
  sv.write(toA);                               // וידוא הגעה מדויקת לזווית היעד בסוף
}
void moveOneServo(Servo &sv, uint8_t pin, int target)// רצף מלא עבור סרוו אחד: attach → למנוחה → ליעד → החזקה → חזרה → detach
{ 
  if(!sv.attached()) sv.attach(pin);           // מצרפים לפין אם לא מחובר
  sv.write(NEUTRAL_ANGLE); delay(150);         // מעבר קצר למנוחה והמתנה לייצוב
  smoothMove(sv, NEUTRAL_ANGLE, target, STEP_DEG, STEP_DELAY_MS); // תנועה חלקה למטרה
  delay(HOLD_AT_TARGET_MS);                    // החזקת הזרוע ביעד
  smoothMove(sv, target, NEUTRAL_ANGLE, STEP_DEG, STEP_DELAY_MS); // חזרה למנוחה
  sv.detach();                                 // ניתוק — מפחית רעידות וצריכת זרם כשהסרוו במנוחה
}

// ===== לוגיקת רצף =====
volatile bool busy=false;                      // דגל: המערכת עסוקה בביצוע רצף (מונע טריגרים כפולים)
void runSequenceOnce() // מפעיל את הרצף פעם אחת: לדים, ספירה לאחור, שלושת הסרווים, צפצוף סיום
{                       
  busy = true;                                 // מסמן עסוק
  digitalWrite(LED_RED_PIN, HIGH);             // אדום דלוק — אזהרה לא לגעת
  digitalWrite(LED_GREEN_PIN, LOW);            // ירוק כבוי בזמן הריצה

  countdown5to1();                             // ספירה לאחור על ה־7-סגמנט (5..1)

  moveOneServo(s1,S1_PIN,S1_TARGET); delay(200); // סרוו 1 → יעד → חזרה (עם השהייה קטנה בין הסרווים)
  moveOneServo(s2,S2_PIN,S2_TARGET); delay(200); // סרוו 2
  moveOneServo(s3,S3_PIN,S3_TARGET); delay(100); // סרוו 3

  busy = false;                                // סיום הרצף
  digitalWrite(LED_RED_PIN, LOW);              // אדום כבה
  digitalWrite(LED_GREEN_PIN, HIGH);           // ירוק נדלק — מוכן
  beepLoud();                                  // איתות סיום (עוצמה נלקחת מהפוטנציומטר)
}

// ===== פסיקה ללחצן =====
volatile bool seqRequested=false;              // דגל שמבקש להריץ רצף (נקרא ב-loop)
volatile unsigned long lastIRQms=0;           // זמן אחרון של פסיקה לצורך דיבאונס
const unsigned long ISR_DEBOUNCE_MS=60;       // חלון דיבאונס 60ms
void onButtonFalling() // פונקציית ISR לפאילה יורדת של הלחצן
{                       
  unsigned long now = millis();                // חותמת זמן נוכחית
  if (busy) return;                            // אם עסוקים — מתעלמים
  if (now - lastIRQms < ISR_DEBOUNCE_MS) return; // דיבאונס: אם עבר פחות מהחלון — מתעלמים
  lastIRQms = now;                             // עדכון זמן הפסיקה
  seqRequested = true;                         // מרימים דגל להריץ רצף (העבודה עצמה לא ב־ISR)
}

// ===== setup / loop =====
void setup()                           // פונקציית אתחול — רצה פעם אחת בהדלקה/ריסט
{       
  // IO בסיסי
  pinMode(BTN_PIN,INPUT_PULLUP);               // לחצן עם נגדי משיכה פנימיים (לוגיקה הפוכה)
  pinMode(LED_RED_PIN,OUTPUT); pinMode(LED_GREEN_PIN,OUTPUT); // לדים כפלט
  pinMode(BUZZER_PIN,OUTPUT);                  // באזר כפלט (פיזי; PWM יגיע מהספרייה)
  digitalWrite(LED_RED_PIN,LOW);               // אדום כבוי בהתחלה
  digitalWrite(LED_GREEN_PIN,HIGH);            // ירוק דלוק — מצב מוכן

  // 7-סגמנט
  pinMode(segA,OUTPUT); pinMode(segB,OUTPUT); pinMode(segC,OUTPUT); pinMode(segD,OUTPUT); // הגדרת כל מקטעי ה־7-סגמנט כפלט
  pinMode(segE,OUTPUT); pinMode(segF,OUTPUT); pinMode(segG,OUTPUT); pinMode(segDP,OUTPUT); // המשך הגדרות פלט
  segAllOff();                                 // מכבים את התצוגה בתחילת הדרך

  // סריאל
  Serial.begin(115200); while(!Serial){;}      // פתיחת תקשורת סריאלית 115200 והמתנה לחיבור (ב-Due/Native USB)

  // סרווים למנוחה ראשונית
  s1.attach(S1_PIN); s1.write(NEUTRAL_ANGLE); delay(50); s1.detach(); // כל סרוו: attach קצר → כתיבה למנוחה → ניתוק
  s2.attach(S2_PIN); s2.write(NEUTRAL_ANGLE); delay(50); s2.detach();
  s3.attach(S3_PIN); s3.write(NEUTRAL_ANGLE); delay(50); s3.detach();

  // ADC (Due = 12bit) לפוטנציומטר
  analogReadResolution(12);                    // קובע רזולוציית ADC ל־12 ביט (0..4095)
  pinMode(POT_PIN, INPUT);                     //A0 קלט אנולגי
  // פסיקה
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), onButtonFalling, FALLING); // חיבור פסיקה ללחצן — טריגר על מעבר HIGH→LOW
}

void loop()                                   // הלולאה הראשית — רצה שוב ושוב
{
  // קבלת בקשת רצף מה-ISR
  noInterrupts();                              // אזור קריטי: מונעים שינוי של seqRequested בזמן הקריאה
  bool doRun = seqRequested;                   // מעתיקים את הדגל למשתנה מקומי
  if (doRun) seqRequested = false;             // מאפסים את הדגל (צריכה חד־פעמית)
  interrupts();                                // סוף אזור קריטי — מחזירים פסיקות

  // עידכון עוצמת הבאזר מהפוטנציומטר באופן רציף
  updateBuzzerDutyFromPot();                   // A0 קורא ,g_potDutyPct  מעדכן 

  if (doRun) runSequenceOnce();                // אם התקבלה בקשה — מריצים את הרצף (ספירה, סרווים, איתות סיום)
}
