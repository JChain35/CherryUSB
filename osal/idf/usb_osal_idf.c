/*
 * Copyright (c) 2022, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "usb_osal.h"
#include "usb_errno.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_heap_caps.h"

#define TAG "usb_osal_idf"

static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

#if defined CONFIG_SPIRAM && CONFIG_SPIRAM
#define MAX_OSAL_TASKS 8

struct usb_osal_static_task {
    TaskHandle_t task;
    StackType_t *stack;
    StaticTask_t *tcb;
};
struct usb_osal_static_task *osal_task = NULL;
static uint8_t osal_task_count = 0;
#endif

usb_osal_thread_t usb_osal_thread_create(const char *name, uint32_t stack_size, uint32_t prio, usb_thread_entry_t entry, void *args)
{
    TaskHandle_t task = NULL;
#if defined CONFIG_SPIRAM && CONFIG_SPIRAM
    if( osal_task == NULL ) {
        osal_task = (struct usb_osal_static_task *)heap_caps_malloc(MAX_OSAL_TASKS * sizeof(struct usb_osal_static_task), MALLOC_CAP_SPIRAM);
        if( osal_task == NULL ) {
            ESP_LOGE(TAG, "Failed to allocate memory for OSAL tasks");
            return NULL;
        }
        memset(osal_task, 0, MAX_OSAL_TASKS * sizeof(struct usb_osal_static_task));
    }

    if( osal_task_count >= MAX_OSAL_TASKS ) {
        ESP_LOGE(TAG, "Maximum number of OSAL tasks reached %d", MAX_OSAL_TASKS);
        return NULL;
    }
    
    osal_task[osal_task_count].stack = (StackType_t *)heap_caps_malloc(stack_size, MALLOC_CAP_SPIRAM);
    if( osal_task[osal_task_count].stack == NULL ) {
        ESP_LOGE(TAG, "Failed to allocate stack memory for task %s, size %"PRIu32"", name, stack_size);
        return NULL;
    }
    osal_task[osal_task_count].tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
    if( osal_task[osal_task_count].tcb == NULL ) {
        ESP_LOGE(TAG, "Failed to allocate TCB memory for task %s, %d", name, sizeof(StaticTask_t));
        free(osal_task[osal_task_count].tcb);
        return NULL;
    }

    osal_task[osal_task_count].task = NULL;
    stack_size /= sizeof(StackType_t);
    osal_task[osal_task_count].task = xTaskCreateStatic(entry, name, stack_size, args, 
        configMAX_PRIORITIES - 1 - prio, osal_task[osal_task_count].stack, osal_task[osal_task_count].tcb);
    if( osal_task[osal_task_count].task == NULL ) {
        ESP_LOGE(TAG, "Failed to create task %s", name);
        free(osal_task[osal_task_count].stack);
        free(osal_task[osal_task_count].tcb);
        return NULL;
    }

    task = osal_task[osal_task_count].task;
    osal_task_count = osal_task_count + 1;
#else
    xTaskCreate(entry, name, stack_size, args, configMAX_PRIORITIES - 1 - prio, &task);
    if (task == NULL) {
        ESP_LOGE(TAG, "Failed to create task %s", name);
        return NULL;
    }
#endif
    return (usb_osal_thread_t)task;
}

void usb_osal_thread_delete(usb_osal_thread_t thread)
{
    if (thread == NULL) {
        ESP_LOGE(TAG, "Thread is NULL, cannot delete.");
        return;
    }

#if defined CONFIG_SPIRAM && CONFIG_SPIRAM
    for (uint8_t i = 0; i < osal_task_count; i++) {
        if (osal_task[i].task == thread) {
            vTaskDelete(osal_task[i].task);
            free(osal_task[i].stack);
            free(osal_task[i].tcb);
            osal_task[i].task = NULL;
            osal_task[i].stack = NULL;
            osal_task[i].tcb = NULL;

            // Shift remaining tasks down
            for (uint8_t j = i; j < osal_task_count - 1; j++) {
                osal_task[j] = osal_task[j + 1];
            }
            osal_task_count--;
            return;
        }
    }
    ESP_LOGE(TAG, "Thread not found in OSAL task list, cannot delete.");
#else
    vTaskDelete((TaskHandle_t)thread);
#endif
}

usb_osal_sem_t usb_osal_sem_create(uint32_t initial_count)
{
    return (usb_osal_sem_t)xSemaphoreCreateCounting(1, initial_count);
}

void usb_osal_sem_delete(usb_osal_sem_t sem)
{
    vSemaphoreDelete((SemaphoreHandle_t)sem);
}

int usb_osal_sem_take(usb_osal_sem_t sem, uint32_t timeout)
{
    if (timeout == USB_OSAL_WAITING_FOREVER) {
        return (xSemaphoreTake((SemaphoreHandle_t)sem, portMAX_DELAY) == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
    } else {
        return (xSemaphoreTake((SemaphoreHandle_t)sem, pdMS_TO_TICKS(timeout)) == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
    }
}

int usb_osal_sem_give(usb_osal_sem_t sem)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    int ret;

    if (xPortInIsrContext()) {
        ret = xSemaphoreGiveFromISR((SemaphoreHandle_t)sem, &xHigherPriorityTaskWoken);
        if (ret == pdPASS) {
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    } else {
        ret = xSemaphoreGive((SemaphoreHandle_t)sem);
    }

    return (ret == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
}

void usb_osal_sem_reset(usb_osal_sem_t sem)
{
    xQueueReset((QueueHandle_t)sem);
}

usb_osal_mutex_t usb_osal_mutex_create(void)
{
    return (usb_osal_mutex_t)xSemaphoreCreateMutex();
}

void usb_osal_mutex_delete(usb_osal_mutex_t mutex)
{
    vSemaphoreDelete((SemaphoreHandle_t)mutex);
}

int usb_osal_mutex_take(usb_osal_mutex_t mutex)
{
    return (xSemaphoreTake((SemaphoreHandle_t)mutex, portMAX_DELAY) == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
}

int usb_osal_mutex_give(usb_osal_mutex_t mutex)
{
    return (xSemaphoreGive((SemaphoreHandle_t)mutex) == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
}

usb_osal_mq_t usb_osal_mq_create(uint32_t max_msgs)
{
    return (usb_osal_mq_t)xQueueCreate(max_msgs, sizeof(uintptr_t));
}

void usb_osal_mq_delete(usb_osal_mq_t mq)
{
    vQueueDelete((QueueHandle_t)mq);
}

int usb_osal_mq_send(usb_osal_mq_t mq, uintptr_t addr)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    int ret;

    if (xPortInIsrContext()) {
        ret = xQueueSendFromISR((usb_osal_mq_t)mq, &addr, &xHigherPriorityTaskWoken);
        if (ret == pdPASS) {
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    } else {
        ret = xQueueSend((usb_osal_mq_t)mq, &addr, 0xffffffff);
    }

    return (ret == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
}

int usb_osal_mq_recv(usb_osal_mq_t mq, uintptr_t *addr, uint32_t timeout)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    int ret;

    if (xPortInIsrContext()) {
        ret = xQueueReceiveFromISR((usb_osal_mq_t)mq, addr, &xHigherPriorityTaskWoken);
        if (ret == pdPASS) {
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
        return (ret == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
    } else {
        if (timeout == USB_OSAL_WAITING_FOREVER) {
            return (xQueueReceive((usb_osal_mq_t)mq, addr, portMAX_DELAY) == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
        } else {
            return (xQueueReceive((usb_osal_mq_t)mq, addr, pdMS_TO_TICKS(timeout)) == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
        }
    }
}

static void __usb_timeout(TimerHandle_t *handle)
{
    struct usb_osal_timer *timer = (struct usb_osal_timer *)pvTimerGetTimerID((TimerHandle_t)handle);

    timer->handler(timer->argument);
}

struct usb_osal_timer *usb_osal_timer_create(const char *name, uint32_t timeout_ms, usb_timer_handler_t handler, void *argument, bool is_period)
{
    struct usb_osal_timer *timer;
    (void)name;

    timer = pvPortMalloc(sizeof(struct usb_osal_timer));

    if (timer == NULL) {
        return NULL;
    }
    memset(timer, 0, sizeof(struct usb_osal_timer));

    timer->handler = handler;
    timer->argument = argument;

    timer->timer = (void *)xTimerCreate("usb_tim", pdMS_TO_TICKS(timeout_ms), is_period, timer, (TimerCallbackFunction_t)__usb_timeout);
    if (timer->timer == NULL) {
        return NULL;
    }
    return timer;
}

void usb_osal_timer_delete(struct usb_osal_timer *timer)
{
    xTimerStop(timer->timer, 0);
    xTimerDelete(timer->timer, 0);
    vPortFree(timer);
}

void usb_osal_timer_start(struct usb_osal_timer *timer)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    int ret;

    if (xPortInIsrContext()) {
        ret = xTimerStartFromISR(timer->timer, &xHigherPriorityTaskWoken);
        if (ret == pdPASS) {
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    } else {
        xTimerStart(timer->timer, 0);
    }
}

void usb_osal_timer_stop(struct usb_osal_timer *timer)
{
    xTimerStop(timer->timer, 0);
}

size_t usb_osal_enter_critical_section(void)
{
    portENTER_CRITICAL_SAFE(&spinlock);
    return 0;
}

void usb_osal_leave_critical_section(size_t flag)
{
    portEXIT_CRITICAL_SAFE(&spinlock);
}

void usb_osal_msleep(uint32_t delay)
{
    vTaskDelay(pdMS_TO_TICKS(delay));
}

void *usb_osal_malloc(size_t size)
{
    return malloc(size);
}

void usb_osal_free(void *ptr)
{
    free(ptr);
}
