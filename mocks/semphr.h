#pragma once

#include <stdbool.h>

#define pdTRUE (true)
#define pdFALSE (false)

typedef void* SemaphoreHandle_t;
typedef int StaticSemaphore_t;

SemaphoreHandle_t xSemaphoreCreateStatic(StaticSemaphore_t*);

bool xSemaphoreTake(SemaphoreHandle_t sem, int timeout);
void xSemaphoreGive(SemaphoreHandle_t sem);
