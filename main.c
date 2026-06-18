/*
Projeto: Simulação de Máquina de Lavar utilizando ESP32 e FreeRTOS

Descrição:
Sistema que simula o funcionamento de uma máquina de 
lavar, com máquina de estados para controlar das etapas:
lavagem, drenagem, centrifugação, enxágue e secagem.

Recursos utilizados:
- ESP32
- FreeRTOS
- GPIOs para LEDs indicadores
- Motores de passo controlados por ponte H
- Entrada de comandos via monitor serial

BY: Yasmin Gouvêa
Data: Junho/2026
*/
#include <stdio.h>      // Biblioteca padrão de entrada e saída (printf e getchar).
#include <ctype.h>      // Manipulação de caracteres (toupper).
#include <stdbool.h>    // Permite utilizar o tipo booleano (true e false).
#include "freertos/FreeRTOS.h" // Núcleo do sistema operacional FreeRTOS.
#include "freertos/task.h"     // Criação e gerenciamento de tarefas.
#include "driver/gpio.h"       // Configuração e controle dos GPIOs do ESP32.
#include "esp_rom_sys.h"       // Funções do sistema ROM do ESP32 (esp_rom_delay_us).

volatile bool agua    = false; // Indica a presença de água no sistema.
volatile bool porta   = false; // Simula o sensor de porta fechada. 
volatile bool stope   = false; // Indica se a parada de emergência foi acionada.
volatile bool reseta  = false; // Solicita a reinicialização completa do sistema.

int tempo_lavagem = 0;

//função que ajuda a não repetir mensagens na tela.
static bool aviso_modo    = false;
static bool aviso_porta   = false;
static bool aviso_liga    = false;
static bool aviso_agua    = false;
static bool aviso_agua2   = false;
static bool aviso_inicio  = false; 

#define LAVAR 21
#define CENTRI 20
#define ENXAGUE 23
#define STOP 14

// LEDs indicadores do sistema
#define PRONTO 2     // Máquina pronta para iniciar
#define WATER 4      // Indicação de presença de água
#define ALERTA 22    // Avisos ao usuário
#define PROBLE 19    // Indica falha ou parada de emergência

//definição ponte H 
// MOTOR 1 DIREÇÕES 
#define STEP1 GPIO_NUM_5
#define DIR1  GPIO_NUM_18
// MOTOR 2
#define STEP2 GPIO_NUM_17
#define DIR2  GPIO_NUM_16

//delays e tempos
#define STEP_DELAY_LENTO 900    // lavagem
#define STEP_DELAY_RAPIDO 200   // centrifugação e enxágue

#define STAR     2000   //quanto tempo apertar botão inicio
#define DEVAGAR   500   //quanto tempo entre cada grupo de código
#define FECHA    2000   //quanto tempo fechar porta
#define LIGA     2000   //quanto tempo ligar motor

// Tempos das etapas do ciclo (ms)
#define LL   12000   // Lavagem lenta
#define LR    8000   // Lavagem rápida
#define CTG  10000   // Centrifugação
#define EXG  10000   // Enxágue
#define CTGS 12000   // Secagem


//Máquina de estados da lavadora.
typedef enum {
  DESLIGADA,                 // Sistema desligado
  AGUARDANDO_PORTA,          // Espera o fechamento da porta
  AGUARDANDO_MODO,           // Espera seleção do modo de lavagem
  AGUARDANDO_AGUA_LAVAGEM,   // Espera disponibilidade de água
  LAVANDO,                   // Executa a lavagem
  DRENANDO_1,                // Remove água após lavagem
  CENTRIFUGANDO,             // Primeira centrifugação
  AGUARDANDO_AGUA_ENXAGUE,   // Espera água para enxágue
  ENXAGUANDO,                // Executa o enxágue
  DRENANDO_2,                // Remove água após enxágue
  SECANDO,                   // Secagem final
  FINALIZADA                 // Ciclo concluído
} estado_t;
estado_t estado = DESLIGADA; 

//Modos disponíveis de operação.
typedef enum {
  NENHUM,    // Nenhum modo 
  RAPIDO,    // Ciclo de pouca duração
  LENTO      // Ciclo de longa duração
} modo_t;
modo_t modo = NENHUM;

void desligar_sistema()
{
  agua  = false;
  porta = false;

  gpio_set_level(WATER,  false);
  gpio_set_level(ALERTA, false);
  gpio_set_level(PRONTO, false);
}


/*
Comandos disponíveis:
L - Liga a máquina
P - Abre/fecha a porta
A - Simula presença de água
S - Parada de emergência
R - Reinicializa o sistema
1 - Seleciona lavagem lenta
2 - Seleciona lavagem rápida
*/
void tarefa_comandos(void *pvParameters)
{
  while (true)
  {
    int tecla = getchar();

    if (tecla != EOF)
    {

    tecla = toupper(tecla);

    switch (tecla)
    {
      case 'L':
          if (stope)
          {
            printf("Pressione 'R' para reiniciar o sistema.\n");
          }

          else if (estado == DESLIGADA)
          {
            stope = false;
            estado = AGUARDANDO_PORTA;
            printf("Ligando\n");
          }
            break;

      case 'P':

          if (estado == DESLIGADA     ||
          estado == AGUARDANDO_PORTA  ||
          estado == AGUARDANDO_MODO   ||
          estado == FINALIZADA)
          {
            porta = !porta;
            printf("Porta %s\n", porta ? "fechada" : "aberta");
            if (porta == true && estado == AGUARDANDO_PORTA)
            {
              estado = AGUARDANDO_MODO;
            }
            else if (porta == false && estado == AGUARDANDO_MODO)
            {
              estado = AGUARDANDO_PORTA;
              printf("\nFeche a porta para continuar.\n");
            }
            }
              else
            {
              printf("ERRO: não é possível abrir a porta durante o ciclo!\n");
            }
            break;

      case 'A' :
          agua = !agua;
          printf("água %s\n", agua ? "presente" : "ausente");
          if (agua == true) 
          gpio_set_level(WATER,true);
                
          else 
          gpio_set_level(WATER,false);
          break;
                
              
      case 'S':
          estado = DESLIGADA;
          modo = NENHUM;
          printf("parada de emergência\n");
          vTaskDelay(200 / portTICK_PERIOD_MS);
          stope = true;
                
          desligar_sistema();

          gpio_set_level(PROBLE, true);
          break;

      case 'R':
          if (stope)
          {
            reseta = true;
            printf("reseta sistema\n");
            gpio_set_level(PROBLE, false);
          }
          else
          {
            vTaskDelay(pdMS_TO_TICKS(500));
          }
            break;
            
      case '1':
          if (estado == AGUARDANDO_MODO)
          {
            modo = LENTO;
            estado = AGUARDANDO_AGUA_LAVAGEM;
            printf("lavagem lenta\n");
          }
          break;

      case '2':
          if (estado == AGUARDANDO_MODO)
          {
            modo = RAPIDO;
            estado = AGUARDANDO_AGUA_LAVAGEM;
            printf("lavagem rápida\n");
          }
          break;
    }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}


//Controla o movimento dos motores de passo.
void motor_step(gpio_num_t stepPin, //Pino STEP do driver.
                gpio_num_t dirPin,  //Pino DIR do driver.
                int direction,      //Sentido de rotação.
                int steps,          //Quantidade de passos.
                int delay_us)       //Velocidade do motor.
  {
    gpio_set_level(dirPin, direction);

    for (int i = 0; i < steps; i++)
    {
      if (estado == DESLIGADA || stope) //se estiver desligada, ou em estado de stop não funciona
      break;

      gpio_set_level(stepPin, 1);   
      esp_rom_delay_us(delay_us); 

      gpio_set_level(stepPin, 0);
      esp_rom_delay_us(delay_us);

      if ((i % 50) == 0)
      {
        vTaskDelay(1); //evitar whatdog
      }
    }
  }


void app_main(void)
{ 
  {
    // configura GPIOs
    gpio_set_direction(STEP1,   GPIO_MODE_OUTPUT);
    gpio_set_direction(DIR1,    GPIO_MODE_OUTPUT);   //direção e quantidade de passos

    gpio_set_direction(STEP2,   GPIO_MODE_OUTPUT);
    gpio_set_direction(DIR2,    GPIO_MODE_OUTPUT);   //direção e quantidade de passos
    
    gpio_set_direction(PRONTO,  GPIO_MODE_OUTPUT);   //led motor pronto
    gpio_set_direction(WATER,   GPIO_MODE_OUTPUT);   //led tem agua 
    gpio_set_direction(ALERTA,  GPIO_MODE_OUTPUT);   //led atenção
    gpio_set_direction(PROBLE,  GPIO_MODE_OUTPUT);   //led problema 

    gpio_set_direction    (LAVAR, GPIO_MODE_INPUT);
    gpio_pullup_en(LAVAR);

    gpio_set_direction   (CENTRI, GPIO_MODE_INPUT);
    gpio_pullup_en(CENTRI);

    gpio_set_direction  (ENXAGUE, GPIO_MODE_INPUT);
    gpio_pullup_en(ENXAGUE);

    xTaskCreate(
    tarefa_comandos,
    "comandos",
    2048,
    NULL,
    1,
    NULL
    );
  }

  

while (true)
  {

    if (reseta == true)
    {
      estado =  DESLIGADA;
      modo = NENHUM;

      aviso_liga    = false;
      aviso_porta   = false;
      aviso_modo    = false;
      aviso_agua    = false;
      aviso_inicio  = false;
      aviso_agua2   = false;

      stope = false;
      tempo_lavagem = 0;

      desligar_sistema();

      gpio_set_level(PROBLE, false); 

      printf("\nSistema reiniciado\n");

      reseta = false;
    } 

vTaskDelay(DEVAGAR / portTICK_PERIOD_MS); //delay desligado
  
  // ---------------- DESLIGADA ----------------
  // Aguarda o usuário ligar a máquina ou reiniciar após emergência.
    if (estado == DESLIGADA)
    {
      if (stope)
      {
        if (!aviso_liga)
        {
          printf("\nSistema parado por emergência.\n");
          printf("Pressione 'R' para reiniciar.\n");

          gpio_set_level(PRONTO, false);
          gpio_set_level(ALERTA, true);

          aviso_liga = true;
        }
      }
      else
      {
        if (!aviso_liga)
        {
          printf("\nPressione 'L' para iniciar a máquina de lavar\n");

          gpio_set_level(PRONTO, true);
          gpio_set_level(ALERTA, false);

          aviso_liga = true;
        }
      }
    }
    else
    {
      aviso_liga = false;
      gpio_set_level(PRONTO, false);
    }


vTaskDelay(DEVAGAR / portTICK_PERIOD_MS); //delay porta

  // ---------------- AGUARDANDO_PORTA ---------------- 
  // Impede o início do ciclo enquanto a porta estiver aberta.
    if (estado == AGUARDANDO_PORTA)
    {
      if ((!aviso_porta) && porta == false)
      {
        printf("\nFeche a porta pressionando 'P'\n");
        gpio_set_level(ALERTA, true);
        aviso_porta = true;
      }
    }
    else
    {
      gpio_set_level(ALERTA, false);
      aviso_porta = false;
    }


vTaskDelay(DEVAGAR / portTICK_PERIOD_MS); //delay modo

  // ---------------- AGUARDANDO_MODO ----------------
  // Permite ao usuário escolher entre lavagem rápida ou lenta.
    if (estado == AGUARDANDO_MODO)
    {
      if (!aviso_modo)
      {
        printf("\nVocê quer uma lavagem rápida ou demorada?\n");
        printf("Aperte '1' para lenta e '2' para rápida.\n");
        aviso_modo = true;
      }
    }
    else
    {
      aviso_modo = false;
    }


vTaskDelay(DEVAGAR / portTICK_PERIOD_MS); //delay inicio

  // ---------------- início ----------------
    if (estado == AGUARDANDO_AGUA_LAVAGEM)
    {
      if (!aviso_inicio)
      {
        printf("\n=== Iniciando Ciclo ===\n");
        aviso_inicio = true;
      }
    }
    else
    {
      aviso_inicio = false;
    }

vTaskDelay(DEVAGAR / portTICK_PERIOD_MS); //delay agua1

  // ---------------- AGUARDANDO_AGUA_LAVAGEM ----------------
  // Espera a presença de água para iniciar o ciclo.
    if (estado == AGUARDANDO_AGUA_LAVAGEM)
    {
      if (!aviso_agua)
      {
        gpio_set_level(ALERTA, true);
        printf("\nPor favor abra a torneira apertando 'A'\n");
        aviso_agua = true;
      }

      while (agua == false)
      {
        if (stope)
        {
          printf("\nOperação cancelada\n");
          estado = DESLIGADA;
          break;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
      }

      if (agua == true)
      {
        gpio_set_level(ALERTA, false);
        aviso_agua = false;
        estado = LAVANDO;   // próxima etapa da máquina de estados
      }

      if (stope)
      {
        continue;
      }
    }
  
vTaskDelay(DEVAGAR / portTICK_PERIOD_MS); //delay lavagem

  // ---------------- LAVANDO ----------------
  // Executa movimentos alternados dos motores durante o tempo
  // definido pelo modo selecionado.
    if (estado == LAVANDO)
    {
      TickType_t inicio;

      // Define o tempo
      if (modo == RAPIDO)
      {
        printf("\nLavagem rápida\n");
        tempo_lavagem = LR;
      }
      else if (modo == LENTO)
      {
        printf("\nLavagem lenta\n");
        tempo_lavagem = LL;
      }

      // Executa a lavagem
      inicio = xTaskGetTickCount();

      while ((xTaskGetTickCount() - inicio) < pdMS_TO_TICKS(tempo_lavagem))
      {
        if (stope == true)
        {
          estado = DESLIGADA;
          printf("\nLavagem interrompida\n");
          break;
        }

        // Horário
        motor_step(STEP1, DIR1, 1, 200, STEP_DELAY_LENTO);
        motor_step(STEP2, DIR2, 0, 200, STEP_DELAY_LENTO);
        vTaskDelay(pdMS_TO_TICKS(900));
        // Anti-horário
        motor_step(STEP1, DIR1, 0, 200, STEP_DELAY_LENTO);
        motor_step(STEP2, DIR2, 1, 200, STEP_DELAY_LENTO);
        vTaskDelay(pdMS_TO_TICKS(900));
      }
      if (estado != DESLIGADA)
      {
        estado = DRENANDO_1;
      }
    } 
vTaskDelay(DEVAGAR / portTICK_PERIOD_MS); //delay drenando

  // ---------------- DRENANDO_1 ----------------
  // Simula a retirada da água após a lavagem.
    if (estado == DRENANDO_1)
    {
      printf("\nDrenando água...\n");

      agua = false;
      gpio_set_level(WATER, false);

      estado = CENTRIFUGANDO;
    }

  
vTaskDelay(DEVAGAR / portTICK_PERIOD_MS); //delay centrifugando
  
  // ---------------- CENTRIFUGANDO ----------------
  // Gira os motores em alta velocidade para remover excesso de água.
    if (estado == CENTRIFUGANDO)
    {
      printf("\nCentrifugação\n");

      TickType_t inicio = xTaskGetTickCount();

      while ((xTaskGetTickCount() - inicio) < pdMS_TO_TICKS(CTG))
      {
        if (stope)
        {
          estado = DESLIGADA;
          break;
        }

        motor_step(STEP1, DIR1, 1, 200, STEP_DELAY_RAPIDO);
        motor_step(STEP2, DIR2, 0, 200, STEP_DELAY_RAPIDO);

        vTaskDelay(pdMS_TO_TICKS(100));
      }

      if (estado != DESLIGADA)
      {
        estado = AGUARDANDO_AGUA_ENXAGUE;
      }
    }

vTaskDelay(DEVAGAR / portTICK_PERIOD_MS); //delay agua2

  // ---------------- AGUARDANDO_AGUA_ENXAGUE ----------------
  // Aguarda novamente a presença de água para iniciar o enxágue.
    if (estado == AGUARDANDO_AGUA_ENXAGUE)
    {
      printf("\nDrenando\n");
      if (!aviso_agua2)
      {
        gpio_set_level(ALERTA, true);
        printf("\nPor favor abra a torneira apertando 'A'\n");
        aviso_agua2 = true;
      }

      while (agua == false)
      {
        if (stope)
        {
          estado = DESLIGADA;
          break;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
      }

      if (agua == true)
      {
        gpio_set_level(ALERTA, false);
        estado = ENXAGUANDO;
        aviso_agua2 = false;
      }
    }

vTaskDelay(DEVAGAR / portTICK_PERIOD_MS); //delay enxague

  // ---------------- ENXAGUANDO ----------------
  // Realiza o enxágue da roupa.
    if (estado == ENXAGUANDO)
    {
      printf("\nEnxaguando\n");
      TickType_t inicio = xTaskGetTickCount();

      while ((xTaskGetTickCount() - inicio) < pdMS_TO_TICKS(EXG))
      {
        if (stope)
        {
          estado = DESLIGADA;
          break;
        }

        motor_step(STEP1, DIR1, 0, 200, STEP_DELAY_RAPIDO);
        motor_step(STEP2, DIR2, 1, 200, STEP_DELAY_RAPIDO);

        vTaskDelay(pdMS_TO_TICKS(100));
      }

      if (estado != DESLIGADA)
      {
        estado = DRENANDO_2;
      }
    }

vTaskDelay(DEVAGAR / portTICK_PERIOD_MS); //delay drenando2

  // ---------------- DRENANDO_2 ----------------
  // Esvazia a água após o enxágue.
    if (estado == DRENANDO_2)
    {
      printf("\nDrenando\n");
      agua = false;
      gpio_set_level(WATER, false);

      estado = SECANDO;
    }

vTaskDelay(DEVAGAR / portTICK_PERIOD_MS); //delay secagem

  // ---------------- SECANDO ----------------
  // Realiza a secagem através da rotação dos motores.
    if (estado == SECANDO)
    {
      printf("\nSecando\n");
      TickType_t inicio = xTaskGetTickCount();

      while ((xTaskGetTickCount() - inicio) < pdMS_TO_TICKS(CTGS))
      {
        if (stope)
        {
          estado = DESLIGADA;
          break;
        }

        motor_step(STEP1, DIR1, 1, 200, STEP_DELAY_RAPIDO);
        motor_step(STEP2, DIR2, 0, 200, STEP_DELAY_RAPIDO);

        vTaskDelay(pdMS_TO_TICKS(100));
      }

      if (estado != DESLIGADA)
      {
        estado = FINALIZADA;
      }
    }

vTaskDelay(DEVAGAR / portTICK_PERIOD_MS); //delay finaliza
    
  // ---------------- FINALIZADA ----------------
  // Indica o término do ciclo, desliga o sistema e retorna
  // ao estado inicial.
    if (estado == FINALIZADA)
    {
      printf("\nCiclo concluído\n");

      tempo_lavagem = 0;
      desligar_sistema();

      modo = NENHUM;
      estado = DESLIGADA;

      gpio_set_level(PROBLE, 0);
    }
  }
}