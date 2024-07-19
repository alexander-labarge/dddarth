#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <regex.h>

#define MAX_PATH 1024
#define RESULT_DIR "results"
#define MOUNT_POINT "/mnt/output_disk"

const char *default_block_sizes[] = {"32k", "64k", "128k", "256k", "512k", "1M", "4M", "16M"};
const size_t num_default_block_sizes = sizeof(default_block_sizes) / sizeof(default_block_sizes[0]);

char *COPY_SIZE = "1G";
char **BLOCK_SIZES = NULL;
size_t num_block_sizes = 0;
char *INPUT_FILE = "/dev/nvme0n1";
char *OUTPUT_DISK = "/dev/sda";

char best_block_size[10] = "";
double best_transfer_rate = 0;

void print_title() {
    printf("==============================================================================\n\n");
    printf(":::::::::  :::::::::  :::::::::      :::     ::::::::: ::::::::::: :::    ::: \n");
    printf(":+:    :+: :+:    :+: :+:    :+:   :+: :+:   :+:    :+:    :+:     :+:    :+: \n");
    printf("+:+    +:+ +:+    +:+ +:+    +:+  +:+   +:+  +:+    +:+    +:+     +:+    +:+ \n");
    printf("+#+    +:+ +#+    +:+ +#+    +:+ +#++:++#++: +#++:++#:     +#+     +#++:++#++ \n");
    printf("+#+    +#+ +#+    +#+ +#+    +#+ +#+     +#+ +#+    +#+    +#+     +#+    +#+ \n");
    printf("#+#    #+# #+#    #+# #+#    #+# #+#     #+# #+#    #+#    #+#     #+#    #+# \n");
    printf("#########  #########  #########  ###     ### ###    ###    ###     ###    ### \n\n");
    printf("============================= v 0.1.0 - 19 Jul 24 ============================\n\n");
}

void execute_command(const char *command) {
    int ret = system(command);
    if (ret == -1) {
        perror("system");
        exit(EXIT_FAILURE);
    } else if (WIFEXITED(ret) && WEXITSTATUS(ret) != 0) {
        fprintf(stderr, "Command failed with exit status %d: %s\n", WEXITSTATUS(ret), command);
        exit(EXIT_FAILURE);
    }
}

void create_results_directory() {
    struct stat st = {0};
    if (stat(RESULT_DIR, &st) == -1) {
        if (mkdir(RESULT_DIR, 0700) != 0) {
            perror("mkdir");
            exit(EXIT_FAILURE);
        }
    } else {
        char rm_command[MAX_PATH];
        snprintf(rm_command, sizeof(rm_command), "rm -f %s/*", RESULT_DIR);
        execute_command(rm_command);
    }
}

void list_block_devices() {
    printf("\033[1;33mListing block devices:\033[0m\n");
    execute_command("lsblk");
}

void unmount_existing_partitions(const char *disk) {
    char command[MAX_PATH];
    snprintf(command, sizeof(command), "lsblk -ln -o NAME,MOUNTPOINT %s | awk '$2 {print $1}'", disk);
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        perror("popen");
        exit(EXIT_FAILURE);
    }

    char partition[128];
    while (fgets(partition, sizeof(partition), fp)) {
        partition[strcspn(partition, "\n")] = '\0'; // Remove newline character
        char umount_command[MAX_PATH];
        snprintf(umount_command, sizeof(umount_command), "sudo umount /dev/%s", partition);
        execute_command(umount_command);
    }
    pclose(fp);
}

void prepare_disk(const char *disk) {
    char command[MAX_PATH];

    printf("\033[1;34mCreating new GPT partition table on %s...\033[0m\n", disk);
    unmount_existing_partitions(disk);

    // Create new GPT partition table
    snprintf(command, sizeof(command), "sudo parted %s --script mklabel gpt", disk);
    execute_command(command);

    // Create primary partition
    snprintf(command, sizeof(command), "sudo parted %s --script mkpart primary ext4 0%% 100%%", disk);
    execute_command(command);

    sleep(1);  // Adding a short delay to ensure partition is created

    // Check if the partition was created successfully
    struct stat st;
    char partition_path[MAX_PATH];
    snprintf(partition_path, sizeof(partition_path), "%s1", disk);
    if (stat(partition_path, &st) != 0) {
        fprintf(stderr, "Error: Partition %s was not created successfully. Error code: %d\n", partition_path, errno);
        exit(EXIT_FAILURE);
    }

    printf("\033[1;34mCreating Ext4 File System on %s...\033[0m\n", partition_path);
    // Create ext4 filesystem and suppress output
    snprintf(command, sizeof(command), "sudo mkfs.ext4 -F %s > /dev/null 2>&1", partition_path);
    execute_command(command);

    // Create mount point and mount the partition
    snprintf(command, sizeof(command), "sudo mkdir -p %s", MOUNT_POINT);
    execute_command(command);
    snprintf(command, sizeof(command), "sudo mount %s %s", partition_path, MOUNT_POINT);
    execute_command(command);
}

void drop_caches() {
    execute_command("echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null");
    sync();
}

size_t parse_size(const char *size_str) {
    size_t size;
    char unit;
    sscanf(size_str, "%zu%c", &size, &unit);
    switch (unit) {
        case 'k':
        case 'K':
            size *= 1024;
            break;
        case 'M':
        case 'm':
            size *= 1024 * 1024;
            break;
        case 'G':
        case 'g':
            size *= 1024 * 1024 * 1024;
            break;
        default:
            break;
    }
    return size;
}

double parse_transfer_rate(const char *dd_output) {
    regex_t regex;
    regmatch_t matches[3];
    double transfer_rate_value = 0.0;
    char rate_str[16];
    const char *pattern = "([0-9]+\\.[0-9]+) (GB/s|MB/s)";
    
    if (regcomp(&regex, pattern, REG_EXTENDED) != 0) {
        perror("regcomp");
        exit(EXIT_FAILURE);
    }

    const char *cursor = dd_output;
    while (regexec(&regex, cursor, 3, matches, 0) == 0) {
        snprintf(rate_str, matches[1].rm_eo - matches[1].rm_so + 1, "%.*s", (int)(matches[1].rm_eo - matches[1].rm_so), cursor + matches[1].rm_so);
        transfer_rate_value = atof(rate_str);
        if (strncmp(cursor + matches[2].rm_so, "GB/s", 4) == 0) {
            transfer_rate_value *= 1024;
        }
        cursor += matches[0].rm_eo;
    }

    regfree(&regex);
    return transfer_rate_value;
}

void run_dd(const char *block_size) {
    char timestamp[32];
    time_t now = time(NULL);
    snprintf(timestamp, sizeof(timestamp), "%ld", now);

    char output_file_path[MAX_PATH];
    snprintf(output_file_path, sizeof(output_file_path), "%s/%s_%s_%s.dd", MOUNT_POINT, COPY_SIZE, block_size, timestamp);

    size_t block_size_bytes = parse_size(block_size);
    size_t copy_size_bytes = parse_size(COPY_SIZE);
    size_t count = copy_size_bytes / block_size_bytes;

    char dd_command[MAX_PATH * 2];
    snprintf(dd_command, sizeof(dd_command), "dd if=%s of=%s bs=%s count=%zu 2>&1", INPUT_FILE, output_file_path, block_size, count);

    printf("\033[1;33mRunning dd with block size %s...\033[0m\n", block_size);
    printf("\033[1;32mExecuting: %s\033[0m\n", dd_command);

    drop_caches();
    FILE *fp = popen(dd_command, "r");
    if (fp == NULL) {
        perror("popen");
        exit(EXIT_FAILURE);
    }

    char dd_output[2048];
    size_t dd_output_size = fread(dd_output, sizeof(char), sizeof(dd_output) - 1, fp);
    dd_output[dd_output_size] = '\0'; // Null-terminate the string
    pclose(fp);

    char result_file_path[MAX_PATH];
    snprintf(result_file_path, sizeof(result_file_path), "%s/dd_output_%s.txt", RESULT_DIR, block_size);

    FILE *result_file = fopen(result_file_path, "w");
    if (result_file == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    fprintf(result_file, "%s", dd_output);
    fclose(result_file);

    // Parse the transfer rate
    double transfer_rate_value = parse_transfer_rate(dd_output);

    printf("\033[1;32mTransfer rate with block size %s: %.2f MB/s\033[0m\n", block_size, transfer_rate_value);

    // Update the best block size and transfer rate
    if (transfer_rate_value > best_transfer_rate) {
        strcpy(best_block_size, block_size);
        best_transfer_rate = transfer_rate_value;
    }
}

void benchmark_and_get_best_block_size() {
    best_transfer_rate = 0.0;  // reset before next bench
    best_block_size[0] = '\0';

    // Use default block sizes if none are specified
    if (BLOCK_SIZES == NULL) {
        BLOCK_SIZES = (char **)default_block_sizes;
        num_block_sizes = num_default_block_sizes;
    }

    for (size_t i = 0; i < num_block_sizes; ++i) {
        run_dd(BLOCK_SIZES[i]);
    }

    printf("\033[1;35mBest block size: %s\033[0m\n", best_block_size);
    printf("\033[1;35mBest transfer rate: %.2f MB/s\033[0m\n", best_transfer_rate);
}

void nvme_to_sdb_auto_rip() {
    prepare_disk("/dev/sdb");
    benchmark_and_get_best_block_size();

    if (strlen(best_block_size) == 0) {
        fprintf(stderr, "\033[1;31mError: No valid block size found. Aborting copy.\033[0m\n");
        exit(EXIT_FAILURE);
    }

    char timestamp[32];
    time_t now = time(NULL);
    snprintf(timestamp, sizeof(timestamp), "%ld", now);

    char dd_command[MAX_PATH * 2];
    snprintf(dd_command, sizeof(dd_command), "dd if=/dev/nvme0n1 of=/mnt/output_disk/nvme0n1_sdb_%ld.dd bs=%s status=progress", now, best_block_size);

    printf("\033[1;33mRunning final dd command to copy from nvme0n1 to sdb...\033{0m\n");
    printf("\033[1;32mExecuting: %s\033[0m\n", dd_command);
    execute_command(dd_command);
}

void nvme_to_sda_auto_rip() {
    prepare_disk("/dev/sda");
    benchmark_and_get_best_block_size();

    if (strlen(best_block_size) == 0) {
        fprintf(stderr, "\033[1;31mError: No valid block size found. Aborting copy.\033[0m\n");
        exit(EXIT_FAILURE);
    }

    char timestamp[32];
    time_t now = time(NULL);
    snprintf(timestamp, sizeof(timestamp), "%ld", now);

    char dd_command[MAX_PATH * 2];
    snprintf(dd_command, sizeof(dd_command), "dd if=/dev/nvme0n1 of=/mnt/output_disk/nvme0n1_sda_%ld.dd bs=%s status=progress", now, best_block_size);

    printf("\033[1;33mRunning final dd command to copy from nvme0n1 to sda...\033[0m\n");
    printf("\033[1;32mExecuting: %s\033[0m\n", dd_command);
    execute_command(dd_command);
}

void install_program() {
    char command[MAX_PATH];
    snprintf(command, sizeof(command), "sudo cp ./dddarth /usr/local/bin/dddarth-amd64");
    execute_command(command);
    printf("\033[1;32mProgram installed to /usr/local/bin/dddarth\033[0m\n");
}

void create_systemd_service(const char *source_drive, const char *dest_drive) {
    INPUT_FILE = strdup(source_drive);
    OUTPUT_DISK = strdup(dest_drive);
    benchmark_and_get_best_block_size();

    if (strlen(best_block_size) == 0) {
        fprintf(stderr, "\033[1;31mError: No valid block size found. Aborting creation of systemd service.\033[0m\n");
        exit(EXIT_FAILURE);
    }

    char service_file[MAX_PATH];
    snprintf(service_file, sizeof(service_file), "/etc/systemd/system/dddarth.service");

    FILE *service = fopen(service_file, "w");
    if (service == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    // TODO: need to fix the hardcoding of of= -- the parameter is coming through as  of=-o
    fprintf(service,
            "[Unit]\n"
            "Description=DDDarth Auto Rip Service\n"
            "After=network.target\n\n"
            "[Service]\n"
            "ExecStart=/usr/bin/dd if=%s of=/dev/sdb bs=%s status=progress\n"
            "Restart=on-failure\n\n"
            "[Install]\n"
            "WantedBy=multi-user.target\n",
            INPUT_FILE, best_block_size);

    fclose(service);

    execute_command("sudo systemctl daemon-reload");
    execute_command("sudo systemctl enable dddarth.service");

    printf("\033[1;32mSystemd service created and enabled to start on boot.\033[0m\n");
}

void usage(const char *program_name) {
    fprintf(stderr, "\033[1;36mUsage: %s [-c COPY_SIZE] [-b BLOCK_SIZES] [-i INPUT_FILE] [-o OUTPUT_DISK] [--nvme-to-sdb-auto-rip] [--nvme-to-sda-auto-rip] [--systemd-auto-rip source_drive destination_drive] [--install] [--help]\033[0m\n", program_name);
    printf("\n");
    fprintf(stderr, "  \033[1;31m-c COPY_SIZE\033[0m            \033[1;37mSize of the data to copy (default: 1G)\033[0m\n");
    fprintf(stderr, "  \033[1;31m-b BLOCK_SIZES\033[0m          \033[1;37mComma-separated list of block sizes to test (default: 32k,64k,128k,256k,512k,1M,4M,16M)\033[0m\n");
    fprintf(stderr, "  \033[1;31m-i INPUT_FILE\033[0m           \033[1;37mInput file/device (default: /dev/nvme0n1)\033{0m\n");
    fprintf(stderr, "  \033[1;31m-o OUTPUT_DISK\033[0m          \033[1;37mOutput disk (default: /dev/sda)\033[0m\n");
    fprintf(stderr, "  \033[1;31m--nvme-to-sdb-auto-rip\033[0m  \033[1;37mRun benchmark and copy from nvme0n1 to sdb with best performance values.\033[0m\n");
    fprintf(stderr, "  \033[1;31m--nvme-to-sda-auto-rip\033[0m  \033[1;37mRun benchmark and copy from nvme0n1 to sda with best performance values.\033[0m\n");
    fprintf(stderr, "  \033[1;31m--systemd-auto-rip\033[0m      \033[1;37mRun benchmark and create a systemd service to copy from source to destination with best performance values.\033[0m\n");
    fprintf(stderr, "  \033[1;31m--install\033[0m               \033[1;37mInstall the program to /usr/local/bin\033[0m\n");
    fprintf(stderr, "  \033[1;31m--help\033[0m                  \033[1;37mDisplay this help and exit\033[0m\n");
    exit(1);
}

void parse_arguments(int argc, char **argv) {
    static struct option long_options[] = {
        {"nvme-to-sdb-auto-rip", no_argument, 0, 'r'},
        {"nvme-to-sda-auto-rip", no_argument, 0, 's'},
        {"systemd-auto-rip", required_argument, 0, 'a'},
        {"install", no_argument, 0, 'i'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:b:i:o:rsha", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                COPY_SIZE = optarg;
                break;
            case 'b': {
                char *token;
                char *rest = optarg;
                size_t count = 0;
                while ((token = strtok_r(rest, ",", &rest))) {
                    count++;
                }
                BLOCK_SIZES = malloc(count * sizeof(char*)); // Allocate memory for BLOCK_SIZES array
                if (!BLOCK_SIZES) {
                    perror("malloc");
                    exit(EXIT_FAILURE);
                }
                rest = optarg;
                num_block_sizes = 0;
                while ((token = strtok_r(rest, ",", &rest))) {
                    BLOCK_SIZES[num_block_sizes++] = strdup(token); // Duplicate token to ensure it is not overwritten
                }
                break;
            }
            case 'i':
                INPUT_FILE = optarg;
                break;
            case 'o':
                OUTPUT_DISK = optarg;
                break;
            case 'r':
                nvme_to_sdb_auto_rip();
                exit(0);
            case 's':
                nvme_to_sda_auto_rip();
                exit(0);
            case 'a':
                if (optind + 1 >= argc) {
                    fprintf(stderr, "\033[1;31mError: --systemd-auto-rip requires source and destination drives as arguments.\033[0m\n");
                    usage(argv[0]);
                }
                create_systemd_service(argv[optind], argv[optind + 1]);
                exit(0);
            case 'h':
                print_title();
            default:
                usage(argv[0]);
        }
    }

    if (num_block_sizes == 0) {
        BLOCK_SIZES = (char **)default_block_sizes;
        num_block_sizes = num_default_block_sizes;
    }
}

int main(int argc, char **argv) {
    parse_arguments(argc, argv);

    printf("\033[1;34mCopy size: %s\033[0m\n", COPY_SIZE);
    printf("\033[1;34mBlock sizes to be tested: ");
    for (size_t i = 0; i < num_block_sizes; ++i) {
        printf("%s ", BLOCK_SIZES[i]);
    }
    printf("\033[0m\n");
    printf("\033[1;34mInput file: %s\033[0m\n", INPUT_FILE);
    printf("\033[1;34mOutput disk: %s\033[0m\n", OUTPUT_DISK);

    create_results_directory();
    printf("\033[1;34mCreating Benchmark Directory: Results\033[0m\n");
    printf("\033[1;34mCreating Ext4 File System on %s\033[0m\n", OUTPUT_DISK);
    prepare_disk(OUTPUT_DISK);
    printf("\033[1;34mSetup Complete...\033[0m\n");

    benchmark_and_get_best_block_size();

    printf("\033[1;35mBest block size: %s\033[0m\n", best_block_size);
    printf("\033[1;35mBest transfer rate: %.2f MB/s\033[0m\n", best_transfer_rate);

    char umount_command[MAX_PATH];
    snprintf(umount_command, sizeof(umount_command), "sudo umount %s", MOUNT_POINT);
    execute_command(umount_command);

    if (BLOCK_SIZES != (char **)default_block_sizes) {
        for (size_t i = 0; i < num_block_sizes; ++i) {
            free(BLOCK_SIZES[i]);
        }
        free(BLOCK_SIZES);
    }
    return 0;
}
