## README

# DDDarth: Automated Performance Oriented DD

![image](https://github.com/user-attachments/assets/28e16aad-0116-418d-a1a7-7bcf6670bf0e)

![image](https://github.com/user-attachments/assets/403b93b6-4c43-4a0f-b691-159749ec4ab3)

### Overview

This program benchmarks and performs data transfer from any drive to a specified output disk. It supports automatic creation of a systemd service for scheduled data transfers. The program is highly configurable, allowing the user to specify various options such as copy size, block sizes, input file, and output disk.

### Features

- Benchmarks multiple block sizes to find the best performance.
- Performs data transfer using the `dd` command.
- Creates systemd services for automated data transfers.
- Coming Soon - Supports Luks AES-256 encryption with Argon2 (memory-hard function designed to resist GPU and ASIC attacks) in future releases.
- Coming Soon - Compatible with arm64, Raspberry Pi, and other ARM-based systems for cross-compilation.

### Default Configuration

- **Copy size**: 1G
- **Block sizes**: 32k, 64k, 128k, 256k, 512k, 1M, 4M, 16M
- **Input file/device**: /dev/nvme0n1
- **Output disk**: /dev/sda

### Installation

To install the program, run:
```sh
sudo ./dddarth --install
```

### Usage

The program supports various command-line options:

```sh
Usage: ./dddarth [options]
  ┌───────────────────────────────┬─────────────────────────────────────────────────────────────────────────────────────────────────────────┐
  │ -c --copy-size                │ Size of the data to copy (default: 1G)                                                                  │
  │                               │ Example: ./dddarth -c 2G                                                                                │
  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ -b --block-sizes              │ Comma-separated list of block sizes to test (default: 32k,64k,128k,256k,512k,1M,4M,16M)                 │
  │                               │ Example: ./dddarth -b 64k,128k,256k                                                                     │
  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ -i --input-file               │ Input file/device (default: /dev/nvme0n1)                                                               │
  │                               │ Example: ./dddarth -i /dev/sda                                                                          │
  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ -o --output-disk              │ Output disk (default: /dev/sdb)                                                                         │
  │                               │ Example: ./dddarth -o /dev/sdb                                                                          │
  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ --nvme-to-sdb-auto-rip        │ Run benchmark and copy from nvme0n1 to sdb with best performance values.                                │
  │                               │ Example: ./dddarth --nvme-to-sdb-auto-rip                                                               │
  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ --nvme-to-sda-auto-rip        │ Run benchmark and copy from nvme0n1 to sda with best performance values.                                │
  │                               │ Example: ./dddarth --nvme-to-sda-auto-rip                                                               │
  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ --systemd-auto-rip            │ Run benchmark and create a systemd service to copy from source to target with best performance values.  │
  │                               │ Example: ./dddarth --systemd-auto-rip /dev/nvme0n1 /dev/sdb                                             │
  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ --install                     │ Install the program to /usr/local/bin                                                                   │
  │                               │ Example: ./dddarth --install                                                                            │
  ├───────────────────────────────┼─────────────────────────────────────────────────────────────────────────────────────────────────────────┤
  │ --help                        │ Display this help and exit                                                                              │
  │                               │ Example: ./dddarth --help                                                                               │
  └───────────────────────────────┴─────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

### Example Commands

1. **Run benchmark and copy from nvme0n1 to sdb**:
    ```sh
    sudo ./dddarth --nvme-to-sdb-auto-rip
    ```

2. **Run benchmark and copy from nvme0n1 to sda**:
    ```sh
    sudo ./dddarth --nvme-to-sda-auto-rip
    ```

3. **Create a systemd service for automated data transfer**:
    ```sh
    sudo ./dddarth --systemd-auto-rip /dev/nvme0n1 /dev/sdb
    ```

4. **Install the program**:
    ```sh
    sudo ./dddarth --install
    ```

### Dependencies

The program requires the following libraries and tools:

- `stdio.h`
- `stdlib.h`
- `string.h`
- `unistd.h`
- `sys/stat.h`
- `sys/mount.h`
- `sys/types.h`
- `fcntl.h`
- `time.h`
- `stdarg.h`
- `errno.h`
- `getopt.h`
- `libgen.h`
- `regex.h`
- `pwd.h`
- `grp.h`

### Build

To build the program, use the following command:
```sh
gcc -o dddarth dddarth.c
```

### License

This program is licensed under the MIT License.

### Contributions

Contributions are welcome! Please submit a pull request or open an issue for any improvements or bug fixes.