#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include "monitor_ioctl.h"

int main() {
    int fd = open("/dev/container_monitor", O_RDWR);
    if (fd < 0) {
        perror("Failed to open device. Did you sudo insmod monitor.ko?");
        return 1;
    }

    struct monitor_request req;
    req.pid = getpid();
    strncpy(req.container_id, "student_a_test", 31);
    
    // Limits
    req.soft_limit_bytes = 20 * 1024 * 1024; // 20 MiB
    req.hard_limit_bytes = 40 * 1024 * 1024; // 40 MiB

    if (ioctl(fd, MONITOR_REGISTER, &req) < 0) {
        perror("IOCTL Registration failed");
        close(fd);
        return 1;
    }

    printf("Registered PID %d.\n", req.pid);
    printf("Limits: Soft=20MB, Hard=40MB\n\n");

    // STAGE 1: Cross the Soft Limit
    printf("[Step 1] Allocating 25MB (Crossing Soft Limit)...\n");
    char *buf1 = malloc(25 * 1024 * 1024);
    if (buf1) {
        memset(buf1, 1, 25 * 1024 * 1024);
        printf("Done. Waiting 3 seconds for kernel to detect SOFT LIMIT...\n\n");
        sleep(3); 
    }

    // STAGE 2: Cross the Hard Limit
    printf("[Step 2] Allocating another 20MB (Total 45MB - Crossing Hard Limit)...\n");
    char *buf2 = malloc(20 * 1024 * 1024);
    if (buf2) {
        memset(buf2, 1, 20 * 1024 * 1024);
        printf("Done. Waiting for kernel to DISPATCH SIGKILL...\n");
    }

    // If the kernel fails to kill us, we loop here
    while(1) {
        sleep(1);
    }

    close(fd);
    return 0;
}
