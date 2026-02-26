# Freeze Project guide
### ⚠️ WARNING:
Freeze OS is experimental. Installing incorrectly can wipe your data.
Always back up your files first.

### What You Need for this
A USB drive (at least 1GB)
Another computer to prepare the USB
Your freeze.iso file (from GitHub)

### Step 1 Download the Freeze Project
Go to the repo:
  https://github.com/Clashnewbme/Freeze-Project
  
If you wanna download or build:
make

Make sure you have:
freeze.iso

### Step 2 Create a Bootable USB

Windows is (Easiest)
Use Rufus:
Insert USB drive

1. Open Rufus
2. Select your USB
3. Click SELECT --> choose freeze.iso
4. Partition scheme:
5. MBR (for older BIOS systems)
6. Click START


### macOS

Find your USB:
diskutil list

Unmount it:
diskutil unmountDisk /dev/diskX

Write ISO:
sudo dd if=freeze.iso of=/dev/rdiskX bs=4m

Wait until it finishes

### Linux (Preferably Ubuntu but any is fine)

sudo dd if=freeze.iso of=/dev/sdX bs=4M status=progress
sync

### Step 3 Boot
Insert the USB into your target computer
Restart

Open boot menu:
F12 / ESC / DEL / F2 (depends on your PC)
Select your USB drive
Freeze Project should boot

# IMPORTANT (Most Common Issue)
If it doesn't boot, it’s usually because of UEFI vs BIOS.
Fix:

Enter BIOS settings and:

Disable Secure Boot

Enable Legacy Boot / CSM

Set boot mode to Legacy / BIOS

The Freeze Project (like most projects) usually only supports BIOS / Legacy.

# Extra

If you want more information check out the Freeze Project Website (https://freeze-project.vercel.app)
