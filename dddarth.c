#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <regex.h>

#define MAX_PATH 2048
#define RESULT_DIR "results"
#define MOUNT_POINT "/mnt/output_disk"

const char *default_block_sizes[] = {"32k", "64k", "128k", "256k", "512k", "1M", "4M", "16M"};
const size_t num_default_block_sizes = sizeof(default_block_sizes) / sizeof(default_block_sizes[0]);

char *copy_size = "1G";
char **block_sizes = NULL;
size_t num_block_sizes = 0;
char *input_file = "/dev/nvme0n1";
char *output_disk = "/dev/sda";

char best_block_size[10] = "";
double best_transfer_rate = 0;

void print_colored(const char *color_code, const char *format, ...) {
    va_list args;
    va_start(args, format);
    printf("%s", color_code);
    vprintf(format, args);
    printf("\033[0m");
    va_end(args);
}

void print_title() {
    print_colored("\033[1;34m", "==============================================================================\n");
    print_colored("\033[1;34m", ":::::::::  :::::::::  :::::::::      :::     ::::::::: ::::::::::: :::    ::: \n");
    print_colored("\033[1;34m", ":+:    :+: :+:    :+: :+:    :+:   :+: :+:   :+:    :+:    :+:     :+:    :+: \n");
    print_colored("\033[1;34m", "+:+    +:+ +:+    +:+ +:+    +:+  +:+   +:+  +:+    +:+    +:+     +:+    +:+ \n");
    print_colored("\033[1;34m", "+#+    +:+ +#+    +:+ +#+    +:+ +#++:++#++: +#++:++#:     +#+     +#++:++#++ \n");
    print_colored("\033[1;34m", "+#+    +#+ +#+    +#+ +#+    +#+ +#+     +#+ +#+    +#+    +#+     +#+    +#+ \n");
    print_colored("\033[1;34m", "#+#    #+# #+#    #+# #+#    #+# #+#     #+# #+#    #+#    #+#     #+#    #+# \n");
    print_colored("\033[1;34m", "#########  #########  #########  ###     ### ###    ###    ###     ###    ### \n");
    print_colored("\033[1;34m", "============================= v 0.1.0 - 19 Jul 24 ============================\n\n");
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

void check_root() {
    if (geteuid() != 0) {
        fprintf(stderr, "\033[1;31mError: This program must be run as root.\033[0m\n");
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
    print_colored("\033[1;33m", "Listing block devices:\n");
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
    struct stat st;

    print_colored("\033[1;34m", "Creating new GPT partition table on %s...\n", disk);
    unmount_existing_partitions(disk);

    snprintf(command, sizeof(command), "sudo parted %s --script mklabel gpt", disk);
    execute_command(command);

    snprintf(command, sizeof(command), "sudo parted %s --script mkpart primary ext4 0%% 100%%", disk);
    execute_command(command);

    sleep(1);

    char partition_path[MAX_PATH];
    snprintf(partition_path, sizeof(partition_path), "%s1", disk);
    if (stat(partition_path, &st) != 0) {
        fprintf(stderr, "Error: Partition %s was not created successfully. Error code: %d\n", partition_path, errno);
        exit(EXIT_FAILURE);
    }

    print_colored("\033[1;34m", "Creating Ext4 File System on %s...\n", partition_path);
    snprintf(command, sizeof(command), "sudo mkfs.ext4 -F %s > /dev/null 2>&1", partition_path);
    execute_command(command);

    if (stat(MOUNT_POINT, &st) == -1) {
        if (mkdir(MOUNT_POINT, 0700) != 0) {
            perror("mkdir");
            exit(EXIT_FAILURE);
        }
    }

    snprintf(command, sizeof(command), "sudo mount %s %s", partition_path, MOUNT_POINT);
    execute_command(command);

    snprintf(command, sizeof(command), "sudo chmod 777 %s", MOUNT_POINT);
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
    snprintf(output_file_path, sizeof(output_file_path), "%s/%s_%s_%s.dd", MOUNT_POINT, copy_size, block_size, timestamp);

    struct stat st = {0};
    if (stat(MOUNT_POINT, &st) == -1) {
        fprintf(stderr, "Error: Mount point %s does not exist.\n", MOUNT_POINT);
        exit(EXIT_FAILURE);
    }

    print_colored("\033[1;33m", "Output file path: %s\n", output_file_path);

    size_t block_size_bytes = parse_size(block_size);
    size_t copy_size_bytes = parse_size(copy_size);
    size_t count = copy_size_bytes / block_size_bytes;

    char dd_command[MAX_PATH * 2];
    snprintf(dd_command, sizeof(dd_command), "sudo dd if=%s of=%s bs=%s count=%zu status=progress 2>&1", input_file, output_file_path, block_size, count);

    print_colored("\033[1;33m", "Running dd with block size %s...\n", block_size);
    print_colored("\033[1;32m", "Executing: %s\n", dd_command);

    drop_caches();
    FILE *fp = popen(dd_command, "r");
    if (fp == NULL) {
        perror("popen");
        exit(EXIT_FAILURE);
    }

    char dd_output[2048];
    size_t dd_output_size = fread(dd_output, sizeof(char), sizeof(dd_output) - 1, fp);
    dd_output[dd_output_size] = '\0';
    pclose(fp);

    char result_file_path[MAX_PATH];
    snprintf(result_file_path, sizeof(result_file_path), "%s/dd_output_%s.txt", RESULT_DIR, block_size);

    if (stat(RESULT_DIR, &st) == -1) {
        if (mkdir(RESULT_DIR, 0700) != 0) {
            perror("mkdir");
            exit(EXIT_FAILURE);
        }
    }

    print_colored("\033[1;33m", "Result file path: %s\n", result_file_path);

    FILE *result_file = fopen(result_file_path, "w");
    if (result_file == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    fprintf(result_file, "%s", dd_output);
    fclose(result_file);

    double transfer_rate_value = parse_transfer_rate(dd_output);

    print_colored("\033[1;32m", "Transfer rate with block size %s: %.2f MB/s\n", block_size, transfer_rate_value);

    if (transfer_rate_value > best_transfer_rate) {
        strcpy(best_block_size, block_size);
        best_transfer_rate = transfer_rate_value;
    }
}

void benchmark_and_get_best_block_size() {
    best_transfer_rate = 0.0;
    best_block_size[0] = '\0';

    if (block_sizes == NULL) {
        block_sizes = (char **)default_block_sizes;
        num_block_sizes = num_default_block_sizes;
    }

    for (size_t i = 0; i < num_block_sizes; ++i) {
        run_dd(block_sizes[i]);
    }

    print_colored("\033[1;35m", "Best block size: %s\n", best_block_size);
    print_colored("\033[1;35m", "Best transfer rate: %.2f MB/s\n", best_transfer_rate);
}

void nvme_to_sdb_auto_rip() {
    prepare_disk("/dev/sdb");
    benchmark_and_get_best_block_size();

    if (strlen(best_block_size) == 0) {
        print_colored("\033[1;31m", "Error: No valid block size found. Aborting copy.\n");
        exit(EXIT_FAILURE);
    }

    char timestamp[32];
    time_t now = time(NULL);
    snprintf(timestamp, sizeof(timestamp), "%ld", now);

    char dd_command[MAX_PATH * 2];
    snprintf(dd_command, sizeof(dd_command), "dd if=/dev/nvme0n1 of=/mnt/output_disk/nvme0n1_sdb1_%ld.dd bs=%s status=progress", now, best_block_size);

    print_colored("\033[1;33m", "Running final dd command to copy from nvme0n1 to sdb...\n");
    print_colored("\033[1;32m", "Executing: %s\n", dd_command);
    execute_command(dd_command);
}

void nvme_to_sda_auto_rip() {
    prepare_disk("/dev/sda");
    benchmark_and_get_best_block_size();

    if (strlen(best_block_size) == 0) {
        print_colored("\033[1;31m", "Error: No valid block size found. Aborting copy.\n");
        exit(EXIT_FAILURE);
    }

    char timestamp[32];
    time_t now = time(NULL);
    snprintf(timestamp, sizeof(timestamp), "%ld", now);

    char dd_command[MAX_PATH * 2];
    snprintf(dd_command, sizeof(dd_command), "dd if=/dev/nvme0n1 of=/mnt/output_disk/nvme0n1_sda1_%ld.dd bs=%s status=progress", now, best_block_size);

    print_colored("\033[1;33m", "Running final dd command to copy from nvme0n1 to sda...\n");
    print_colored("\033[1;32m", "Executing: %s\n", dd_command);
    execute_command(dd_command);
}

void install_program() {
    char command[MAX_PATH];
    snprintf(command, sizeof(command), "sudo cp ./dddarth /usr/local/bin/dddarth");
    execute_command(command);
    print_colored("\033[1;32m", "Program installed to /usr/local/bin/dddarth\n");
}

void create_systemd_service(const char *input_drive, const char *output_drive) {
    input_file = strdup(input_drive);
    output_disk = strdup(output_drive);
    benchmark_and_get_best_block_size();

    if (strlen(best_block_size) == 0) {
        print_colored("\033[1;31m", "Error: No valid block size found. Aborting creation of systemd service.\n");
        exit(EXIT_FAILURE);
    }

    char command[MAX_PATH];
    char partuuid_output[MAX_PATH];
    char partuuid_input[MAX_PATH];

    // Get PARTUUID of the output drive
    snprintf(command, sizeof(command), "blkid -s PARTUUID -o value %s1", output_drive);
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        perror("popen");
        exit(EXIT_FAILURE);
    }
    if (fgets(partuuid_output, sizeof(partuuid_output), fp) == NULL) {
        perror("fgets");
        pclose(fp);
        exit(EXIT_FAILURE);
    }
    pclose(fp);
    partuuid_output[strcspn(partuuid_output, "\n")] = 0;

    // Get PARTUUID of the input drive
    snprintf(command, sizeof(command), "blkid -s PARTUUID -o value %s1", input_drive);
    fp = popen(command, "r");
    if (fp == NULL) {
        perror("popen");
        exit(EXIT_FAILURE);
    }
    if (fgets(partuuid_input, sizeof(partuuid_input), fp) == NULL) {
        perror("fgets");
        pclose(fp);
        exit(EXIT_FAILURE);
    }
    pclose(fp);
    partuuid_input[strcspn(partuuid_input, "\n")] = 0;

    char service_file[MAX_PATH];
    snprintf(service_file, sizeof(service_file), "/etc/systemd/system/dddarth.service");

    FILE *service = fopen(service_file, "w");
    if (service == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    fprintf(service,
            "[Unit]\n"
            "Description=DDDarth Service\n"
            "After=network.target\n\n"
            "[Service]\n"
            "Type=oneshot\n"
            "ExecStart=/bin/bash -c ' \\\n"
            "    timestamp=$(date +%%Y%%m%%d%%H%%M%%S); \\\n"
            "    mount_point=\"/mnt/dmx-extraction${timestamp}\"; \\\n"
            "    mkdir -p ${mount_point}; \\\n"
            "    mount PARTUUID=%s ${mount_point}; \\\n"
            "    output_file=\"${mount_point}/%s_%s_${timestamp}.dd\"; \\\n"
            "    dd if=%s of=${output_file} bs=%s status=progress' \n"
            "Restart=on-failure\n"
            "User=root\n"
            "Group=root\n\n"
            "[Install]\n"
            "WantedBy=multi-user.target\n",
            partuuid_output, partuuid_input, partuuid_output, input_file, best_block_size);

    fclose(service);

    execute_command("sudo systemctl daemon-reload");
    execute_command("sudo systemctl enable dddarth.service");

    print_colored("\033[1;32m", "Systemd service created and enabled to start on boot.\n");
}

void usage(const char *program_name) {
    print_title();
    

    printf("\033[1;33mOverview:\033[0m\n");
    printf("  This program benchmarks and performs data transfer from any drive to a specified output disk.\n");
    printf("  It supports automatic creation of a systemd service for scheduled data transfers.\n\n");

    printf("\033[1;33mPending Improvements:\033[0m\n");
    printf("  - Automatic Luks AES-256 with Argon2, which is a memory-hard function designed to resist GPU and ASIC attacks. \n");
    printf("  - Web Application & Cross-compile for arm64, Raspberry Pi, and other ARM-based systems.\n\n");

    printf("\033[1;33mDefault Benchmarking Behavior (when no options are specified):\033[0m\n");
    printf("  - Copy size: 1G\n");
    printf("  - Block sizes: 32k, 64k, 128k, 256k, 512k, 1M, 4M, 16M\n");
    printf("  - Input file/device: /dev/nvme0n1\n");
    printf("  - Output disk: /dev/sda\n\n");
    print_colored("\033[1;36m", "Usage: %s \033[0m\033[1;31m[options]\033[0m\n", program_name);
    printf("  ┌───────────────────────────────┬─────────────────────────────────────────────────────────────────────────────────────────────────────────┐\n");
    printf("  │ \033[1;31m-c --copy-size\033[0m                │ \033[1;37mSize of the data to copy (default: 1G)\033[0m\n");
    printf("  │                               │ Example: %s -c 2G                                                                                │\n", program_name);
    printf("  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤\n");
    printf("  │ \033[1;31m-b --block-sizes\033[0m              │ \033[1;37mComma-separated list of block sizes to test (default: 32k,64k,128k,256k,512k,1M,4M,16M)\033[0m\n");
    printf("  │                               │ Example: %s -b 64k,128k,256k                                                                     │\n", program_name);
    printf("  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤\n");
    printf("  │ \033[1;31m-i --input-file\033[0m               │ \033[1;37mInput file/device (default: /dev/nvme0n1)\033{0m\n");
    printf("  │                               │ Example: %s -i /dev/sda                                                                          │\n", program_name);
    printf("  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤\n");
    printf("  │ \033[1;31m-o --output-disk\033[0m              │ \033[1;37mOutput disk (default: /dev/sda)\033[0m\n");
    printf("  │                               │ Example: %s -o /dev/sdb                                                                          │\n", program_name);
    printf("  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤\n");
    printf("  │ \033[1;31m--nvme-to-sdb-auto-rip\033[0m        │ \033[1;37mRun benchmark and copy from nvme0n1 to sdb with best performance values.\033[0m\n");
    printf("  │                               │ Example: %s --nvme-to-sdb-auto-rip                                                               │\n", program_name);
    printf("  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤\n");
    printf("  │ \033[1;31m--nvme-to-sda-auto-rip\033[0m        │ \033[1;37mRun benchmark and copy from nvme0n1 to sda with best performance values.\033[0m\n");
    printf("  │                               │ Example: %s --nvme-to-sda-auto-rip                                                               │\n", program_name);
    printf("  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤\n");
    printf("  │ \033[1;31m--systemd-auto-rip\033[0m            │ \033[1;37mRun benchmark and create a systemd service to copy from source to target with best performance values.\033[0m\n");
    printf("  │                               │ Example: %s --systemd-auto-rip /dev/nvme0n1 /dev/sda                                             │\n", program_name);
    printf("  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤\n");
    printf("  │ \033[1;31m--install\033[0m                     │ \033[1;37mInstall the program to /usr/local/bin\033[0m\n");
    printf("  │                               │ Example: %s --install                                                                            │\n", program_name);
    printf("  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤\n");
    printf("  │ \033[1;31m--help\033[0m                        │ \033[1;37mDisplay this help and exit\033[0m\n");
    printf("  │                               │ Example: %s --help                                                                               │\n", program_name);
    printf("  └───────────────────────────────┴─────────────────────────────────────────────────────────────────────────────────────────────────────────┘\n");

    exit(1);
}




void parse_arguments(int argc, char **argv) {
    static struct option long_options[] = {
        {"copy-size", required_argument, 0, 'c'},
        {"block-sizes", required_argument, 0, 'b'},
        {"input-file", required_argument, 0, 'i'},
        {"output-disk", required_argument, 0, 'o'},
        {"nvme-to-sdb-auto-rip", no_argument, 0, 'r'},
        {"nvme-to-sda-auto-rip", no_argument, 0, 's'},
        {"systemd-auto-rip", required_argument, 0, 'a'},
        {"install", no_argument, 0, 'n'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt_index = 0;
    int c;
    while ((c = getopt_long(argc, argv, "c:b:i:o:rsa:nh", long_options, &opt_index)) != -1) {
        switch (c) {
            case 'c':
                copy_size = strdup(optarg);
                break;
            case 'b':
                // Logic to handle block sizes will go here
                break;
            case 'i':
                input_file = strdup(optarg);
                break;
            case 'o':
                output_disk = strdup(optarg);
                break;
            case 'r':
                nvme_to_sdb_auto_rip();
                exit(EXIT_SUCCESS);
            case 's':
                nvme_to_sda_auto_rip();
                exit(EXIT_SUCCESS);
            case 'a':
                create_systemd_service(optarg, argv[optind]);
                exit(EXIT_SUCCESS);
            case 'n':
                install_program();
                exit(EXIT_SUCCESS);
            case 'h':
                usage(argv[0]);
                exit(EXIT_SUCCESS);
            default:
                fprintf(stderr, "Error: Invalid option '%c'. Use --help for more information.\n", c);
                exit(EXIT_FAILURE);
        }
    }

    if (copy_size == NULL) {
        copy_size = strdup("1G");
    }
    if (block_sizes == NULL) {
        block_sizes = (char **)default_block_sizes;
        num_block_sizes = num_default_block_sizes;
    }

    if (input_file == NULL) {
        input_file = strdup("/dev/nvme0n1");
    }
    if (output_disk == NULL) {
        output_disk = strdup("/dev/sda");
    }
}

int main(int argc, char **argv) {
    check_root();
    parse_arguments(argc, argv);

    print_colored("\033[1;34m", "Copy size: %s\n", copy_size);
    print_colored("\033[1;34m", "Block sizes to be tested: ");
    for (size_t i = 0; i < num_block_sizes; ++i) {
        printf("%s ", block_sizes[i]);
    }
    printf("\033[0m\n");
    print_colored("\033[1;34m", "Input file: %s\n", input_file);
    print_colored("\033[1;34m", "Output disk: %s\n", output_disk);

    create_results_directory();
    print_colored("\033[1;34m", "Creating Benchmark Directory: Results\n");
    print_colored("\033[1;34m", "Creating Ext4 File System on %s\n", output_disk);
    prepare_disk(output_disk);
    print_colored("\033[1;34m", "Setup Complete...\n");

    benchmark_and_get_best_block_size();

    print_colored("\033[1;35m", "Best block size: %s\n", best_block_size);
    print_colored("\033[1;35m", "Best transfer rate: %.2f MB/s\n", best_transfer_rate);

    char umount_command[MAX_PATH];
    snprintf(umount_command, sizeof(umount_command), "sudo umount %s", MOUNT_POINT);
    execute_command(umount_command);

    if (block_sizes != (char **)default_block_sizes) {
        for (size_t i = 0; i < num_block_sizes; ++i) {
            free(block_sizes[i]);
        }
        free(block_sizes);
    }
    return 0;
}