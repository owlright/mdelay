#include <sys/timex.h>
#include <stdio.h>
int main() {
    struct timex t;
    int ret;

    ret = adjtimex(&t);
    if (ret < 0) {
        perror("adjtimex");
        return 1;
    }
    printf("Current TAI offset: %d\n", t.tai);

    t.tai = 37;  // Replace <new_offset> with the desired TAI offset
    ret = adjtimex(&t);
    if (ret < 0) {
        perror("adjtimex");
        return 1;
    }

    printf("New TAI offset: %d\n", t.tai);

    return 0;
}
