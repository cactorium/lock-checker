typedef void* handle;
int lock(handle h, int delay);
void unlock(handle h);

#define delay (255)

handle tmp;

void foo(int i) {
    {
        if (lock(tmp, delay) == 0) {
            return;
        }
    }

    if (i == 2) {
        unlock(tmp);
        return;
    }

    unlock(tmp);
}
