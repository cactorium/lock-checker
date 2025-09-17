#include "FreeRTOS.h"
#include "semphr.h"

SemaphoreHandle_t tmp;

static void bar(void);
static void foobar(void);

int foo(int v) {
    xSemaphoreTake(tmp, portMAX_DELAY);
    bar();
    if (v == 7) {
        xSemaphoreGive(tmp);
        return 2;
    }
    xSemaphoreGive(tmp);
    return 5;
}

static void bar() {
    for (int i = 0; i < 10; i++) {
        foobar();
    }
}

int test = 11;
static void foobar() {
    xSemaphoreTake(tmp, portMAX_DELAY);
    if (test == 12) {
        xSemaphoreGive(tmp);
        return;
    }

    test++;
    xSemaphoreGive(tmp);
}

void baz(void) {
    xSemaphoreTake(tmp, 6);
    xSemaphoreGive(tmp);
}
