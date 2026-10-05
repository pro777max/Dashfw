/*
 * DASHFW — тестовый скетч Arduino Mega 2560 (генератор данных)
 *
 * Wiring:
 *   Mega Pin18 (TX1) -> ESP32-P4 GPIO44 (RX UART2)
 *   Mega Pin19 (RX1) <- ESP32-P4 GPIO43 (TX UART2)
 *   Mega GND         -> ESP32-P4 GND   (обязательно!)
 *
 * Протокол: "SPEED:%d,RPM:%d,TEMP:%d\n" @ 115200 8N1, период 100 мс.
 * USB-Serial (115200) дублирует строку для отладки.
 *
 * ВАЖНО: <math.h> подключён явно. Без него avr-gcc выдаёт
 *        "implicit declaration of function 'sin'" и линкуется неверная версия.
 *        %f в printf НЕ используется — иначе нужен -Wl,-u,vfprintf -lm.
 *
 * Загрузка ТОЛЬКО через Raspberry Pi (arduino-cli заблокирован, 403):
 *   avrdude -c arduino -p atmega2560 -P /dev/ttyUSB0 -b 115200 -D -U flash:w:mega_test.hex:i
 */

#include <Arduino.h>
#include <math.h>

#define BAUD_LINK  115200UL
#define BAUD_USB   115200UL
#define PERIOD_MS  100UL

void setup(void)
{
    Serial.begin(BAUD_USB);    /* USB, отладка */
    Serial1.begin(BAUD_LINK);  /* Pin18/Pin19, связь с ESP32-P4 */
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop(void)
{
    static unsigned long speed_kmh = 0;
    static unsigned long rpm       = 700;
    static bool          dir_up    = true;
    static unsigned long last_ms   = 0;

    unsigned long now = millis();
    if (now - last_ms < PERIOD_MS) {
        return;
    }
    last_ms = now;

    /* Пила: скорость 0..180 км/ч, обороты 700..7500 */
    if (dir_up) {
        speed_kmh += 2;
        rpm       += 90;
        if (speed_kmh >= 180) {
            dir_up = false;
        }
    } else {
        speed_kmh = (speed_kmh >= 2) ? (speed_kmh - 2) : 0;
        rpm       = (rpm >= 160) ? (rpm - 90) : 700;
        if (speed_kmh == 0 && rpm <= 700) {
            dir_up = true;
        }
    }
    if (rpm < 700)  rpm = 700;
    if (rpm > 7500) rpm = 7500;

    /* Температура 75..95, синусоида */
    int temp_c = 85 + (int)(10.0 * sin((double)now / 5000.0));

    Serial1.printf("SPEED:%lu,RPM:%lu,TEMP:%d\n", speed_kmh, rpm, temp_c);
    Serial.printf("SPEED:%lu,RPM:%lu,TEMP:%d\n", speed_kmh, rpm, temp_c);

    digitalWrite(LED_BUILTIN, (now / 500) & 1);
}