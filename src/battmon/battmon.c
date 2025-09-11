#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <string.h>

#define SLEEP_INTERVAL 30
#define BATTERY_CAPACITY   "/sys/class/power_supply/axp2202-battery/capacity"
#define BATTERY_STATUS     "/sys/class/power_supply/axp2202-battery/status"
#define LED_TEST           "/sys/class/power_supply/axp2202-battery/led_test"
#define LOWPWR_LED         "/sys/class/power_supply/axp2202-battery/lowpwr_led"
#define WORK_LED           "/sys/class/power_supply/axp2202-battery/work_led"
#define SYSRQ_TRIGGER      "/proc/sysrq-trigger"
#define LOGFILE            "/var/log/batt.log"

void write_file(const char *path, const char *value) {
    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if (fd >= 0) {
        write(fd, value, strlen(value));
        close(fd);
    }
}

int read_int_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char buf[16];
    int len = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (len <= 0) return -1;
    buf[len] = 0;
    return atoi(buf);
}

int flag_exists(const char *flag) {
    return access(flag, F_OK) == 0;
}

void set_flag(const char *flag) {
    int fd = open(flag, O_WRONLY | O_CREAT, 0644);
    if (fd >= 0) close(fd);
}

void clear_flags(void) {
    unlink("/tmp/lowbattery10");
    unlink("/tmp/lowbattery5");
}

void log_message(const char *msg) {
    FILE *f = fopen(LOGFILE, "a");
    if (f) {
        fprintf(f, "%s\n", msg);
        fclose(f);
    }
}

void vibrate() {
    system("rumble-test");
    usleep(300000);
}

void kill_mnt_processes(int sig) {
    FILE *fp = popen("ps -eo pid,cmd", "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        int pid;
        char cmd[220];
        if (sscanf(line, " %d %219[^\n]", &pid, cmd) == 2) {
            if (strstr(cmd, "/mnt/")) {
                kill(pid, sig);
            }
        }
    }
    pclose(fp);
}

void safe_shutdown() {
    log_message("Battery at 1% - safe shutdown");
    kill_mnt_processes(SIGTERM);
    sync();
    sleep(5);
    kill_mnt_processes(SIGKILL);
    sync();
    sleep(5);
    system("swapoff -a");
    system("umount /mnt/vendor");
    system("umount /mnt/data");
    system("umount /mnt/mmc");
    sync();
    sleep(5);
    system("/usr/sbin/poweroff");
    exit(0);
}

int main() {
    clear_flags();

    while (1) {
        int capacity = read_int_file(BATTERY_CAPACITY);
        FILE *fstatus = fopen(BATTERY_STATUS, "r");
        char status[32] = {0};
        if (fstatus) {
            fgets(status, sizeof(status), fstatus);
            fclose(fstatus);
            status[strcspn(status, "\n")] = 0;
        }

        // Modo: cargando
        if (strcmp(status, "Charging") == 0) {
            clear_flags();
            write_file(LOWPWR_LED, "0");
            write_file(LED_TEST, "0");
            write_file(WORK_LED, "1");
        }
        // Modo: batería baja 10%
        else if (capacity <= 10 && capacity > 5 && !flag_exists("/tmp/lowbattery10") && strcmp(status, "Discharging") == 0) {
            log_message("Battery low (10%)");
            vibrate(); vibrate(); vibrate();
            write_file(WORK_LED, "0");
            write_file(LED_TEST, "0");
            write_file(LOWPWR_LED, "1");
            set_flag("/tmp/lowbattery10");
        }
        // Modo: batería crítica 5%
        else if (capacity <= 5 && capacity > 1 && !flag_exists("/tmp/lowbattery5") && strcmp(status, "Discharging") == 0) {
            log_message("Battery low (5%)");
            vibrate(); vibrate(); vibrate();
            write_file(WORK_LED, "0");
            write_file(LOWPWR_LED, "0");
            write_file(LED_TEST, "1");
            set_flag("/tmp/lowbattery5");
        }
        // Modo: apagado seguro
        else if (capacity <= 1 && strcmp(status, "Discharging") == 0) {
            vibrate(); vibrate(); vibrate();
            safe_shutdown();
        }
        sleep(SLEEP_INTERVAL);
    }
    return 0;
}
