// STANDARD INCLUDES
#include <stdio.h>l.g
#include <conio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
// KERNEL INCLUDES
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "extint.h"
// HARDWARE SIMULATOR UTILITY FUNCTIONS
#include "HW_access.h"
// SERIAL SIMULATOR CHANNEL TO USE
#define COM_CH_0 (0)
#define COM_CH_1 (1)
#define COM_CH_2 (2)
// TASK PRIORITIES
#define TASK_SERIAL_REC_PRI   ( tskIDLE_PRIORITY + 5 )
#define TASK_TEMP_PRI         ( tskIDLE_PRIORITY + 4 )
#define TASK_SERIAL_SEND_PRI  ( tskIDLE_PRIORITY + 3 )
#define TASK_LED_PRI          ( tskIDLE_PRIORITY + 2 )
#define TASK_DISPLAY_PRI      ( tskIDLE_PRIORITY + 1 )
// TASKS: FORWARD DECLARATIONS
void LEDBar_Task(void* pvParameters);
void SerialReceive_Task(void* pvParameters);
void SerialReceive_Task_CH1(void* pvParameters);
void Temperature_Task(void* pvParameters);
void Display_Task(void* pvParameters);
void SerialSend_Task(void* pvParameters);
static void TimerCallback(TimerHandle_t tmH);
// TRASNMISSION DATA - CONSTANT IN THIS APPLICATION
// RECEPTION DATA BUFFER - COM 0
#define R_BUF_SIZE (32)
uint8_t r_buffer[R_BUF_SIZE];
unsigned volatile r_point;
// FIFO BAFFER ZA POSLEDNJIH 5 VREDNOSTI
#define FIFO_SIZE (5)
#define SEG_DASH 16
#define TX_QUEUE_LENGTH 10
#define TX_MSG_SIZE 50
uint8_t fifo_ch0[FIFO_SIZE];
uint8_t fifo_ch1[FIFO_SIZE];
uint8_t fifo_index_ch0 = 0;
uint8_t fifo_index_ch1 = 0;
uint8_t blink_alarm = 0;
uint8_t blink_temp = 0;

typedef enum {
	ALARM,
	ALARM_OFF,
	TEMP_95,
	TEMP_95_OFF,
	VENT_ON,
	VENT_OFF
} StatusMsg_t;

// 7-SEG NUMBER DATABASE - ALL HEX DIGITS [ 0 1 2 3 4 5 6 7 8 9 A B C D E F ]
static const char hexnum[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71 };

// DISPLAY VARIABLES
static unsigned char dispMem[5];

// GLOBAL OS-HANDLES
SemaphoreHandle_t TBE_BinarySemaphore;
SemaphoreHandle_t RXC_BinarySemaphore_CH0;
SemaphoreHandle_t RXC_BinarySemaphore_CH1;
QueueHandle_t Temp_Queue_CH0;
QueueHandle_t Temp_Queue_CH1;
QueueHandle_t Display_Queue;
QueueHandle_t SerialTx_Queue;
QueueHandle_t Status_Queue;
static TimerHandle_t myTimer1;

// STRUCTURES
// INTERRUPTS //

static uint32_t prvProcessTBEInterrupt(void) { // TBE - TRANSMISSION BUFFER EMPTY - INTERRUPT HANDLER
	BaseType_t xHigherPTW = pdFALSE;
	xSemaphoreGiveFromISR(TBE_BinarySemaphore, &xHigherPTW);
	portYIELD_FROM_ISR(xHigherPTW);
}
static uint32_t prvProcessRXCInterrupt(void) { // RXC - RECEPTION COMPLETE - INTERRUPT HANDLER
	BaseType_t xHigherPTW = pdFALSE;
	if (get_RXC_status(COM_CH_0))
		xSemaphoreGiveFromISR(RXC_BinarySemaphore_CH0, &xHigherPTW);
	if (get_RXC_status(COM_CH_1))
		xSemaphoreGiveFromISR(RXC_BinarySemaphore_CH1, &xHigherPTW);
	portYIELD_FROM_ISR(xHigherPTW);
}
// MAIN - SYSTEM STARTUP POINT
void main_demo(void) {
	// INITIALIZATION OF THE PERIPHERALS
	init_7seg_comm();
	for (int i = 0; i < 4; i++) {
		dispMem[i] = 0;
	}
	init_LED_comm();
	set_LED_BAR(0, 0x00);
	set_LED_BAR(1, 0x00);
	set_LED_BAR(2, 0x00);
	set_LED_BAR(3, 0x00);
	set_LED_BAR(4, 0x00);

	myTimer1 = xTimerCreate(NULL, pdMS_TO_TICKS(20), pdTRUE, NULL, TimerCallback);
	if (myTimer1 == NULL) while (1);
	xTimerStart(myTimer1, 0);

	init_serial_downlink(COM_CH_0); // RX kanal 0
	init_serial_downlink(COM_CH_1); // RX kanal 1 (prima podatke koje šalješ)
	init_serial_uplink(COM_CH_2); // TX kanal 2 (za slanje upozorenja)
	// INTERRUPT HANDLERS
	vPortSetInterruptHandler(portINTERRUPT_SRL_TBE, prvProcessTBEInterrupt); // SERIAL TRANSMITT INTERRUPT HANDLER
	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, prvProcessRXCInterrupt); // SERIAL RECEPTION INTERRUPT HANDLER
	// BINARY SEMAPHORES
	TBE_BinarySemaphore = xSemaphoreCreateBinary(); // CREATE TBE SEMAPHORE - SERIAL TRANSMIT COMM
	RXC_BinarySemaphore_CH0 = xSemaphoreCreateBinary();
	RXC_BinarySemaphore_CH1 = xSemaphoreCreateBinary(); // CREATE RXC SEMAPHORE - SERIAL RECEIVE COMM
	// QUEUES
	Temp_Queue_CH0 = xQueueCreate(10, sizeof(int));
	Temp_Queue_CH1 = xQueueCreate(10, sizeof(int));
	Display_Queue = xQueueCreate(1, sizeof(float));
	SerialTx_Queue = xQueueCreate(TX_QUEUE_LENGTH, TX_MSG_SIZE);
	Status_Queue = xQueueCreate(10, sizeof(StatusMsg_t));
	// TASKS
	xTaskCreate(SerialReceive_Task, "SRx", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAL_REC_PRI, NULL); // SERIAL RECEIVER TASK
	r_point = 0;
	xTaskCreate(LEDBar_Task, "ST", configMINIMAL_STACK_SIZE, NULL, TASK_LED_PRI, NULL); // CREATE LED BAR TASK
	xTaskCreate(SerialReceive_Task_CH1, "SRx1", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAL_REC_PRI, NULL);
	xTaskCreate(Temperature_Task, "TEMP", configMINIMAL_STACK_SIZE, NULL, TASK_TEMP_PRI, NULL);
	xTaskCreate(Display_Task, "DISP", configMINIMAL_STACK_SIZE, NULL, TASK_DISPLAY_PRI, NULL);
	xTaskCreate(SerialSend_Task, "STX", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAL_SEND_PRI, NULL);
	// START SCHEDULER
	vTaskStartScheduler();
	while (1);
}
// TASKS: IMPLEMENTATIONS
void serial_send_string(uint8_t com_channel, const char* str) {
	while (*str) {
		send_serial_character(com_channel, *str++);
		xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);
	}
}
void SerialReceive_Task(void* pvParameters) {
	uint8_t cc = 0;

	char rx_buffer[10];
	uint8_t rx_index = 0;

	while (1) {
		xSemaphoreTake(RXC_BinarySemaphore_CH0, portMAX_DELAY);
		get_serial_character(COM_CH_0, &cc);

		// ignorisi eventualne CR/LF ako dolaze dodatno
		if (cc == '\r' || cc == '\n') {
			if (rx_index > 0) {
				rx_buffer[rx_index] = '\0';

				int value = atoi(rx_buffer);

				// === ISPIS PRIMLJENOG PODATKA ===
				printf("KANAL 0: primio podatak: %d\n", value);

				// === PROVERA ===
				if (value > 150) {
					printf("UPOZORENJE: primljen podatak veci od 150 (%d)\n", value);

					char msg[TX_MSG_SIZE];
					sprintf(msg, "SENZOR 1 NEISPRAVAN\r");
					xQueueSend(SerialTx_Queue, msg, portMAX_DELAY);
				}

				// slanje u Temp task
				xQueueSend(Temp_Queue_CH0, &value, portMAX_DELAY);

				// reset za sledecu poruku
				rx_index = 0;
			}
		}
		else {
			// skladišti karaktere (ASCII cifre)
			if (rx_index < sizeof(rx_buffer) - 1) {
				rx_buffer[rx_index++] = cc;
			}
			else {
				// overflow zaštita → reset
				rx_index = 0;
			}
		}
	}
}
void SerialReceive_Task_CH1(void* pvParameters) {
	uint8_t cc = 0;

	char rx_buffer[10];
	uint8_t rx_index = 0;

	while (1) {
		xSemaphoreTake(RXC_BinarySemaphore_CH1, portMAX_DELAY);
		get_serial_character(COM_CH_1, &cc);

		// ignorisi eventualne CR/LF ako dolaze dodatno
		if (cc == '\r' || cc == '\n') {
			if (rx_index > 0) {
				rx_buffer[rx_index] = '\0';

				int value = atoi(rx_buffer);

				// === ISPIS PRIMLJENOG PODATKA ===
				printf("KANAL 1: primio podatak: %d\n", value);

				// === PROVERA ===
				if (value > 150) {
					printf("UPOZORENJE: primljen podatak veci od 150 (%d)\n", value);

					char msg[TX_MSG_SIZE];
					sprintf(msg, "SENZOR 2 NEISPRAVAN\r");
					xQueueSend(SerialTx_Queue, msg, portMAX_DELAY);
				}

				// slanje u Temp task
				xQueueSend(Temp_Queue_CH1, &value, portMAX_DELAY);

				// reset za sledecu poruku
				rx_index = 0;
			}
		}
		else {
			// skladišti karaktere (ASCII cifre)
			if (rx_index < sizeof(rx_buffer) - 1) {
				rx_buffer[rx_index++] = cc;
			}
			else {
				// overflow zaštita → reset
				rx_index = 0;
			}
		}
	}
}

void Temperature_Task(void* pvParameters) {

	int fifo_ch0[FIFO_SIZE] = { 0 };
	int fifo_ch1[FIFO_SIZE] = { 0 };

	int index_ch0 = 0, count_ch0 = 0;
	int index_ch1 = 0, count_ch1 = 0;

	int value_ch0, value_ch1;

	float temperature1 = 0.0f, temperature2 = 0.0f;
	float T = 0.0f;
	float last_T = 0.0f;

	int have_ch0 = 0;
	int have_ch1 = 0;

	StatusMsg_t flag;

	while (1) {

		int ch0_new = 0;
		int ch1_new = 0;

		// ===== CH0 =====
		if (xQueueReceive(Temp_Queue_CH0, &value_ch0, pdMS_TO_TICKS(50)) == pdTRUE) {

			fifo_ch0[index_ch0] = value_ch0;
			index_ch0 = (index_ch0 + 1) % FIFO_SIZE;

			if (count_ch0 < FIFO_SIZE) count_ch0++;

			have_ch0 = 1;
			ch0_new = 1;
		}

		// ===== CH1 =====
		if (xQueueReceive(Temp_Queue_CH1, &value_ch1, pdMS_TO_TICKS(50)) == pdTRUE) {

			fifo_ch1[index_ch1] = value_ch1;
			index_ch1 = (index_ch1 + 1) % FIFO_SIZE;

			if (count_ch1 < FIFO_SIZE) count_ch1++;

			have_ch1 = 1;
			ch1_new = 1;
		}

		// ===== OBRADA =====
		if (have_ch0 && have_ch1 && (ch0_new || ch1_new)) {

			int sum0 = 0, sum1 = 0;

			//  SABIRAJ SAMO VALIDNE
			for (int i = 0; i < count_ch0; i++)
				sum0 += fifo_ch0[i];

			for (int i = 0; i < count_ch1; i++)
				sum1 += fifo_ch1[i];

			float avg0 = (float)sum0 / count_ch0;
			float avg1 = (float)sum1 / count_ch1;

			temperature1 = (avg0 * 100.0f) / 150.0f;
			temperature2 = (avg1 * 100.0f) / 150.0f;

			printf("TEMP SENZOR 1: %.2f C\n", temperature1);
			printf("TEMP SENZOR 2: %.2f C\n", temperature2);

			// ISPIS FIFO
			printf("Poslednjih 5 vrednosti na CH0: ");
			for (int i = 0; i < count_ch0; i++)
				printf("%d ", fifo_ch0[i]);
			printf("\n");

			printf("Poslednjih 5 vrednosti na CH1: ");
			for (int i = 0; i < count_ch1; i++)
				printf("%d ", fifo_ch1[i]);
			printf("\n");

			// ===== RAZLIKA =====
			float diff = fabs(temperature1 - temperature2);
			printf("RAZLIKA: %.2f C\n", diff);

			if (diff > 5.0f) {
				flag = ALARM;
			}
			else {
				flag = ALARM_OFF;
			}
			xQueueSend(Status_Queue, &flag, portMAX_DELAY);

			// ===== SREDNJA =====
			T = (temperature1 + temperature2) / 2.0f;
			printf("SREDNJA TEMP: %.2f C\n", T);

			xQueueOverwrite(Display_Queue, &T);

			// ===== 95°C =====
			if (T > 95.0f && last_T <= 95.0f) {
				char msg[TX_MSG_SIZE];
				sprintf(msg, "UPOZORENJE\r");
				xQueueSend(SerialTx_Queue, msg, portMAX_DELAY);

				flag = TEMP_95;
				xQueueSend(Status_Queue, &flag, portMAX_DELAY);
			}

			if (T < 95.0f && last_T >= 95.0f) {
				flag = TEMP_95_OFF;
				xQueueSend(Status_Queue, &flag, portMAX_DELAY);
			}

			// ===== VENTILATOR =====
			if (T > 90.0f && last_T <= 90.0f) {
				char msg[TX_MSG_SIZE];
				sprintf(msg, "VENTILATOR UKLJUCEN\r");
				xQueueSend(SerialTx_Queue, msg, portMAX_DELAY);

				flag = VENT_ON;
				xQueueSend(Status_Queue, &flag, portMAX_DELAY);
			}
			else if (T < 85.0f && last_T >= 85.0f) {
				char msg[TX_MSG_SIZE];
				sprintf(msg, "VENTILATOR ISKLJUCEN\r");
				xQueueSend(SerialTx_Queue, msg, portMAX_DELAY);

				flag = VENT_OFF;
				xQueueSend(Status_Queue, &flag, portMAX_DELAY);
			}

			last_T = T;
		}

		vTaskDelay(pdMS_TO_TICKS(10));
	}
}
void LEDBar_Task(void* pvParameters) {
	uint8_t blink_alarm = 0;
	uint8_t blink_temp = 0;

	uint8_t led_alarm_ch4 = 0;
	uint8_t upozorenje_95 = 0;
	uint8_t ventilator = 0;

	int counter_alarm = 0;

	while (1) {
		StatusMsg_t flag;

		if (xQueueReceive(Status_Queue, &flag, 0) == pdTRUE) {

			switch (flag) {

			case ALARM:
				led_alarm_ch4 = 1;
				break;

			case ALARM_OFF:
				led_alarm_ch4 = 0;
				break;

			case TEMP_95:
				upozorenje_95 = 1;
				break;

			case TEMP_95_OFF:
				upozorenje_95 = 0;
				break;

			case VENT_ON:
				ventilator = 1;
				break;

			case VENT_OFF:
				ventilator = 0;
				break;
			}
		}

		// ALARM >5°C (1000ms) 
		if (led_alarm_ch4) {

			counter_alarm++;

			if (counter_alarm >= 10) { // 10 * 50ms = 500ms polu perioda
				blink_alarm = !blink_alarm;
				counter_alarm = 0;
			}

			set_LED_BAR(3, blink_alarm ? 0xFF : 0x00);
		}
		else {
			set_LED_BAR(3, 0x00);
			blink_alarm = 0;
			counter_alarm = 0;
		}

		// --- TEMP >95°C ( 100ms) ---
		if (upozorenje_95) {
			set_LED_BAR(2, blink_temp ? 0xFF : 0x00);
			blink_temp = !blink_temp;
		}
		else {
			set_LED_BAR(2, 0x00);
			blink_temp = 0;
		}

		// Ventilator
		set_LED_BAR(1, ventilator ? 0x01 : 0x00);

		vTaskDelay(pdMS_TO_TICKS(50));
	}
}
void Display_Task(void* pvParameters) {
	float T = 0.0f;  // početna vrednost (može biti 0 ili neka default)

	while (1) {
		// Samo proveri da li postoji nova vrednost, ali NE BLOKIRAJ
		xQueuePeek(Display_Queue, &T, 0);   // 0 = non-blocking, uzme poslednju ako postoji

		// uvek formatiraj trenutnu T (čak i ako nije stigla nova)
		int int_part = (int)T;
		int decimal = (int)((T - int_part) * 10);  // +0.5 za bolje zaokruživanje
		int tens = int_part / 10;
		int units = int_part % 10;

		dispMem[0] = 12;           // C
		dispMem[1] = decimal;
		dispMem[2] = SEG_DASH;
		dispMem[3] = units;
		dispMem[4] = (int_part >= 10) ? tens : 0;

		// osvežavanje svakih 100 ms
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}

static void TimerCallback(TimerHandle_t tmH) {
	static unsigned char count = 0;

	select_7seg_digit(4 - count);

	if (dispMem[count] == SEG_DASH) {
		set_7seg_digit(0x08); // donja crta (segment d)
	}
	else {
		set_7seg_digit(hexnum[dispMem[count]]);
	}

	count = (count < 4) ? count + 1 : 0;
}
void SerialSend_Task(void* pvParameters) {
	char msg[TX_MSG_SIZE];

	while (1) {
		if (xQueueReceive(SerialTx_Queue, msg, portMAX_DELAY) == pdTRUE) {
			serial_send_string(COM_CH_2, msg);
		}
	}
}