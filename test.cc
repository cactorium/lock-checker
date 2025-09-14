
extern "C" {
typedef void* handle;
int lock(handle h, int delay);
void unlock(handle h);
}

#define portMAX_DELAY (65535)

handle tmp;

/*
void foo(int i) {
    if (lock(tmp, delay) == 1) {
        unlock(tmp);
        return;
    }

    lock(tmp, 65535);
    if (i == 2) {
        unlock(tmp);
        return;
    } else {
        if (i == 7) {
            //unlock(tmp);
        } else {
            //lock(tmp, 65535);
            //unlock(tmp);
        }
    }

    unlock(tmp);
}

void bar(void) {
    int i = 7;
    lock(tmp, delay);

    do {
        unlock(tmp);
    } while(0);
    unlock(tmp);
}

int foobar(int i) {
    int j = 0;
    {
        if (lock(tmp, delay) == 0) {
            j = 2;
            return -1;
        }
    }

    if (i == 2) {
        unlock(tmp);
        return i-2;
   } else {
        if (i == 7) {
            j++;
            lock(tmp, 3);
        } else {
            unlock(tmp);
        }
    }

    unlock(tmp);
    return j-3;
}
*/

static void bar(void);
static void foobar(void);

int foo(int v) {
    lock(tmp, portMAX_DELAY);
    bar();
    if (v == 7) {
        unlock(tmp);
        return 2;
    }
    unlock(tmp);
    return 5;
}

static void bar() {
    for (int i = 0; i < 10; i++) {
        foobar();
    }
}

int test = 11;
static void foobar() {
    lock(tmp, portMAX_DELAY);
    if (test == 12) {
        unlock(tmp);
        return;
    }

    test++;
    unlock(tmp);
}
