// Z '/opt/arm/stm32/inc'.
#include <gpio.h>
#include <stm32.h>

#define WHEN_ZERO 0
#define WHEN_ONE  1

#define OLD 0
#define NEW 1

#define LEFT 0
#define LEFT_REG 1
#define LEFT_BTN_PIN 3

#define RIGHT 1
#define RIGHT_REG 1
#define RIGHT_BTN_PIN 4

#define UP 2
#define UP_REG 1
#define UP_BTN_PIN 5

#define DOWN 3
#define DOWN_REG 1
#define DOWN_BTN_PIN 6

#define FIRE 4
#define FIRE_REG 1
#define FIRE_BTN_PIN 10

#define USER 5
#define USER_REG 2
#define USER_BTN_PIN 13

#define MODE 6
#define MODE_REG 0
#define MODE_BTN_PIN 0

#define changed(reg1,reg2,pin) \
  ((reg1 >> pin) & 1) ^ ((reg2 >> pin) & 1)


// REJESTR CR1.
// Tryb pracy.
#define USART_Mode_Rx_Tx (USART_CR1_RE | USART_CR1_TE)
#define USART_Enable     USART_CR1_UE

// Przesyłane słowo to dane łącznie z ewentualnym bitem parzystości.
#define USART_WordLength_8b 0x0000
#define USART_WordLength_9b USART_CR1_M

// Bit parzystości.
#define USART_Parity_No   0x0000
#define USART_Parity_Even USART_CR1_PCE
#define USART_Parity_Odd  (USART_CR1_PCE | USART_CR1_PS)

// REJESTR CR2.
// Bit(y) stopu.
#define USART_StopBits_1   0x0000
#define USART_StopBits_0_5 0x1000
#define USART_StopBits_2   0x2000
#define USART_StopBits_1_5 0x3000

// REJESTR CR3.
// Sterowanie przepływem.
#define USART_FlowControl_None 0x0000
#define USART_FlowControl_RTS  USART_CR3_RTSE
#define USART_FlowControl_CTS  USART_CR3_CTSE

/* Po włączeniu mikrokontroler STM32F411 jest taktowany
 * wewnętrznym generatorem RC HSI (ang. High Speed
 * Internal) o częstotliwości 16 MHz.
 */
#define HSI_HZ 16000000U

/* UART1 jest taktowany zegarem PCLK2, który po
 * włączeniu mikrokontrolera jest zegarem HSI.
 */
#define PCLK2_HZ HSI_HZ
// Przykładowa konfiguracja.
#define BAUD_RATE 9600U
// len("RIGHT RELEASED\r\n\0") = 17
#define MAX_STR_LEN 17

// Circular outer buffer size.
#define OUTSZ 1000

char outbuf[OUTSZ] = "";
uint16_t a = 0, b = 0;

uint8_t btn_pins[] = {
  LEFT_BTN_PIN,
  RIGHT_BTN_PIN,
  UP_BTN_PIN,
  DOWN_BTN_PIN,
  FIRE_BTN_PIN,
  USER_BTN_PIN,
  MODE_BTN_PIN
};

char messages[][2][MAX_STR_LEN] = {
  {"LEFT PRESSED\r\n", "LEFT RELEASED\r\n"},
  {"RIGHT PRESSED\r\n", "RIGHT RELEASED\r\n"},
  {"UP PRESSED\r\n", "UP RELEASED\r\n"},
  {"DOWN PRESSED\r\n", "DOWN RELEASED\r\n"},
  {"FIRE PRESSED\r\n", "FIRE RELEASED\r\n"},
  {"USER PRESSED\r\n", "USER RELEASED\r\n"},
// Aktywacja jest na odwrót dla MODE.
  {"MODE RELEASED\r\n", "MODE PRESSED\r\n"}
};

uint8_t reg_inds[] = {
  LEFT_REG,
  RIGHT_REG,
  UP_REG,
  DOWN_REG,
  FIRE_REG,
  USER_REG,
  MODE_REG
};

uint32_t regs[][2] = {
  {0, 0},
  {0, 0},
  {0, 0}
};

uint16_t len(const char *s) {
  uint16_t n = 0;
  while (s[n] != '\0')
    ++n;
  return n;
}

void send(char *s, char *buf, uint16_t *off) {
  uint16_t sn = len(s);
  for (uint16_t i = 0; i < sn; ++i) {
    buf[*off] = s[i];
    *off = ((*off) + 1) % OUTSZ;
  }
}

void wlacz_taktowanie() {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN |
                  RCC_AHB1ENR_GPIOBEN |
                  RCC_AHB1ENR_GPIOCEN;
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
}

void konfiguruj_linie() {
  // Konfigurujemy linię TXD.
  GPIOafConfigure(GPIOA,
                  9,
                  GPIO_OType_PP,
                  GPIO_Fast_Speed,
                  GPIO_PuPd_NOPULL,
                  GPIO_AF_USART1);

  // Konfigurujemy linię RXD.
  GPIOafConfigure(GPIOA,
                  10,
                  GPIO_OType_PP,
                  GPIO_Fast_Speed,
                  GPIO_PuPd_UP,
                  GPIO_AF_USART1);
}

void konfiguruj_USART() {
  konfiguruj_linie();

  // Przykładowa konfiguracja (układ pozostaje nieaktywny – nie ustawiamy bitu USART Enable)
  // Tryb pracy, długość słowa, bit parzystości.
  USART1->CR1 = USART_Mode_Rx_Tx |
                USART_WordLength_8b |
                USART_Parity_No;
  // Bit(y) stopu.
  USART1->CR2 = USART_StopBits_1;
  // Sterowanie przepływem.
  USART1->CR3 = USART_FlowControl_None;
  // Częstotliwość taktowania.
  USART1->BRR = (PCLK2_HZ + (BAUD_RATE / 2U)) / BAUD_RATE;
}

// Jakby jakiś przycisk się zmienił
// to dodaj znaki do kolejki do wypisania.
void note_btn_change(uint8_t btn) {
  uint8_t btn_pin = btn_pins[btn],
          ind     = reg_inds[btn];
  uint32_t new_btn_state = regs[ind][NEW];
  if (changed(regs[ind][OLD], new_btn_state, btn_pin)) {
    if ((new_btn_state >> btn_pin) & 1) {
      send(messages[btn][WHEN_ONE], outbuf, &b);
    } else {
      send(messages[btn][WHEN_ZERO], outbuf, &b);
    }
  }
}

int main() {
  wlacz_taktowanie();
  __NOP();

  konfiguruj_USART();

  // Uruchamiamy.
  USART1->CR1 |= USART_Enable;

  regs[0][OLD] = GPIOA->IDR,
  regs[1][OLD] = GPIOB->IDR,
  regs[2][OLD] = GPIOC->IDR;

  for (;;) {
    regs[0][NEW] = GPIOA->IDR,
    regs[1][NEW] = GPIOB->IDR,
    regs[2][NEW] = GPIOC->IDR;

    // Jakby coś było do wypisania i się zwolniło miejsce to wypisz 1 znak.
    if (a != b && ((USART1->SR & USART_SR_TXE) != 0)) {
      USART1->DR = outbuf[a];
      a = (a + 1) % OUTSZ;
    }

    note_btn_change(LEFT);
    note_btn_change(RIGHT);
    note_btn_change(UP);
    note_btn_change(DOWN);
    note_btn_change(FIRE);
    note_btn_change(USER);
    note_btn_change(MODE);

    for (uint8_t i = 0; i <= 2; ++i)
      regs[i][OLD] = regs[i][NEW];
  }
}
