# DDDarth: Automated Disk Benchmarking and Copying Tool

![image](https://github.com/user-attachments/assets/8f101f39-14a8-4132-8fb5-008ce4e0e593)

## Overview

DDDarth is a command-line tool designed to automate the process of benchmarking disk performance and then using the optimal block size to copy data from one disk to another. The tool supports various operations, including copying data from NVMe to other NVMes & SATA disks and setting up systemd services to perform these operations on boot.

## Features

- Automated disk benchmarking with multiple block sizes
- Optimal block size determination for best performance
- Disk preparation with GPT partitioning and ext4 filesystem creation
- Copying data from NVMe to other NVMes & SATA disks
- Systemd service creation for automated disk copying on boot
- Installation to `/usr/local/bin`

## Usage

```shell
Usage: ./dddarth [-c COPY_SIZE] [-b BLOCK_SIZES] [-i INPUT_FILE] [-o OUTPUT_DISK] [--nvme-to-sdb-auto-rip] [--nvme-to-sda-auto-rip] [--systemd-auto-rip source_drive destination_drive] [--install] [--help]

  -c COPY_SIZE            Size of the data to copy (default: 1G)
  -b BLOCK_SIZES          Comma-separated list of block sizes to test (default: 32k,64k,128k,256k,512k,1M,4M,16M)
  -i INPUT_FILE           Input file/device (default: /dev/nvme0n1)
  -o OUTPUT_DISK          Output disk (default: /dev/sda)
  --nvme-to-sdb-auto-rip  Run benchmark and copy from nvme0n1 to sdb with best performance values.
  --nvme-to-sda-auto-rip  Run benchmark and copy from nvme0n1 to sda with best performance values.
  --systemd-auto-rip      Run benchmark and create a systemd service to copy from source to destination with best performance values.
  --install               Install the program to /usr/local/bin
  --help                  Display this help and exit
```

## Compilation

To compile the program, use the following command:

```shell
gcc ./dddarth.c -o dddarth
```

## Commands and Options

### `--nvme-to-sdb-auto-rip`

This command benchmarks disk performance with various block sizes and then copies data from the NVMe disk (`/dev/nvme0n1`) to the SATA disk (`/dev/sdb`) using the optimal block size.

```shell
sudo ./dddarth --nvme-to-sdb-auto-rip
```

### `--nvme-to-sda-auto-rip`

Similar to the `--nvme-to-sdb-auto-rip` command, this option benchmarks disk performance and then copies data from the NVMe disk (`/dev/nvme0n1`) to the SATA disk (`/dev/sda`) using the optimal block size.

```shell
sudo ./dddarth --nvme-to-sda-auto-rip
```

### `--systemd-auto-rip source_drive destination_drive`

This command benchmarks disk performance and creates a systemd service that will copy data from the specified source drive to the specified destination drive on system boot using the optimal block size.

```shell
sudo ./dddarth --systemd-auto-rip /dev/nvme0n1 /dev/sda
```

### `--install`

Installs the `dddarth` program to `/usr/local/bin` for easy access.

```shell
sudo ./dddarth --install
```

## Example

Here's an example workflow using DDDarth:

1. **Compile the program:**

   ```shell
   gcc ./dddarth.c -o dddarth
   ```

2. **Install the program:**

   ```shell
   sudo ./dddarth --install
   ```

3. **Run a benchmark and copy from NVMe to SATA disk:**

   ```shell
   sudo ./dddarth --nvme-to-sda-auto-rip
   ```

4. **Create a systemd service to perform the copy on boot:**

   ```shell
   sudo ./dddarth --systemd-auto-rip /dev/nvme0n1 /dev/sda
   ```

5. **Verify the systemd service:**

   ```shell
   sudo systemctl status dddarth.service
   ```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
