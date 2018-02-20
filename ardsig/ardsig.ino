/************************************************************************************************************************************
Программный код для охранной сигнализации (системы оповещения)

Автор: Игорь Ткачук
*************************************************************************************************************************************/

#include <SoftwareSerial.h>                     // Библиотека програмной реализации обмена по UART-протоколу
SoftwareSerial SIM800(8, 9);                    // RX, TX

#include <DHT.h>                                //библиотека для работы датчика температуры и влажности
#include <DHT_U.h>
#define DHTPIN 3                                //датчик DHT подключен ко входу 3
#define DHTTYPE DHT22 

#include <Wire.h>                               //Date and time functions using a DS1307 RTC connected via I2C and Wire lib
                                                //Для Arduino Nano выводы I2C: A4 (SDA) и A5 (SCL).
#include <RTClib.h>                             //Библиотека часов

#define IRDPIN 6                                //датчик HC-SR501 подключен ко входу 4
#define RELE1PIN 3                              //сигнальный контакт реле "один" подключен ко входу 4
#define GKNPIN 2                                //геркон подключен ко входу 2

#define DISARM 0
#define ARM 1
#define PREALARM 2
#define ALARM 3
#define ALARMSG "ALARM"                         //сообщение отсылаемое посредством SMS в момент перевода системы в статус ТРЕВОГА

#define USSDBALCMD *111#                        //USSD-запрос для получения баланса на симке

                                                //желательно хранить эти значения в EEPROM и иметь возможность изменять их посредством интерфейса или SMS
#define LIGHTSHEDON 22                          //время включения освещения по расписаню
#define LIGHTSHEDOFF 23                         //время отключения освещения по расписаню

float temph[2];                                 //массив для температуры и влажности
volatile unsigned long int tempTimer = 0;       //переменная для таймера обновления показаний температуры и влажности
volatile unsigned long int tempClock = 0;       //переменная для сохранения значения таймера
volatile boolean tempTimerOn = 0;               //переменная для запуска/остановки таймера

volatile unsigned long int shedTimer = 0;       //переменная для таймера обновления показаний температуры и влажности
volatile unsigned long int shedClock = 0;       //переменная для сохранения значения таймера
volatile boolean shedTimerOn = 0;               //переменная для запуска/остановки таймера

String msgbody = ""; 
String msgphone = ""; 
String balphone = "";                           //переменная для хранения номера телефона абонента запросившего баланс по карте

bool lightOn = false;                           //переменная для хранения состояния освещения

int ird_value = 0;                              //переменная для хранения статуса датчика движения
int gkn_value = 0;

bool rtc_present = false;                       //переменная хранит статус подключения RTC

int sysStatus = DISARM;                         // переменная хранит текущее состояние системы: DISARM, ARM, etc.

DHT dht(DHTPIN, DHTTYPE);                       //настройка датчика температуры и влажности
RTC_DS1307 rtc;

String _response = "";                          // Переменная для хранения ответа модуля SIM800L

ISR (TIMER0_COMPA_vect)                         //функция, вызываемая таймером-счетчиком каждые 0,001 сек
{
  if(tempTimerOn == 1)                          //если таймер включен
  {
      tempTimer++;                              //увеличение значения таймера на +1 каждые 0,001 сек
  }
}



void setup() {

  dht.begin();                          //инициализация датчика температуры

  if (! rtc.begin()) {                  //инициализация RTC
    Serial.println("Couldn't find RTC");
  } else {
    rtc_present = true;
  }

  if (! rtc.isrunning() && rtc_present) {
    Serial.println("RTC is NOT running!");
    // following line sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }
  
  pinMode(IRDPIN, INPUT);               //настройка пина Arduino на который подключен датчик HC-SR501
  pinMode(GKNPIN, INPUT);               //настройка пина Arduino на который подключен геркон
  pinMode(RELE1PIN, OUTPUT);            //настройка пина Arduino на который подключено первое реле
  
  //Настройка таймера на срабатывание каждые 0,001 сек
  TCCR0A |= (1 << WGM01);
  OCR0A = 0xF9;                                       //начало отсчета до переполнения (255)
  TIMSK0 |= (1 << OCIE0A);                            //Set the ISR COMPA vect
  sei();                                              //разрешить прерывания
  TCCR0B |= (1 << CS01) | (1 << CS00);                //установить делитель частоты на 64
  //теперь каждые 0,001 сек будет вызываться функция ISR (TIMER0_COMPA_vect)

   Serial.begin(9600);                                // Скорость обмена данными с компьютером 
   SIM800.begin(9600);                                // Скорость обмена данными с модемом 
   Serial.println("Start!"); 
   
   sendATCommand("AT", true);                         // Отправили AT для настройки скорости обмена данными 
   
   // Команды настройки модема при каждом запуске 
   //_response = sendATCommand("AT+CLIP=1", true);    // Включаем АОН 
   //_response = sendATCommand("AT+DDET=1", true);    // Включаем DTMF 
   _response = sendATCommand("AT+CMGF=1;&W", true);   // Включаем текстовый режима SMS (Text mode) и сразу сохраняем значение (AT&W)! 

   sysStatus = DISARM;                                //преводим систему в режим ожидания
}

void loop() {
  getDHTValue();

  if (SIM800.available()) {                            // Если модем, что-то отправил нам в порт... 
    _response = waitResponse();                        // Получаем ответ от модема для анализа 
    _response.trim();                                  // Убираем лишние пробелы в начале и конце 
    Serial.println(_response);                         // Если нужно выводим в монитор порта 
    //.... 


    //ОБРАБОТКА ВХОДЯЩЕГО SMS
    if (_response.startsWith("+CMTI:")) {              // Пришло сообщение об получении SMS 
      int index = _response.lastIndexOf(",");          // Находим последнюю запятую, перед индексом 
      String result = _response.substring(index + 1, _response.length()); // Получаем индекс 
      result.trim();                                   // Убираем пробельные символы в начале/конце 
      _response=sendATCommand("AT+CMGR="+result, true);// Получить содержимое SMS 
      parseSMS(_response);                             // Распарсить SMS на элементы 
      sendATCommand("AT+CMGDA=\"DEL ALL\"", true);     // Удалить все сообщения, чтобы не забивали память модуля 


       String whiteListPhones = "+380954736368, +380673711661"; // Белый список телефонов

       // Проверяем, чтобы длина номера была больше 6 цифр, и номер должен быть в списке
      if (msgphone.length() >= 7 && whiteListPhones.indexOf(msgphone) >= 0) {

        msgbody.toUpperCase();
        
        if(msgbody == "GHT"){
          sendSMS(msgphone, String("t=" + String(temph[0]) + "; h=" + String(temph[1]))); // Если да, то отвечаем на запрос
        }
        else if (msgbody == "ARM"){
          arm();
        }
        else if (msgbody == "DARM"){
          disArm();
        }
        else if (msgbody == "GBAL"){
          balphone = msgphone;                                                   // сохраняем номер телефона абонента запросившего баланс, для последующего ответа на него
          sendATCommand("AT+CUSD=1,\"USSDBALCMD\"", true);
        }
      }
      else {
        // Если нет, не делаем ничего
      }
    } 

    //ОБРАБОТКА ВХОДЯЩЕГО USSD (предполагается, что сообщение содержит текущий баланс на симке)
    if (_response.startsWith("+CUSD:")) {                                         // Пришло уведомление о USSD-ответе 
      if (_response.indexOf("\"") > -1) {                                         // Если ответ содержит кавычки, значит есть сообщение (предохранитель от "пустых" USSD-ответов) 
        String msgBalance = _response.substring(_response.indexOf("\"") + 2);     // Получаем непосредственно текст 
        msgBalance = msgBalance.substring(0, msgBalance.indexOf("\"")); 
        Serial.println("USSD: " + msgBalance);                                    // Выводим полученный ответ 
        sendSMS(balphone, "BAL: " + String( getFloatFromString(msgBalance) ) );   // Отправляем ответ на ранее зафиксированный номер
        balphone = "";                                                            // Очищаем ранее зафиксированный номер телефона
      }
    }
  } 

  //РЕАКЦИЯ СИСТЕМЫ НА ИЗМЕНЕНИЕ СОСТОЯНИЯ ДАТЧИКОВ В РЕЖИМЕ ОХРАНЫ sysStatus == ARM
  ird_value = digitalRead(IRDPIN); 
  if (HIGH == ird_value && sysStatus == ARM) {        // Если сработал датчик движения - отсылаем SMS с извещением
    alarm();
  } else {
   //ничего не делаем 
  }

  gkn_value = digitalRead(GKNPIN); 
  if (LOW == gkn_value && sysStatus == ARM) {         // Если сработал геркон - отсылаем SMS с извещением
    alarm();
  } else {
   //ничего не делаем 
  }
  
  if (Serial.available()) {                            // Ожидаем команды по Serial... 
    SIM800.write(Serial.read());                       // ...и отправляем полученную команду модему 
  };
}


void disArm(){                                          //преводим систему в режим ожидания
  sysStatus = DISARM;
  sendSMS(msgphone, "ARM OFF");
  // выключаем реле
  // выключаем динамик
}

void arm(){                                             //преводим систему в режим охраны
  sysStatus = ARM;
  sendSMS(msgphone, "ARM ON");
}

void preAlarm(){
  
}

void alarm(){
    sysStatus = ALARM;                                   //преводим систему в тревоги
    Serial.println(ALARMSG);
    sendSMS("+380673711661", ALARMSG); 
}

void getDHTValue()                                       //функция считывания показаний датчика DHT22 каждые 1000 мсек.
{
    cli();
    tempTimerOn = 1;
    tempClock = tempTimer;
    sei();
    
    if(tempClock >= 1000)
    {
        cli();
        tempTimerOn = 1;
        tempTimer = 0;
        tempClock = 0;
        sei();    
        float t = dht.readTemperature();
        float h = dht.readHumidity(); 
        temph[0] = float(t);
        temph[1] = float(h);
    }
}

String sendATCommand(String cmd, bool waiting) 
{ 
  String _resp = "";                                    // Переменная для хранения результата 
  Serial.println(cmd);                                  // Дублируем команду в монитор порта 
  SIM800.println(cmd);                                  // Отправляем команду модулю 
  if (waiting) {                                        // Если необходимо дождаться ответа... 
    _resp = waitResponse();                             // ... ждем, когда будет передан ответ 
    // Если Echo Mode выключен (ATE0), то эти 3 строки можно закомментировать 
    if (_resp.startsWith(cmd)) {                        // Убираем из ответа дублирующуюся команду 
      _resp = _resp.substring(_resp.indexOf("\r", cmd.length()) + 2); 
    } 
    Serial.println(_resp);                              // Дублируем ответ в монитор порта 
  } return _resp;                                       // Возвращаем результат. Пусто, если проблема 
} 

String waitResponse() {                                  // Функция ожидания ответа и возврата полученного результата 
  String _resp = "";                                     // Переменная для хранения результата 
  long _timeout = millis() + 10000;                      // Переменная для отслеживания таймаута (10 секунд) 
  
  while (!SIM800.available() && millis() < _timeout) {}; // Ждем ответа 10 секунд, если пришел ответ или наступил таймаут, то... 
  
  if (SIM800.available()) {                              // Если есть, что считывать... 
    _resp = SIM800.readString();                         // ... считываем и запоминаем 
  } 
  else {                                                 // Если пришел таймаут, то... 
    Serial.println("Timeout...");                        // ... оповещаем об этом и... 
  } 
  return _resp;                                          // ... возвращаем результат. Пусто, если проблема 
} 

void parseSMS(String msg) {
  String msgheader = ""; 
  
  msg = msg.substring(msg.indexOf("+CMGR: ")); 
  msgheader = msg.substring(0, msg.indexOf("\r")); 
  
  msgbody = msg.substring(msgheader.length() + 2); 
  msgbody = msgbody.substring(0, msgbody.lastIndexOf("OK")); 
  msgbody.trim(); 
  
  int firstIndex = msgheader.indexOf("\",\"") + 3; 
  int secondIndex = msgheader.indexOf("\",\"", firstIndex);
  msgphone = msgheader.substring(firstIndex, secondIndex); 
  
  Serial.println("Phone: "+msgphone); 
  Serial.println("Message: "+msgbody); 
  
  // Далее пишем логику обработки SMS-команд. 
  // Здесь также можно реализовывать проверку по номеру телефона 
  // И если номер некорректный, то просто удалить сообщение. }
}

void sendSMS(String phone, String message) { 
  sendATCommand("AT+CMGS=\"" + phone + "\"", true);                 // Переходим в режим ввода текстового сообщения 
  sendATCommand(message + "\r\n" + (String)((char)26), true);       // После текста отправляем перенос строки и Ctrl+Z 
}

float getFloatFromString(String str) {                              // Функция извлечения цифр из сообщения - для парсинга баланса из USSD-запроса 
    bool flag = false; 
    String result = ""; 
    
    str.replace(",", ".");                                          // Если в качестве разделителя десятичных используется запятая - меняем её на точку. 
    
    for (int i = 0; i < str.length(); i++) { 
      if (isDigit(str[i]) || (str[i] == (char)46 && flag)) {        // Если начинается группа цифр (при этом, на точку без цифр не обращаем внимания), 
        if (result == "" && i > 0 && (String)str[i - 1] == "-") {   // Нельзя забывать, что баланс может быть отрицательным 
          result += "-";                                            // Добавляем знак в начале 
        } 
        
        result += str[i];                                           // начинаем собирать их вместе 
        
        if (!flag) flag = true;                                     // Выставляем флаг, который указывает на то, что сборка числа началась. 
        } else {                                                    // Если цифры закончились и флаг говорит о том, что сборка уже была, 
          if (str[i] != (char)32) {                                 // Если порядок числа отделен пробелом - игнорируем его, иначе... 
            if (flag) break;                                        // ...считаем, что все. 
          }
        }
    }

    return result.toFloat();                                        // Возвращаем полученное число. 
}

void sheduleLight (){
    cli();
    shedTimerOn = 1;
    shedClock = shedTimer;
    sei();
    
    if(shedClock >= 1000)
    {
        cli();
        tempTimerOn = 1;
        tempTimer = 0;
        tempClock = 0;
        sei();
        
        DateTime now = rtc.now();
        if (now.hour() >= LIGHTSHEDON && !lightOn){
          lightOn = true; // включаем освещение
          digitalWrite(RELE1PIN, HIGH);
        }

        if (now.hour() <= LIGHTSHEDOFF && lightOn){
          lightOn = false; // выключаем освещение
          digitalWrite(RELE1PIN, LOW);
        }
    }
}

