#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "lwip/dns.h"
#include "hardware/rtc.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"

// Definições de pinos
#define BUTTON_PIN 5        // Botão A para configurar alarme
#define JOYSTICK_VRY_PIN 26 // Pino ADC para o eixo Y do joystick
#define BUZZER_PIN 21       // Pino PWM para o buzzer passivo (GPIO 21)
#define TRIG_PIN 18         // Pino TRIG do sensor ultrassônico
#define ECHO_PIN 17         // Pino ECHO do sensor ultrassônico
#define SERVO_PIN 16        // Pino PWM para o servo SG90
#define LDR_D0_PIN 4        // Pino digital D0 do módulo LDR (GPIO 4)

// Configuração da frequência do buzzer (em Hz)
#define BUZZER_FREQUENCY 100 // Frequência de 100 Hz para buzzer passivo (ajuste se necessário)

// Configurações da rede Wi-Fi
#define WIFI_SSID "CLARO_2C7995"
#define WIFI_PASSWORD "A4AuyRc#tT"

// Configurações NTP
#define NTP_SERVER "time.google.com"
#define NTP_PORT 123
#define NTP_MSG_LEN 48  // Tamanho do pacote NTP
#define NTP_DELTA 2208988800ULL  // Segundos entre 1900 e 1970 (epoch Unix)

// Constantes para o ultrassônico
#define SOUND_SPEED 34300 // Velocidade do som em cm/s (a 20°C)
#define MAX_DISTANCE 400  // Distância máxima em cm (ajuste conforme necessário)

// Constantes para o servo
#define SERVO_MIN_WIDTH 550   // Ajuste para 550 us (teste valores entre 500 e 600 para 0°)
#define SERVO_MAX_WIDTH 2500  // Pulso máximo em us (180°)
#define PWM_FREQ 50          // Frequência do PWM (50Hz)

// Variáveis globais
static struct udp_pcb *udp;
static ip_addr_t ntp_server_addr;
static bool ntp_request_sent = false;

volatile bool servo_state = false; // Estado do servo (false = 0°, true = 180°)
volatile bool ldr_active = false;  // Estado da leitura do LDR (false = desativado, true = ativado)
volatile bool distance_active = false; // Estado da leitura da distância (false = desativado, true = ativado)
volatile bool buzzer_active = false; // Estado do buzzer (false = desativado, true = ativado)

// Estados do sistema
enum State {
    INITIAL,
    SHOW_TIME,
    ASK_ALARM,
    SET_ALARM_HOURS,
    SET_ALARM_MINUTES,
    ALARM_SET,
    ALARM_TRIGGERED,
    MONITORING // Novo estado para monitoramento após o alarme
} current_state = INITIAL;

// Variáveis para o alarme
int alarm_hours = 0;  // 0-23
int alarm_minutes = 0; // 0-59
static rtc_callback_t alarm_callback = NULL;

// Função para configurar o joystick (pinos de leitura e ADC)
void setup_joystick() {
    adc_init();         // Inicializa o módulo ADC
    adc_gpio_init(JOYSTICK_VRY_PIN); // Configura o pino do eixo Y para entrada ADC
}

// Função para configurar o botão
void setup_button() {
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN); // Pull-up para evitar flutuações
}

// Função para configurar o buzzer com PWM
void setup_buzzer() {
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM); // Configura o GPIO como PWM
    uint slice = pwm_gpio_to_slice_num(BUZZER_PIN); // Obtém o slice do PWM
    // Configurar o PWM com frequência desejada
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, clock_get_hz(clk_sys) / (BUZZER_FREQUENCY * 4096)); // Divisor de clock
    pwm_init(slice, &config, true);

    // Iniciar o PWM no nível baixo
    pwm_set_gpio_level(BUZZER_PIN, 0);

}

// Função para configurar o sensor ultrassônico
void setup_ultrasonic() {
    gpio_init(TRIG_PIN);
    gpio_set_dir(TRIG_PIN, GPIO_OUT);
    gpio_put(TRIG_PIN, 0); // Inicializa TRIG em baixo

    gpio_init(ECHO_PIN);
    gpio_set_dir(ECHO_PIN, GPIO_IN);
}

// Função para configurar o servo motor
void setup_servo() {
    gpio_set_function(SERVO_PIN, GPIO_FUNC_PWM);  // Define o GPIO como saída PWM
    uint slice = pwm_gpio_to_slice_num(SERVO_PIN); // Obtém o slice do PWM
    pwm_set_wrap(slice, 20000); // 20.000 ciclos → 50Hz (20ms de período)
    pwm_set_clkdiv(slice, 125.0f); // Reduz o clock para ajustar a escala
    pwm_set_enabled(slice, true); // Habilita o PWM
    pwm_set_gpio_level(SERVO_PIN, SERVO_MIN_WIDTH); // Inicia em 0° (550 us)
}

// Função para configurar o módulo LDR
void setup_ldr() {
    gpio_init(LDR_D0_PIN);
    gpio_set_dir(LDR_D0_PIN, GPIO_IN);
    gpio_pull_down(LDR_D0_PIN); // Usa pull-down (ajuste se necessário, pode ser pull-up)
}

// Função para ligar o buzzer
void buzzer_on() {
    if (!buzzer_active) {
        printf("Buzzer ativado");
        uint slice = pwm_gpio_to_slice_num(BUZZER_PIN);
         // Configurar o duty cycle para 50% (ativo)
        pwm_set_gpio_level(BUZZER_PIN, 2048);
        buzzer_active = true;
    }
}

// Função para desligar o buzzer
void buzzer_off() {
    if (buzzer_active) {
        uint slice = pwm_gpio_to_slice_num(BUZZER_PIN);
        pwm_set_gpio_level(BUZZER_PIN, 0); // Desliga o PWM
        buzzer_active = false;
        printf("Buzzer desativado.\n");
    }
}

// Função para definir o ângulo do servo (0° a 180°)
void set_servo_angle(uint pin, float angle) {
    uint duty = SERVO_MIN_WIDTH + (angle * (SERVO_MAX_WIDTH - SERVO_MIN_WIDTH) / 180); // Converte ângulo para duty cycle (550 a 2500)
    pwm_set_gpio_level(pin, duty);
}

// Função para medir a distância com o sensor ultrassônico
float measure_distance() {
    gpio_put(TRIG_PIN, 1);
    sleep_us(10);
    gpio_put(TRIG_PIN, 0);

    uint32_t start_time = time_us_32();
    while (!gpio_get(ECHO_PIN) && (time_us_32() - start_time) < 1000000); // Timeout 1s
    if (time_us_32() - start_time >= 1000000) return -1; // Timeout, erro

    start_time = time_us_32();
    while (gpio_get(ECHO_PIN) && (time_us_32() - start_time) < 1000000); // Aguarda ECHO terminar
    if (time_us_32() - start_time >= 1000000) return -1; // Timeout, erro

    uint32_t end_time = time_us_32();
    uint32_t pulse_duration = end_time - start_time; // Tempo em microssegundos
    float distance = (pulse_duration * SOUND_SPEED) / 2000000.0; // Distância em cm
    return distance;
}

// Função para ler o estado do LDR e retornar como string
const char* read_ldr() {
    bool ldr_state = gpio_get(LDR_D0_PIN);
    return ldr_state ? "Não tomado" : "Tomado";
}

// Função para ler o eixo Y do joystick
void joystick_read_axis(uint16_t *eixo_y) {
    adc_select_input(0);
    sleep_us(2);
    *eixo_y = adc_read();
}

// Função para verificar o movimento do joystick e ajustar horas/minutos
void handle_joystick_input(uint16_t eixo_y, int *value, int max_value) {
    const uint16_t CENTER_RANGE_MIN = 1980;
    const uint16_t CENTER_RANGE_MAX = 1983;
    const uint16_t UP_THRESHOLD = 4090;
    const uint16_t DOWN_THRESHOLD = 25;

    if (eixo_y >= UP_THRESHOLD) {
        (*value)++;
        if (*value > max_value) *value = 0;
        printf("%02d:%02d\n", alarm_hours, alarm_minutes);
    } else if (eixo_y <= DOWN_THRESHOLD) {
        (*value)--;
        if (*value < 0) *value = max_value;
        printf("%02d:%02d\n", alarm_hours, alarm_minutes);
    }
}

// Callback do alarme RTC
void alarm_handler(void) {
    printf("Alarme disparado!\n");
    current_state = ALARM_TRIGGERED;
    rtc_disable_alarm(); // Desativa o alarme para evitar repetição
}

// Função para verificar e corrigir o RTC
void check_rtc() {
    datetime_t t;
    if (!rtc_get_datetime(&t)) {
        //printf("Erro ao ler RTC. Reiniciando RTC...\n");
        rtc_init();
        if (!rtc_get_datetime(&t)) {
           // printf("Falha crítica no RTC. Verifique hardware ou sincronização NTP.\n");
        } else {
            printf("RTC reiniciado, horário atual: %02d:%02d:%02d\n", t.hour, t.min, t.sec);
        }
    }
}

void ntp_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    if (p->len >= NTP_MSG_LEN) {
        uint8_t *buf = (uint8_t *)p->payload;
        //printf("Bytes NTP (40-43): %02x %02x %02x %02x\n", buf[40], buf[41], buf[42], buf[43]); Print para debbug
        uint32_t ntp_seconds = ((uint32_t)buf[40] << 24) | ((uint32_t)buf[41] << 16) | 
                              ((uint32_t)buf[42] << 8) | (uint32_t)buf[43];
        //printf("Timestamp NTP bruto: %u\n", ntp_seconds); Print para debbug
        uint32_t unix_seconds = ntp_seconds - NTP_DELTA;
        //printf("Timestamp Unix (UTC): %u\n", unix_seconds); Print para debbug
        unix_seconds -= (3 * 3600); // UTC-3
        time_t rawtime = unix_seconds;
        struct tm *timeinfo = gmtime(&rawtime);
        if (timeinfo) {
            datetime_t dt = {
                .year  = timeinfo->tm_year + 1900,
                .month = timeinfo->tm_mon + 1,
                .day   = timeinfo->tm_mday,
                .dotw  = timeinfo->tm_wday,
                .hour  = timeinfo->tm_hour,
                .min   = timeinfo->tm_min,
                .sec   = timeinfo->tm_sec
            };
            rtc_set_datetime(&dt);
            printf("Horário definido (UTC-3): %04d-%02d-%02d %02d:%02d:%02d\n",
                   dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec);
            current_state = SHOW_TIME;
            check_rtc();
        } else {
           // printf("Erro ao converter timestamp para struct tm\n"); Print para debbug
        }
    }
    pbuf_free(p);
    ntp_request_sent = false;
}

void ntp_request(void) {
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
    uint8_t *req = (uint8_t *)p->payload;
    memset(req, 0, NTP_MSG_LEN);
    req[0] = 0x1B; // LeapIndicator = 0, Version = 3, Mode = 3 (client) 
    udp_sendto(udp, p, &ntp_server_addr, NTP_PORT);
    pbuf_free(p);
    ntp_request_sent = true;
}

void dns_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    if (ipaddr != NULL) {
        ntp_server_addr = *ipaddr;
      //  printf("Endereço NTP resolvido: %s\n", ipaddr_ntoa(ipaddr)); Print para debbug
        ntp_request();
    } else {
       // printf("Falha ao resolver o endereço NTP\n"); Print para debbug
    }
}

int main() {
    stdio_init_all();

    rtc_init();

    setup_joystick();
    setup_button();
    setup_buzzer();
    setup_ultrasonic();
    setup_servo();
    setup_ldr();

    if (cyw43_arch_init()) {
       // printf("Erro ao inicializar Wi-Fi\n"); Print para debbug
        return 1;
    }
    cyw43_arch_enable_sta_mode();
    printf("Conectando ao Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        printf("Falha ao conectar ao Wi-Fi\n");
        return 1;
    }
    printf("Conectado ao Wi-Fi\n");

    udp = udp_new();
    udp_bind(udp, IP_ADDR_ANY, 0);
    udp_recv(udp, ntp_recv_callback, NULL);

    err_t err = dns_gethostbyname(NTP_SERVER, &ntp_server_addr, dns_callback, NULL);
    if (err == ERR_INPROGRESS) {
       // printf("Resolvendo endereço NTP...\n"); Print para debbug
    } else if (err == ERR_OK) {
        dns_callback(NTP_SERVER, &ntp_server_addr, NULL);
    } else {
      //  printf("Erro no DNS: %d\n", err); Print para debbug
    }

    uint16_t valor_y;
    while (true) {
        cyw43_arch_poll();
        check_rtc();

        switch (current_state) {
            case INITIAL:
                sleep_ms(100);
                break;

            case SHOW_TIME:
                datetime_t current_time;
                if (rtc_get_datetime(&current_time)) {
                    printf("Horário atual: %02d:%02d:%02d\n", current_time.hour, current_time.min, current_time.sec);
                } else {
                    printf("Erro ao ler horário atual. Verifique RTC.\n");
                }
                printf("Deseja configurar um alarme? Aperte o botão A para sim\n");
                current_state = ASK_ALARM;
                break;

            case ASK_ALARM:
                if (!gpio_get(BUTTON_PIN)) {
                    sleep_ms(50);
                    while (!gpio_get(BUTTON_PIN));
                    datetime_t current_time;
                    if (rtc_get_datetime(&current_time)) {
                        alarm_hours = current_time.hour;
                        alarm_minutes = current_time.min;
                        printf("Horário atual capturado para alarme: %02d:%02d\n", alarm_hours, alarm_minutes);
                    } else {
                        printf("Erro ao capturar horário para alarme. Usando 00:00.\n");
                        alarm_hours = 0;
                        alarm_minutes = 0;
                    }
                    current_state = SET_ALARM_HOURS;
                    printf("Defina as horas do alarme\n");
                    printf("%02d:%02d\n", alarm_hours, alarm_minutes);
                }
                sleep_ms(100);
                break;

            case SET_ALARM_HOURS:
                joystick_read_axis(&valor_y);
                handle_joystick_input(valor_y, &alarm_hours, 23);
                if (!gpio_get(BUTTON_PIN)) {
                    sleep_ms(50);
                    while (!gpio_get(BUTTON_PIN));
                    current_state = SET_ALARM_MINUTES;
                    printf("Defina os minutos do alarme\n");
                }
                sleep_ms(100);
                break;

            case SET_ALARM_MINUTES:
                joystick_read_axis(&valor_y);
                handle_joystick_input(valor_y, &alarm_minutes, 59);
                if (!gpio_get(BUTTON_PIN)) {
                    sleep_ms(50);
                    while (!gpio_get(BUTTON_PIN));
                    current_state = ALARM_SET;
                    datetime_t alarm_time = {
                        .year  = 2025,
                        .month = 2,
                        .day   = 21,
                        .dotw  = 5,
                        .hour  = alarm_hours,
                        .min   = alarm_minutes,
                        .sec   = 0
                    };
                    printf("Configurando alarme para %02d:%02d:00...\n", alarm_hours, alarm_minutes);
                    rtc_disable_alarm();
                   //// printf("Alarme anterior desativado.\n"); Print para debbug
                    alarm_callback = alarm_handler;
                    printf("Callback do alarme configurado.\n");
                    alarm_time.year = -1;
                    alarm_time.month = -1;
                    alarm_time.day = -1;
                    alarm_time.dotw = -1;
                    alarm_time.sec = -1;
                    rtc_set_alarm(&alarm_time, alarm_callback);
                   // printf("Alarme setado no RTC.\n"); Print para debbug
                    rtc_enable_alarm();
                    ////printf("Alarme ativado no RTC.\n"); Print para debbug
                    printf("Alarme configurado\n"); 
                }
                sleep_ms(100);
                break;

            case ALARM_SET:
                sleep_ms(1000);
                break;

            case ALARM_TRIGGERED:
                buzzer_on(); // Liga o buzzer
                current_state = MONITORING;
                break;

            case MONITORING:
                float distance = measure_distance();
                if (distance > 0 && distance <= MAX_DISTANCE) {
                    if (distance <= 40.0) {
                        if (!servo_state) {
                            set_servo_angle(SERVO_PIN, 100); // Move servo para 100°
                            servo_state = true;
                            buzzer_off(); // Desliga o buzzer quando o servo se move
                            ldr_active = true;
                        }
                    } else {
                        if (servo_state) {
                            set_servo_angle(SERVO_PIN, 0); // Move servo para 0°
                            servo_state = false;
                            ldr_active = false;
                            current_state = ALARM_SET; // Volta para ALARM_SET
                        }
                    }
                    // Print formatado em colunas
                    printf("| Distância: %6.2f cm | Servo: %3u° | LDR: %s |\n",
                           distance, servo_state ? 100 : 0, ldr_active ? read_ldr() : "Desativado");
                } else {
                    printf("| Distância: Erro    | Servo: %3u° | LDR: %s |\n",
                           servo_state ? 100 : 0, ldr_active ? read_ldr() : "Desativado");
                }
                sleep_ms(100); // Pequeno delay para evitar sobrecarga
                break;
        }
    }

    udp_remove(udp);
    cyw43_arch_deinit();
    return 0;
}