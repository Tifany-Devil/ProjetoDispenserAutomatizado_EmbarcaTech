Dispenser Automatizado de Remédios

Bem-vindo ao repositório do Dispenser Automatizado de Remédios, um projeto desenvolvido para o Raspberry Pi Pico que utiliza sensores e atuadores para lembrar o usuário de tomar seus remédios em horários programados. Este sistema integra sincronização de horário via NTP, configuração de alarme com joystick, alerta sonoro com buzzer, controle de servo motor para dispensação e monitoramento via sensor ultrassônico e LDR.

- Visão Geral

Este projeto é um protótipo de um dispenser de remédios que dispara um alarme em um horário configurado, toca um buzzer até que o remédio seja retirado e utiliza um servo motor para abrir o compartimento quando o usuário se aproxima. Ele foi desenvolvido em C usando a Pico SDK e testado em um Raspberry Pi Pico com módulo Wi-Fi CYW43.

Funcionalidades

Sincronização de Horário: Conecta-se a uma rede Wi-Fi e sincroniza o horário via NTP (servidor time.google.com).

 Configuração de Alarme: Permite ao usuário definir o horário do alarme com um joystick e botão.

 Alerta Sonoro: Ativa um buzzer no horário do alarme e mantém o som até que o remédio seja tomado.

 Controle de Dispensação: Abre o servo motor a 100° quando a distância é ≤ 40 cm, mantendo-o aberto até o remédio ser retirado (detectado pelo LDR).

 Monitoramento: Exibe distância, posição do servo e estado do LDR em uma saída tabular no monitor serial.



Hardware Necessário

Raspberry Pi Pico com Wi-Fi (CYW43): Microcontrolador principal.

 Joystick: Conectado ao pino ADC GPIO 26 para configuração do alarme.

 Botão: Conectado ao GPIO 5 para interação do usuário.

 Buzzer Passivo: Conectado ao GPIO 21 (PWM) para alertas sonoros a 100 Hz.

 Sensor Ultrassônico HC-SR04: TRIG no GPIO 18, ECHO no GPIO 17 para medir distância.

 Servo SG90: Conectado ao GPIO 16 (PWM) para abrir o compartimento.

 Módulo LDR: Conectado ao GPIO 4 (entrada digital) para detectar a retirada do remédio.

 Cabo USB: Para alimentação e comunicação serial.

 Fonte de Alimentação (opcional): Para uso independente do USB.



Requisitos de Software

Pico SDK: Biblioteca oficial para desenvolvimento no Raspberry Pi Pico.

 CMake e GCC: Ferramentas de compilação (recomenda-se arm-none-eabi-gcc).

 IDE: Visual Studio Code ou qualquer editor com suporte a CMake.

 Monitor Serial: PuTTY, Minicom ou terminal integrado para visualizar a saída.


Configuração Inicial

1. Configuração do Ambiente

Instale o Pico SDK:


Siga o guia oficial do Raspberry Pi (Getting Started with Pico) para instalar o SDK e configurar o ambiente de compilação.

 Configure o CMake:
 Carregue o Firmware:
 Conecte o Pico ao computador via USB enquanto pressiona o botão BOOTSEL.
 Copie o arquivo .uf2 gerado (ex.: dispenser.uf2) para o dispositivo montado.


2. Configuração do Hardware

Conecte os componentes conforme os pinos definidos no código:

 Botão: GPIO 5 (entrada com pull-up).

 Joystick: GPIO 26 (ADC).

 Buzzer: GPIO 21 (PWM).

 Sensor Ultrassônico: TRIG (GPIO 18), ECHO (GPIO 17).

 Servo: GPIO 16 (PWM).

 LDR: GPIO 4 (entrada com pull-down).

 Verifique as conexões e alimente o Pico via USB ou fonte externa.



3. Configuração da Rede Wi-Fi

Edite as definições no código (main.c):


 #define WIFI_SSID "SUA_REDE": Substitua por sua rede Wi-Fi.

 #define WIFI_PASSWORD "SUA_SENHA": Substitua por sua senha Wi-Fi.




 Recompile e recarregue o firmware após alterar as credenciais.



Como Usar

Inicialização:


 Conecte o Pico ao computador ou fonte de energia.

 Abra um monitor serial (ex.: minicom -b 115200 -o -D /dev/ttyACM0) para visualizar a saída.


 Configuração do Alarme:


 Ao iniciar, o sistema sincroniza o horário via NTP e exibe "Horário atual".

 Pressione o botão (GPIO 5) quando solicitado ("Deseja configurar um alarme?").

 Use o joystick (GPIO 26) para ajustar horas (0-23) e minutos (0-59), confirmando cada etapa com o botão.




Operação:

No horário do alarme, o buzzer toca e o sistema entra no modo de monitoramento.

 Aproxime a mão a ≤ 40 cm do sensor ultrassônico:

 O servo abre a 100°, e o buzzer continua até o remédio ser retirado (LDR detecta "Tomado").

 Afaste a mão (> 40 cm):


Se o remédio não for tomado, o buzzer toca novamente.

 Se o remédio for tomado, o servo fecha (0°), e o sistema retorna ao estado de espera.







Monitoramento:

Acompanhe a saída no monitor serial no formato:

| Distância: 39.50 cm | Servo: 100° | LDR: Não tomado |

Estrutura do Código

main.c:


Contém a lógica principal em uma máquina de estados (enum State).

 Configurações de periféricos em funções como setup_buzzer(), setup_servo(), etc.

 Sincronização NTP via ntp_recv_callback() e controle do alarme com RTC.




 Estados:


INITIAL: Aguarda sincronização NTP.

 SHOW_TIME: Exibe horário atual.

 ASK_ALARM, SET_ALARM_HOURS, SET_ALARM_MINUTES: Configuração do alarme.

 ALARM_SET: Espera o alarme disparar.

 ALARM_TRIGGERED: Ativa o buzzer e transita para monitoramento.

 MONITORING: Gerencia servo, buzzer e sensores.




 Interrupções:


Usa uma interrupção do RTC (alarm_handler()) para disparar o alarme.






Personalização

Mudar o Ângulo do Servo:


Em set_servo_angle(SERVO_PIN, 100), altere 100 para outro valor (0-180°).




 Ajustar a Distância:


Modifique if (distance <= 40.0) para outro limite (ex.: 30.0 cm).




 Frequência do Buzzer:


Altere #define BUZZER_FREQUENCY 100 para outra frequência (ex.: 200 Hz) e recompile.




 Horário NTP:


Substitua #define NTP_SERVER "time.google.com" por outro servidor NTP, se desejado.






Depuração

Use o monitor serial para verificar mensagens como "Alarme disparado!" ou erros ("Erro ao ler RTC").

 Se o Wi-Fi não conectar, cheque as credenciais em WIFI_SSID e WIFI_PASSWORD.

 Caso o servo ou buzzer não funcione, confirme as conexões e os pinos no código.



Limitações

Requer conexão Wi-Fi para sincronização inicial do horário.

 Depende de alimentação constante (USB ou externa).

 O sensor ultrassônico pode ter imprecisões em ambientes com reflexões sonoras.

 O LDR precisa de calibração para diferentes condições de luz.
