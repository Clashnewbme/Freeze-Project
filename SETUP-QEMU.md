<img width="512" height="308" alt="freezelogo" src="https://github.com/user-attachments/assets/4a98902f-2bc1-4cbb-b313-8a92d6b22fee" />

This will guide explains how to build and run the Freeze OS project using **QEMU**.

---

## 1. Install Required Packages

Open qemu and run:

```bash
sudo apt update
sudo apt install build-essential grub-pc-bin grub-common xorriso qemu-system-x86 dosfstools mtools -y
```

## 2. Build
To build:
```bash
make
```
To clean:
```bash
make clean
```
Run:
```bash
make run
```
To do everything together:
```bash
make clean && make && make run
```

if you want you may check for the iso file:
```bash
ls
```

> Thanks for listening
