/************************************************************************************************************************************
Программный код для охранной сигнализации (системы оповещения)

Автор: Игорь Ткачук
*************************************************************************************************************************************/
#include <DHT.h>                                //библиотека для работы датчика температуры и влажности
#include <DHT_U.h>
#define DHTPIN 3                                //датчик подключен ко входу 3
#define DHTTYPE DHT22 

float temph[2];                                 //массив для температуры и влажности
volatile unsigned long int tempTimer = 0;       //переменная для таймера обновления показаний температуры и влажности
volatile unsigned long int tempClock = 0;       //переменная для сохранения значения таймера
volatile boolean tempTimerOn = 0;               //переменная для запуска/остановки таймера

DHT dht(DHTPIN, DHTTYPE);                       //настройка датчика температуры и влажности

ISR (TIMER0_COMPA_vect)      //функция, вызываемая таймером-счетчиком каждые 0,001 сек
{
  if(tempTimerOn == 1)            //если таймер включен
  {
      tempTimer++;                //увеличение значения таймера на +1 каждые 0,001 сек
  }
}



void setup() {

  dht.begin();                          //инициализация датчика температуры
  
  //Настройка таймера на срабатывание каждые 0,001 сек
  TCCR0A |= (1 << WGM01);
  OCR0A = 0xF9;                         //начало отсчета до переполнения (255)
  TIMSK0 |= (1 << OCIE0A);              //Set the ISR COMPA vect
  sei();                                //разрешить прерывания
  TCCR0B |= (1 << CS01) | (1 << CS00);  //установить делитель частоты на 64
  //теперь каждые 0,001 сек будет вызываться функция ISR (TIMER0_COMPA_vect)

}

void loop() {
  getDHTValue();

}

void disArm(){
  
}

void arm(){
  
}

void preAlarm(){
  
}

void alarm(){
  
}

void getDHTValue()        //функция считывания показаний датчика DHT22 каждые 1000 мсек.
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
