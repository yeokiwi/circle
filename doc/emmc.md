# eMMC support for the Compute Module 5

This document describes the changes on the branch `claude/cm5-emmc-reading-vap6uj`
with respect to the `main` branch. The goal was to read the files on the
on-board eMMC memory of a Raspberry Pi Compute Module 5 (CM5) from a bare metal
application.

> Status: the code builds for all supported configurations, but it has **not**
> been tested on real hardware yet.

## Background

The class `CEMMCDevice` in [addon/SDCard](../addon/SDCard) already had a device
selector `EmbeddedMMC` and an own command table `emmc_commands`, which were
added for the Compute Module 3+ and 4 (system option `USE_EMBEDDED_MMC_CM`).
This code path had never been used on the BCM2712 SoC and several steps of the
device identification were still using the SD card protocol instead of the MMC
protocol.

On the CM5 the eMMC memory is connected to the same SDHCI controller, which
drives the SD card on the Raspberry Pi 5 (`sdio1` at `0x1000FFF000`, which is
`ARM_EMMC_BASE` for `RASPPI >= 5`). The relevant properties from the Raspberry
Pi Linux device tree (`bcm2712-rpi-cm5.dtsi`) are:

| Property        | Value            | Consequence for the driver               |
|-----------------|------------------|------------------------------------------|
| `bus-width`     | 8                | The device supports an 8-bit data bus    |
| `broken-cd`     | (set)            | The card detect signal is not connected  |
| `clocks`        | `clk_emmc2`      | 200 MHz base clock                       |
| `vqmmc-supply`  | `sd_io_1v8_reg`  | The I/O voltage is switchable (see below)|

The FAT partition of the eMMC memory is accessed with the FatFs file system
(addon/fatfs) on top of this driver, as it is done for an SD card.

## Changes in addon/SDCard

### Device selection at run-time

`CEMMCDevice::GetDefaultDeviceForMachine()` returns `EmbeddedMMC`, if the
machine model is `MachineModelCM5`, and `DefaultDevice` otherwise. This allows
to use the same kernel image on a Raspberry Pi 5 with SD card and on a CM5 with
on-board eMMC memory. The system option `USE_EMBEDDED_MMC_CM` is still the way
to select the eMMC memory on the Compute Module 3+ and 4 and can be used to
force the eMMC path on any model.

### Card detect

The eMMC memory of a Compute Module is not removable and its card detect signal
is not connected. `CardReset()` failed with the message `no card inserted`,
because the "Card Inserted" bit (bit 16) in the `EMMC_STATUS` register was not
set. This bit is ignored for the `EmbeddedMMC` device now.

### Device identification

| Step  | Before                                      | Now                                                                 |
|-------|---------------------------------------------|---------------------------------------------------------------------|
| CMD1  | Argument `0x00FF8000` plus the SD card bits S18R (24) and XPC (28), which are reserved on MMC. Endless busy loop. | Argument `0x40FF8000` (sector mode and voltage window). The access mode in the OCR selects the sector addressing. The busy loop is bounded to about two seconds. |
| CMD3  | `SEND_RELATIVE_ADDR` with argument 0, RCA taken from the response (R6). On MMC the response is the card status, so the RCA became 0, which *deselects* the device. | `SET_RELATIVE_ADDR` assigns RCA 1 in the argument and evaluates the R1 status. `emmc_commands[3]` uses `SD_RESP_R1`. |
| CMD9  | Not sent for `EmbeddedMMC`, `m_capacity` stayed `(u64) -1`. | The CSD is read. For devices up to 2 GByte the capacity is computed from `C_SIZE`, `C_SIZE_MULT` and `READ_BL_LEN`. |
| CMD8  | `SEND_IF_COND` (SD card only).              | `emmc_commands[8]` is `SEND_EXT_CSD` (R1 with data read). The 512 bytes EXT_CSD register is read in the transfer state. |
| CMD6  | `SD_RESP_R1`, result not checked.           | `SD_RESP_R1b`, the result is verified with CMD13 (including the `SWITCH_ERROR` status bit). |

`GetSize()` returns a valid capacity now, which is taken from `SEC_COUNT`
(EXT_CSD bytes 212-215) for devices greater than 2 GByte. Before it returned
`(u64) -1`, which lets `disk_ioctl(GET_SECTOR_COUNT)` in the FatFs driver fail
with `RES_PARERR`.

### Data bus width and speed

The data bus width is set with CMD6 (EXT_CSD byte `BUS_WIDTH`) and in the
`EMMC_CONTROL0` register of the host controller. Because both have to agree,
the EXT_CSD register is read back over the data lines afterwards and the
setting is only accepted, if the device reports the expected value. On failure
the next narrower bus is tried (8, 4, 1 bit).

If the `CARD_TYPE` field of the EXT_CSD register announces the 52 MHz high
speed mode, `HS_TIMING` is set to 1 and the clock is switched to 50 MHz,
otherwise 25 MHz are used. The mode is checked with CMD13 after the switch and
the initialization fails, if the device does not answer any more. Undefine
`MMC_HIGH_SPEED` in *emmc.cpp* to always use 25 MHz.

### New private methods

| Method                | Purpose                                                     |
|-----------------------|-------------------------------------------------------------|
| `MMCWaitReady()`      | Poll CMD13 until the device is ready in the transfer state  |
| `MMCSwitch()`         | Write one byte into the EXT_CSD register (CMD6)             |
| `MMCReadExtCSD()`     | Read the 512 bytes EXT_CSD register (CMD8)                  |
| `MMCSetBusWidth()`    | Switch device and host controller and verify the bus width  |
| `MMCSetupCapacity()`  | Determine the capacity from `SEC_COUNT`                     |

They are only available, if `USE_SDHOST` is not defined. All changes are
limited to the `EmbeddedMMC` device path and to the `emmc_commands` table, so
the behavior for SD cards is unchanged on all models.

## Changes in lib/fs

`CPartitionManager` writes the recognized partitions to the system log now:

```
partm: emmc1-1: type 0x0C, first sector 8192, 512 MBytes
partm: emmc1-2: type 0x83, first sector 1056768, 29184 MBytes
```

Partitions, which are not supported (extended, EFI), are logged with the
severity `LogDebug`. This makes the layout of the eMMC memory visible, without
enabling the debug output of the driver.

## Changes in sample/45-gpiowebcontrol

* The `CEMMCDevice` object is created with
  `CEMMCDevice::GetDefaultDeviceForMachine()`, so the sample uses the on-board
  eMMC memory on a CM5 and the SD card on all other models. The mounted device
  is written to the system log.
* The file manager is not limited to the root directory any more. `ListFiles()`
  takes a directory path, returns the sub-directories too (marked with
  `bDirectory` and sorted to the top) and `ReadFile()`, `WriteFile()` and
  `DeleteFile()` accept a path relative to the root directory.
* `IsValidPath()` validates such a path. It rejects absolute paths, drive
  prefixes, empty path components and `.` or `..`, so the file manager cannot
  escape from the mounted volume. `IsValidName()` (one path component) is still
  used for uploaded file names.
* The web page shows the current directory with an *Up* button, lists
  sub-directories as links and applies uploads, downloads and deletions to the
  directory, which is currently shown.

## Configurations, which have been built

All builds have been done with `AARCH = 64` and the toolchain prefix
`aarch64-linux-gnu-`:

| Configuration                           | What it covers                        |
|-----------------------------------------|---------------------------------------|
| `RASPPI = 5`                            | CM5 (full build including the sample) |
| `RASPPI = 4`                            | SD card via EMMC2 (default device)    |
| `RASPPI = 3`                            | `USE_SDHOST` path                     |
| `RASPPI = 3/4` + `USE_EMBEDDED_MMC_CM`  | CM3+ and CM4 eMMC path                |

## How to test it on a CM5

1. Build the libraries and the sample:

   ```
   ./makeall
   make -C addon/SDCard
   make -C addon/fatfs
   make -C sample/45-gpiowebcontrol
   ```

2. Put the CM5 into the USB boot mode (jumper "nRPI_BOOT" on the IO board) and
   attach its eMMC memory as a USB mass-storage device to your PC using the
   tool `rpiboot` from [usbboot](https://github.com/raspberrypi/usbboot).

3. Copy the firmware files (see [boot/](../boot)), *config64.txt* (renamed to
   *config.txt*) and *kernel_2712.img* to the FAT partition ("bootfs") of the
   eMMC memory.

4. Boot the CM5 and check the system log (serial on GPIO14/15, 115200 Baud, or
   HDMI). Define `EMMC_DEBUG2` in *emmc.cpp* to get the detailed command trace.
   These messages are expected:

   ```
   emmc: Found a valid eMMC chip (8 bit, 50 MHz)
   partm: emmc1-1: type 0x0C, first sector 8192, ... MBytes
   ```

5. Open `http://<ip-address>/` in a web browser and check the file listing of
   the boot partition, download a file (e.g. *config.txt*), upload a file and
   delete it again.

## Known limitations

* Only FAT partitions can be read. A Raspberry Pi OS installation has its root
  file system on an ext4 partition. It is listed by the partition manager (as
  `emmc1-2`) and can be read as a block device, but Circle has no ext4 driver,
  so its files are not accessible.
* Only MBR partition tables are supported (which is, what the Raspberry Pi
  Imager writes). GPT is not supported by `CPartitionManager`.
* The HS200 and HS400 modes of the CM5 eMMC memory are not used. They require
  1.8V I/O signaling and tuning.
* The I/O voltage regulator of the SDHCI controller (`sd_io_1v8_reg`, which is
  connected to the `gio_aon` GPIO bank of the BCM2712) is not controlled by
  Circle. Should the identification fail with a timeout on CMD1, although the
  eMMC path is used, this would be the first thing to check, because the
  firmware may have left the I/O voltage at 1.8V.
