// ESP32 HIGH ACCURACY FREQUENCY METER version 0.33.0

/* DEVELOPER : Rui Viana
   CONTRIBUITORS: Gustavo Murta / Celso Ito
   DATE: 17/jul/2020
   
   ESP32 Dev Kit - ESP-IDF V4.0.1 or ARDUINO IDE 1.8.12
   This project can be compiled with the arduino IDE or the IDF! (change the extension to .INO to compile Arduino)
   
   Optional = Parallel LCD or I2C LCD (see pins connections in the program)
   
   Comments, doubts, suggestions = jgustavoam@gmail.com 

   The project:
   A high accuracy frequency meter using ESP32, without scales and showing up to 8 digits,
   measuring up to 20 MHz or more. Very stable. Optionally, you can test the frequency meter with internal oscillator.    
   Caution = input signal to Freq Meter - only 3.3 Volts! If you want 5 Volts, use level converter. 

  Definitions: 

  GPIO_34 =  Freq Meter Input
  GPIO_25 =  Oscillator output - to test Frequency Meter 
  To test freq Meter with internal oscillator, make connection between GIPO_34 and GPIO_25 (optional).

  GPIO_35 = Pulse Count control input - HIGH =count up, LOW=count down
  GPIO_32 = High Precision Timer output (to control Pulse Counter)   
  Make connection between GPIO_35 to GPIO_32 to use Frequency Meter (must have). 
  
  If you need, can change GPIOs pins
    
  The frequency meter is divided into 5 parts:
     1. Pulse counter;
     2. Counting time control;
     3. Printing the result;
     4. Space for other functions.
     5. Signal generator programmed for 2 Hz (or 50,000)

 1. The pulse counter uses the ESP32 pcnt.
      Pcnt has the following parameters:
       a. input port;
       b. input channel;
       c. control port;
       d. count on Pulse raising;
       e. count on Pulse falling;
       f. counting only with Control - high level;
       g. maximum count limit.
       
  2. Counting time control uses the high precision esp-timer:
      The esp-timer has the following parameter:
       a. time control;

   5. Frequency generator for tests, uses ledc peripheral:
      The ledc has the following parameters:
       a. output port;
       B. lcd channel;
       ç. frequency;
       d. resolution of ledc;
       e. duty cycle at 50%;
       
  Operation:
  
  The high level of control port enable the counter to count the pulses that arrive at the input port.
  Pulses are counted both as the pulse rising and falling, to improve the counting average.
  The sampling time is defined by the high precision esp-timer, and it is defined in 1 second, in the window variable.
  If the count is greater than 20000 pulses during the counting time, overflow occurs and with each overflow that occurs
  is counted in the multPulses variable and then pulse counter is cleared and proceed to counting.
  Unfortunately the Pulse Counter has only 16 bits that may be used. 

  When the sampling time ends, a routine is called and the value in the pulse counter is read and saved.
  A flag is set on to indicating that the pulse reading has ended.

  In the loop, when verifying if the flag is on indicates that the pulse reading has finished, the value is calculated by multiplying
  the number of overflow by 20000 and adding to the number of remaining pulses and dividing by 2, because it counted 2 times.
  
  As the pulses are counted on the way up and down, the count is double the frequency.
  In the frequency value, points are inserted and printed on the serial monitor.
  The registers are reset and the input control port is raised to a high level again and the pulse count starts.

  It also has a signal generator that generates 50,000 Hz, and can be used for testing.
  This generator can be changed to generate frequencies up to 40 MHz. No, the frequency Meter cannot read this...
  We use the led32 feature of ESP32 to generate frequency that can be used as a test.
  The base frequency value is 2 (or 50,000) Hz, but it can be typed or another value on the serial monitor
  The deafault duty cycle was set at 50%, but the resolution is properly calculated.
  The output port of this generator is currently defined as GPIO 25.

  Internally using GPIO matrix, the input pulse was directed to the ESP32 native LED,
  so the LED will flash at the input frequency. 
  
   The compiler uses the compilation directives to select:
  
   Using LCD     =   LCD_ON or LCD_OFF
   Using LCD I2C =   LCD_I2C_ON or LCD_I2C_OFF
   
   Calculation of adjustments for each frequency range:
   Resolution = log2(Clock(80 MHz)/f) + 1    ex: 50,000 Hz = 80,0000/50,000 = 1,600 log2(1600) = 10 + 1 = 11
   Duty cycle 50%  = (2**Resolution)/2       ex: 2**11 = 2048   2048/2 = 1024

   References: 
 
  https://github.com/espressif/esp-idf/tree/master/examples/peripherals/pcnt
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/pcnt.html
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_timer.html
  ESP332 Oscillator https://github.com/Gustavomurta/ESP32_frequenceMeter/blob/master/ESP32OscilatorV03.ino  
  Answer of Deouss » Thu May 17, 2018 3:07 pm no tópico https://esp32.com/viewtopic.php?t=5734
  Formatting numbers  https://arduino.stackexchange.com/questions/28603/the-most-effective-way-to-format-numbers-on-arduino
*/

#define LCD_OFF                                                           // To use LCD, set LCD_ON 
#define LCD_I2C_OFF                                                       // To use I2C LCD, set LCD_I2C_ON

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/periph_ctrl.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/pcnt.h"
#include "driver/ledc.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "math.h"

#ifdef LCD_ON                                                             // If using LCD 

#ifdef LCD_I2C_ON                                                         // If using I2C LCD 
#include <LiquidCrystal_I2C.h>                                            // LCD I2C Library 
LiquidCrystal_I2C lcd(0x3F, 16, 2);                                       // Define I2C address, columns and rows. Use I2C scanner to identify address
#else
#include <LiquidCrystal.h>                                                // LCD Library 
LiquidCrystal lcd(5, 18, 19, 21, 22, 23);                                 // Define LCD pins at parallel interface
#endif

#endif

#define PCNT_COUNT_UNIT       PCNT_UNIT_0                                 // Set Pulse Count Unit - 0 
#define PCNT_COUNT_CHANNEL    PCNT_CHANNEL_0                              // Set Pulse Count channel - 0 
#define PCNT_INPUT_SIG_IO     GPIO_NUM_34                                 // Set Pulse Count input - Freq Meter Input GPIO 34
#define LEDC_HS_CH0_GPIO      GPIO_NUM_25                                 // Set LEDC HS_CH0 pin - Oscillator output GIPO 25 
#define PCNT_INPUT_CTRL_IO    GPIO_NUM_35                                 // Set Pulse Count Control GPIO pin - HIGH = count up, LOW = count down 
#define OUTPUT_CONTROL_GPIO   GPIO_NUM_32                                 // Saida do timer GPIO 32 Controla a contagem
#define IN_BOARD_LED          (gpio_num_t)2                               // LED nativo ESP32 GPIO 2
#define LEDC_HS_CH0_CHANNEL   LEDC_CHANNEL_0                              // LEDC no canal 0
#define LEDC_HS_MODE          LEDC_HIGH_SPEED_MODE                        // LEDC em high speed
#define LEDC_HS_TIMER         LEDC_TIMER_0                                // Usar timer0 do ledc

uint32_t         overflow  =  20000;                                      // Max Pulse Counter value
#define PCNT_H_LIM_VAL        overflow                                    // High Limit value of Pulse Counter 

esp_timer_create_args_t create_args;                                      // Argumentos do esp-timer
esp_timer_handle_t timer_handle;                                          // Instancia de esp-timer

bool            flag          = true;                                     // Flag to print frequency reading
int16_t         pulses        = 0;                                        // Pulse Counter value 
uint32_t        multPulses    = 0;                                        // Overflows count value 
uint32_t        janela        = 1000000;                                  // Sampling time of one second 
uint32_t        oscilator     = 2;                                        // Oscillator frequency - initial 
uint32_t        mDuty         = 0;                                        // Duty value 
uint32_t        resolucao     = 0;                                        // Resolution value 

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;                     // variavel tipo portMUX_TYPE para sincronismo

//----------------------------------------------------------------------------------------
char *ultos_recursive(unsigned long val, char *s, unsigned radix, int pos)
{
  int c;
  if (val >= radix)
    s = ultos_recursive(val / radix, s, radix, pos + 1);
  c = val % radix;
  c += (c < 10 ? '0' : 'a' - 10);
  *s++ = c;
  if (pos % 3 == 0) *s++ = '.';
  return s;
}

//----------------------------------------------------------------------------------------
char *ltos(long val, char *s, int radix)
{
  if (radix < 2 || radix > 36) {
    s[0] = 0;
  } else {
    char *p = s;
    if (radix == 10 && val < 0) {
      val = -val;
      *p++ = '-';
    }
    p = ultos_recursive(val, p, radix, 0) - 1;
    *p = 0;
  }
  return s;
}

//----------------------------------------------------------------------------
void ledcInit ()                                                          // Optional Pulse Oscillator to test Freq Meter 
{
  resolucao = log((80000000 / oscilator) + 1);                            // Calculo da resolucao para ledc
  //  Serial.println(resolucao);
  mDuty = (pow(2, resolucao)) / 2;                                        // Calculo do duty para ledc
  //  Serial.println(mDuty);

  ledc_timer_config_t ledc_timer = {};                                    // Instancia a configuracao do timer do ledc

  ledc_timer.duty_resolution = (ledc_timer_bit_t) + resolucao;            // Configura resolucao
  ledc_timer.freq_hz    = oscilator;                                      // Frequencia de oscilacao
  ledc_timer.speed_mode = LEDC_HIGH_SPEED_MODE;                           // Mode de operacao em high speed
  ledc_timer.timer_num = LEDC_TIMER_0;                                    // Usar timer0 do ledc
  ledc_timer_config(&ledc_timer);                                         // Configurar o timer do ledc

  ledc_channel_config_t ledc_channel = {};                                // Instancia a configuracao canal do ledc

  ledc_channel.channel    = LEDC_HS_CH0_CHANNEL;                          // Configura canal0
  ledc_channel.duty       = mDuty;                                        // Valor calculado do duty em %
  ledc_channel.gpio_num   = LEDC_HS_CH0_GPIO;                             // Saida no GPIO defino no inicio
  ledc_channel.intr_type  = LEDC_INTR_DISABLE;                            // Desabilita interrupt de ledc
  ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE;                         // Mode de operacao do canal em high speed
  ledc_channel.timer_sel  = LEDC_TIMER_0;                                 // Usar timer0 do ledc

  ledc_channel_config(&ledc_channel);                                     // Configurar o canal do ledc
}

//----------------------------------------------------------------------------------
void tempo_controle(void *p)                                              // Fim de tempo de leitura de pulsos
{
  gpio_set_level(OUTPUT_CONTROL_GPIO, 0);                                 // Controle do PCount - stop count
  pcnt_get_counter_value(PCNT_COUNT_UNIT, &pulses);                       // Obtem o valor contado
  flag = true;                                                            // Informa que ocorreu interrupt de controle
}

//----------------------------------------------------------------------------------
static void IRAM_ATTR pcnt_intr_handler(void *arg)                        // Overflow de contagem de pulsos
{
  portENTER_CRITICAL_ISR(&timerMux);                                      // Desabilita interrupção ?
  multPulses++;                                                           // Incrementa contador de overflow
  PCNT.int_clr.val = BIT(PCNT_COUNT_UNIT);                                // Limpa indicador de interrupt
  portEXIT_CRITICAL_ISR(&timerMux);                                       // Libera novo interrupt
}

//----------------------------------------------------------------------------------
void pcnt_init(void)                                                      // Rotina de inicializacao do pulse count
{
  pcnt_config_t pcnt_config = { };                                        // Instancia pulse config

  pcnt_config.pulse_gpio_num = PCNT_INPUT_SIG_IO;                         // Port de entrada dos pulsos
  pcnt_config.ctrl_gpio_num = PCNT_INPUT_CTRL_IO;                         // Controle da contagem
  pcnt_config.unit = PCNT_COUNT_UNIT;                                     // Unidade de contagem
  pcnt_config.channel = PCNT_COUNT_CHANNEL;                               // Canal de contagem
  pcnt_config.counter_h_lim = PCNT_H_LIM_VAL;                             // Limite maximo de contagem
  pcnt_config.pos_mode = PCNT_COUNT_INC;                                  // Conta na subida do pulso
  pcnt_config.neg_mode = PCNT_COUNT_INC;                                  // Conta na descida do pulso
  pcnt_config.lctrl_mode = PCNT_MODE_DISABLE;                             // Nao usado
  pcnt_config.hctrl_mode = PCNT_MODE_KEEP;                                // Se HIGH conta incrementando
  pcnt_unit_config(&pcnt_config);                                         // Inicializa PCNT

  pcnt_counter_pause(PCNT_COUNT_UNIT);                                    // Inicializa o contador PCNT
  pcnt_counter_clear(PCNT_COUNT_UNIT);                                    // Zera o contador PCNT

  pcnt_event_enable(PCNT_COUNT_UNIT, PCNT_EVT_H_LIM);                     // Limite superior de contagem
  pcnt_isr_register(pcnt_intr_handler, NULL, 0, NULL);                    // Rotina de Interrupt de pcnt
  pcnt_intr_enable(PCNT_COUNT_UNIT);                                      // Habilita interrup de pcnt

  pcnt_counter_resume(PCNT_COUNT_UNIT);                                   // inicia a contagem
}

//----------------------------------------------------------------------------------
void myInit()
{
#ifdef LCD_ON                                                             // Se tem LCD
  lcd.begin(16, 2);                                                       // Inicializa lcd
  lcd.print("Frequencimetro");                                            // Print
#endif

  ledcInit();                                                             // Inicializa o ledc
  pcnt_init();                                                            // Inicializa o pulse count
  gpio_pad_select_gpio(OUTPUT_CONTROL_GPIO);                              // Define o port decontrole
  gpio_set_direction(OUTPUT_CONTROL_GPIO, GPIO_MODE_OUTPUT);              // Define o port de controle como saida

  create_args.callback = tempo_controle;                                  // Instancia o tempo de controle
  esp_timer_create(&create_args, &timer_handle);                          // Cria parametros do timer

  gpio_set_direction(IN_BOARD_LED, GPIO_MODE_OUTPUT);                     // Port LED como saida

  gpio_matrix_in(PCNT_INPUT_SIG_IO, SIG_IN_FUNC226_IDX, false);           // Direciona a entrada de pulsos
  gpio_matrix_out(IN_BOARD_LED, SIG_IN_FUNC226_IDX, false, false);        // Para o LED do ESP32
}

//---------------------------------------------------------------------------------
void app_main(void)
{
#ifndef ARDUINO                                                           // IDF
  myInit();                                                               // IDF
  while (1)                                                               // IDF
  {
#endif
    if (flag == true)                                                     // Se a contagem tiver terminado
    {
      flag = false;                                                       // Impede nova impresao
      float frequencia = 0;                                               // Variavel para calculo de frequencia
      frequencia = (pulses + (multPulses * overflow)) / 2  ;              // Calcula qtos pulsos ocorreram
      char buf[32];                                                       // Buffer para guardar a pontuacao
      printf("frequencia: %s", (ltos(frequencia, buf, 10)));              // Imprime pontuado
      printf(" Hz \n");                                                   // Sufixo
#ifdef LCD_ON                                                             // LCD
      lcd.setCursor(2, 1);                                                // Posiciona cusros na posicao 3 da linha 2
      lcd.print((ltos(frequencia, buf, 10)));                             // Print
      lcd.print(" Hz              ");                                     // Sufixo
#endif
      multPulses = 0;                                                     // Zera contador de overflow
      // Aqui pode rodar qq funcao                                        // Espaco para qualquer função
      vTaskDelay(1);
      // Aqui pode rodar qq funcao                                        // Espaco para qualquer função
      pcnt_counter_clear(PCNT_COUNT_UNIT);                                // Zera o contador PCNT
      esp_timer_start_once(timer_handle, janela);                         // Inicia contador de tempo de 1 segundo
      gpio_set_level(OUTPUT_CONTROL_GPIO, 1);                             // Porta de controle habilita contagem dos pulsos
    }
#ifndef ARDUINO                                                           // IDF
  }                                                                       // IDF
#endif
}

//---------------------------------------------------------------------------------
#ifdef ARDUINO                                                            // if Arduino IDE
void setup()
{
  Serial.begin(115200);                                                   // Init Serial Console Arduino 115200 Bps 
  myInit();                                                               // Initial setup
}

//---------------------------------------------------------------------------------
void loop()
{
  app_main();                                                             // main application
  String inputString = "";                                                // clear temporary string
  oscilator = 0;                                                          // clear oscillator value 
  while (Serial.available())                                              
  {
    char inChar = (char)Serial.read();                                    // Reads a byte on the console
    inputString += inChar;                                                // Add char to string 
    if (inChar == '\n')                                                   // If new line (enter)
    {
      oscilator = inputString.toInt();                                    // Converts String into integer value 
      inputString = "";                                                   // Clear string
    }
  }
  if (oscilator != 0)                                                     // If some value inputted to oscillator 
  {
    ledcInit();                                                           // reconfigure ledc function 
  }
}
#endif
