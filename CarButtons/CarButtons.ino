
// используемые библиотеки
#include <EEPROM.h>
#include <EncButton.h>



// настройки под себя
#define LOG_ENABLED 0             // лог в консоль 0 - выкл, 1 - вкл
#define BTN_SEND 150              // сколько мс держать замкнутой кнопку, чтобы считалось одиночное нажатие
#define BTN_SET 2000              // длительность в мс длительного нажатия кнопки
#define ACC_IN 14                 // пин ардуино для входа канала ACC IN (есть ли ACC на пине)
#define POWER_OUT 19              // пин ардуино для выхода канала POWER (подает 12В на магнитолу при необходимости включения)
#define POWER_OFF_LAG 5000        // через сколько веремни мс после пропадания сигнала ACC нужно выключить магнитолу
#define POWER_OFF_TIMER 2400000   // 40 мин, через сколько времени непрерывной игры выключать магнитолу, если ее принудительно включили без ACC (чтоб аккум не сдох)

constexpr int8_t g_volume_pins[3] = {10, 11, 12}; // входные пины с выводов енкодера панели кнопок
constexpr int8_t left_out = 7;                    // выходной пин "поворот влево" или "громкость ниже"
constexpr int8_t right_out = 8;                   // выходной пин "поворот вправо" или "громкость выше"
constexpr int8_t s_but = 9;                       // входной пин с кнопки "POWER" панели кнопок



// инициализация
#if LOG_ENABLED
#define PRINT(x) Serial.print(x)
#define PRINTHEX(x) Serial.print(x, HEX)
#define PRINTLN(x) Serial.println(x)
#else
#define PRINT(x)
#define PRINTHEX(x)
#define PRINTLN(x)
#endif

boolean set_mode;
boolean is_on;
boolean is_acc;
boolean is_service;
boolean pwr_timer_started;
boolean lag_timer_started;
boolean is_manual;
unsigned long pwr_timer;
unsigned long lag_timer;

int volume_state; // 0 - NONE, 1 - LEFT, 2 - RIGHT
int8_t active_pin_ = 0;
boolean vol_changing;
String gaps;
unsigned long volume_timer;

EncButton <EB_TICK, s_but> btnP;

// запуск
void setup()
{
#if LOG_ENABLED
	Serial.begin(115200);
#endif

  PRINTLN("Started...");

  btnP.setHoldTimeout(BTN_SET);
  btnP.setStepTimeout(BTN_SEND);

  pinMode(ACC_IN, INPUT_PULLUP);
  pinMode(POWER_OUT, OUTPUT);
  digitalWrite(POWER_OUT, LOW);
  for (int pin : g_volume_pins)
  {
    pinMode(pin, INPUT_PULLUP);
  }
  pinMode(s_but, INPUT_PULLUP);
  pinMode(left_out, OUTPUT);
  pinMode(right_out, OUTPUT);
  digitalWrite(left_out, LOW);
  digitalWrite(right_out, LOW);
  
  set_mode = false;
  is_acc = false;
  is_on = false;
  is_service = false;
  is_manual = false;
  pwr_timer_started = false;
  if (EEPROM[0] == 255) EEPROM.put(0, is_service); // загружаем из постоянной памяти флаг сервисного режима
  else EEPROM.get(0, is_service);
}



// рабочий цикл
void loop()
{
  btnP.tick(); //парсим и обновляем статус кнопки питания
  //длительное удержание кнопки питания: если магнитола была включена - выключаем, если выключение при включенном ACC, то переводим в сервисный режим заодно
  if (btnP.held()) {
    #if LOG_ENABLED
    Serial.println("action held"); 
    #endif
    if (digitalRead(POWER_OUT)) { 
      digitalWrite(POWER_OUT, LOW);
      if (!is_service && is_acc) {
        is_service = true; 
        EEPROM.put(0, is_service);
      }
    }
  } 
  // короткое нажатие кнопки питания - включаем магнитолу, если выключена, если ACC при этом включено, то выключаем сервисный режим заодно
  if (btnP.hasClicks(1)) {
    #if LOG_ENABLED
    Serial.println("action 1 click");
    #endif 
    lag_timer = 0; 
    lag_timer_started = false; 
    is_manual = true; 
    if (!digitalRead(POWER_OUT)) {
      digitalWrite(POWER_OUT, HIGH);
    } 
    if(is_service && is_acc) {
      is_service = false; 
      EEPROM.put(0, is_service);
    }
  }
  // троекратное нажатие кнопки питания - включение/выключение режима обучения кнопок енкодера. при однократном повороте енкодера в какую либо сторону происходит "залипание" данного события, чтобы в обучающей программе магнитолы успеть зафиксировать код кнопки и привязать
  if (btnP.hasClicks(3)) {
    #if LOG_ENABLED 
    Serial.println("action 3 clicks");
    #endif 
    set_mode = !set_mode;
  }
  
  update_state(); // парсим и обновляем статус енкодера
  volume_tick();  // обработка команд, связанных с енкодером
  power_tick();   // обработка команд, связанных с кнопкой питания
}



void power_tick()
{
  boolean cur_is_acc = digitalRead(ACC_IN);
  if (cur_is_acc != is_acc) {
    is_acc = cur_is_acc; 
    is_manual=false;
  }
  is_on = digitalRead(POWER_OUT);

  // если есть АСС и не включен сервисный режим
  if (is_acc && !is_service) {
    pwr_timer = 0; 
    pwr_timer_started = false; 
    lag_timer = 0; 
    lag_timer_started = false; 
    digitalWrite(POWER_OUT, HIGH); 
    is_on = true;
  }
  // если нет АСС и лаг таймер не запущен и не было ручного включения
  if (!is_acc && !lag_timer_started && !is_manual) {
    pwr_timer = 0; 
    pwr_timer_started = false; 
    lag_timer = millis(); 
    lag_timer_started = true;
  }
  // если включена, нет АСС, запущен ЛАГ таймер и время таймера превышено
  if (is_on && !is_acc && lag_timer_started && millis() - lag_timer >= POWER_OFF_LAG) {
    pwr_timer = 0; 
    pwr_timer_started = false; 
    lag_timer = 0; 
    lag_timer_started = false; 
    digitalWrite(POWER_OUT, LOW); 
    is_on=false;
  }
  // если включена, нет АСС и не запущен таймер отключения питания
  if (is_on && !is_acc && !pwr_timer_started) {
    pwr_timer = millis(); 
    pwr_timer_started = true;
  }
  // если запущен таймер питания и он превышен
  if (pwr_timer_started && millis() - pwr_timer >= POWER_OFF_TIMER) {
    pwr_timer = 0; 
    pwr_timer_started = false; 
    lag_timer = 0; 
    lag_timer_started = false; 
    digitalWrite(POWER_OUT, LOW); 
    is_on=false;
  }
}



void update_state()
  {
    const auto current_pin = [] () {
      for (const auto pin : g_volume_pins)
      {
        if (digitalRead(pin) == LOW)
        {
          return pin;
        }
      }
      return int8_t(0);
    }();

    if (current_pin == 0)
    {
      volume_state = 0;
      return;
    }
   
    const auto delta = active_pin_ - current_pin;
    active_pin_ = current_pin;

    if (delta == 1 || delta == -2)
    {
      volume_state = 2;
      if (LOG_ENABLED) PRINTLN("RIGHT");
      return;
    }
    if (delta == -1 || delta == 2)
    {
      volume_state = 1;
      if (LOG_ENABLED) PRINTLN("LEFT");
      return;
    }
    volume_state = 0;
  }

void volume_tick()
{
  int act_timer = BTN_SEND; // выбираем длительность нажатия кнопки
  if (set_mode) act_timer = BTN_SET;
  
  if (volume_state > 0 && !vol_changing) // изменяем громкость нажатием
  {
    vol_changing = true;
    volume_timer = millis();
    if (volume_state == 1) digitalWrite(left_out, HIGH);
    if (volume_state == 2) digitalWrite(right_out, HIGH);
  } else if (volume_state > 0 && vol_changing && !set_mode) // накапливаем долги
  {
    if (volume_state == 1) gaps += 'L';
    if (volume_state == 2) gaps += 'R';
   } else if (millis() - volume_timer > act_timer && vol_changing) // отпускаем нажатие громкости
   {
     vol_changing = false;
     digitalWrite(left_out, LOW);
     digitalWrite(right_out, LOW);
   } else if (gaps.length() > 0 && !vol_changing) // обрабатываем долги 
   {
    if (gaps[0] == 'L') digitalWrite(left_out, HIGH);
    if (gaps[0] == 'R') digitalWrite(right_out, HIGH);
    gaps.remove(0, 1);
    vol_changing = true;
    volume_timer = millis();
   }
}
