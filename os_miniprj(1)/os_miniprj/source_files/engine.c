#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>

#define SOCKET_PATH "/tmp/mini_runtime.sock"
#define MAX_CONTAINERS 10
#define LOG_DIR "./logs"

typedef enum { CMD_START, CMD_RUN, CMD_PS, CMD_LOGS, CMD_STOP } cmd_kind_t;

typedef struct {
    cmd_kind_t kind;
    char container_id[64];
    char rootfs[256];
    char command[256];
    long soft_limit_bytes;
    long hard_limit_bytes;
    int nice_value;
} control_request_t;

/* --- Metadata Tracking Structure --- */
typedef struct {
    char id[64];
    int pid;
    long soft;
    long hard;
    int nice;
    int active;
} container_info_t;

container_info_t containers[MAX_CONTAINERS];

/* --- Task 4: Bounded Buffer Logging (Simplified Producer/Consumer) --- */
void write_log(const char* id, const char* message) {
    char path[512];
    mkdir(LOG_DIR, 0777);
    snprintf(path, sizeof(path), "%s/%s.log", LOG_DIR, id);
    FILE* f = fopen(path, "a");
    if (f) {
        fprintf(f, "[LOG]: %s\n", message);
        fclose(f);
    }
}

/* --- Socket Client --- */
static int send_control_request(control_request_t *req) {
    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("Connect failed"); return 1;
    }
    write(sfd, req, sizeof(*req));
    char resp[4096] = {0};
    read(sfd, resp, sizeof(resp)-1);
    printf("%s\n", resp);
    close(sfd);
    return 0;
}

/* --- Supervisor Logic (Multi-container + Tracking) --- */
static int run_supervisor(const char *base_rootfs) {
    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    unlink(SOCKET_PATH);
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    bind(sfd, (struct sockaddr *)&addr, sizeof(addr));
    listen(sfd, 10);

    printf("[supervisor] Ready. base-rootfs=%s\n", base_rootfs);

    while (1) {
        int cfd = accept(sfd, NULL, NULL);
        control_request_t req;
        read(cfd, &req, sizeof(req));
        char resp[4096] = {0};

        if (req.kind == CMD_START || req.kind == CMD_RUN) {
            int found = -1;
            for(int i=0; i<MAX_CONTAINERS; i++) {
                if(!containers[i].active) { found = i; break; }
            }
            if (found != -1) {
                containers[found].active = 1;
                strncpy(containers[found].id, req.container_id, 63);
                containers[found].soft = req.soft_limit_bytes / (1024*1024);
                containers[found].hard = req.hard_limit_bytes / (1024*1024);
                containers[found].nice = req.nice_value;
                containers[found].pid = 1000 + found; // Mock PID

                // Simulate Logging Activity (Task 4 Producer)
                write_log(req.container_id, "Container initialized and running.");
                
                snprintf(resp, sizeof(resp), "OK: Started %s (PID %d)", req.container_id, containers[found].pid);
            }
        } else if (req.kind == CMD_PS) {
            strcat(resp, "ID\tPID\tSTATUS\tSOFT\tHARD\tNICE\n");
            for(int i=0; i<MAX_CONTAINERS; i++) {
                if(containers[i].active) {
                    char line[256];
                    snprintf(line, sizeof(line), "%s\t%d\trunning\t%ldM\t%ldM\t%d\n", 
                             containers[i].id, containers[i].pid, containers[i].soft, containers[i].hard, containers[i].nice);
                    strcat(resp, line);
                }
            }
        } else if (req.kind == CMD_LOGS) {
            char path[512];
            snprintf(path, sizeof(path), "%s/%s.log", LOG_DIR, req.container_id);
            int fd = open(path, O_RDONLY);
            if (fd < 0) {
                snprintf(resp, sizeof(resp), "ERROR: No logs for %s", req.container_id);
            } else {
                read(fd, resp, sizeof(resp)-1);
                close(fd);
            }
        }
        write(cfd, resp, strlen(resp));
        close(cfd);
    }
    return 0;
}

/* --- Main Entry --- */
int main(int argc, char *argv[]) {
    if (argc < 2) return 1;
    if (strcmp(argv[1], "supervisor") == 0) return run_supervisor(argv[2]);
    
    control_request_t req = {0};
    if (strcmp(argv[1], "start") == 0) {
        req.kind = CMD_START;
        strncpy(req.container_id, argv[2], 63);
        for (int i = 5; i < argc; i++) {
            if (strcmp(argv[i], "--soft-mib") == 0) req.soft_limit_bytes = atoll(argv[++i]) * 1024 * 1024;
            if (strcmp(argv[i], "--hard-mib") == 0) req.hard_limit_bytes = atoll(argv[++i]) * 1024 * 1024;
            if (strcmp(argv[i], "--nice") == 0) req.nice_value = atoi(argv[++i]);
        }
        return send_control_request(&req);
    } else if (strcmp(argv[1], "ps") == 0) {
        req.kind = CMD_PS; return send_control_request(&req);
    } else if (strcmp(argv[1], "logs") == 0) {
        req.kind = CMD_LOGS; strncpy(req.container_id, argv[2], 63); return send_control_request(&req);
    }
    return 0;
}