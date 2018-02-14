/************************************************************************************************************************************
Программный код для охранной сигнализации (системы оповещения)

Автор: Игорь Ткачук
*************************************************************************************************************************************/

#include <SoftwareSerial.h>                     // Библиотека програмной реализации обмена по UART-протоколу
SoftwareSerial SIM800(8, 9);                    // RX, TX

#include <DHT.h>                                //библиотека для работы датчика температуры и влажности
#include <DHT_U.h>
#define DHTPIN 3                                //датчик подключен ко входу 3
#define DHTTYPE DHT22 

float temph[2];                                 //массив для температуры и влажности
volatile unsigned long int tempTimer = 0;       //переменная для таймера обновления показаний температуры и влажности
volatile unsigned long int tempClock = 0;       //переменная для сохранения значения таймера
volatile boolean tempTimerOn = 0;               //переменная для запуска/остановки таймера

String msgbody = ""; 
String msgphone = ""; 

DHT dht(DHTPIN, DHTTYPE);                       //настройка датчика температуры и влажности

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
  
}

void loop() {
  getDHTValue();

  if (SIM800.available()) {                            // Если модем, что-то отправил нам в порт... 
    _response = waitResponse();                        // Получаем ответ от модема для анализа 
    _response.trim();                                  // Убираем лишние пробелы в начале и конце 
    Serial.println(_response);                         // Если нужно выводим в монитор порта 
    //.... 
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
        sendSMS(msgphone, String("t=" + String(temph[0]) + "; h=" + String(temph[1]))); // Если да, то отвечаем на запрос
      }
      else {
                                                               // Если нет, не делаем ничего
      }
    } 
  } 
  
  if (Serial.available()) {                            // Ожидаем команды по Serial... 
    SIM800.write(Serial.read());                       // ...и отправляем полученную команду модему 
  };
}


void disArm(){
  
}

void arm(){
  
}

void preAlarm(){
  
}

void alarm(){
  
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
  sendATCommand("AT+CMGS=\"" + phone + "\"", true);           // Переходим в режим ввода текстового сообщения 
  sendATCommand(message + "\r\n" + (String)((char)26), true); // После текста отправляем перенос строки и Ctrl+Z 
}

