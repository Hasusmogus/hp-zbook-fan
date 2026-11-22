# hp-zbook-fan

Linux hwmon driver for the fans on the **HP ZBook Studio G8** (board ID `8873`). It reports fan RPM and lets you switch the EC fan mode.

Stock Linux shows nothing useful here. `hp-wmi` exposes only `pwm1_enable`, and the firmware rejects every value with `EINVAL`. No fan RPM is visible at all.

## What it provides

`sensors` (lm_sensors) and other hwmon tools pick up these files:

| sysfs file | meaning |
|---|---|
| `fan1_input`, `fan2_input` | current RPM (CPU fan, GPU fan) |
| `fan1_target`, `fan2_target` | RPM target the EC is currently aiming for |
| `fan_mode` (rw) | `0` = auto (default curve), `1` = max boost, `2` = elevated |

```sh
d=$(dirname $(grep -l hp_zbook_fan /sys/class/hwmon/*/name))
cat $d/fan1_input
echo 1 | sudo tee $d/fan_mode   # boost
echo 0 | sudo tee $d/fan_mode   # back to auto
```

The mode survives suspend/resume: the driver writes it back on resume. To set the mode at boot, use the module option `mode=` (e.g. `options hp_zbook_fan mode=0` in `/etc/modprobe.d/`).

## Limitations: no arbitrary PWM

The EC firmware does not accept a raw duty cycle or RPM target from the OS. These were all tested on BIOS T92 01.25.00 and had no effect:

- writing the target registers `FTGC`/`FT2C` (0x2F/0x36), even repeatedly every 50 ms. The EC owns them.
- `MFAC`/`CFAN` (0xD5/0xD6, the DSDT's `KFCL`/`KSFS` path). Writes are dropped.
- `SFSD` (PWM0 in EC shared memory). The method runs but nothing changes.
- the zone temperature override path (`CRZN`/`TEMP`).

The only fan control the EC honours is the per-fan mode nibble in `AFAN` (0x2D). The DPTF `IETM.IMOK` method writes the same register. Values above 2 are rejected.

## EC register map (from DSDT)

| reg | name | use |
|---|---|---|
| 0x2D | AFAN | fan mode, low nibble fan 1, high nibble fan 2 (0..2) |
| 0x2E | FRDC | fan 1 tach period, RPM = 245760 / value |
| 0x2F | FTGC | fan 1 target period (read-only) |
| 0x35 | FR2C | fan 2 tach period |
| 0x36 | FT2C | fan 2 target period (read-only) |

## Install (DKMS)

```sh
sudo dnf install dkms kernel-devel   # or your distro's equivalent
sudo cp -r . /usr/src/hp-zbook-fan-1.0
sudo dkms install hp-zbook-fan/1.0
echo hp_zbook_fan | sudo tee /etc/modules-load.d/hp_zbook_fan.conf
sudo modprobe hp_zbook_fan
```

The module loads only on boards with DMI vendor HP and board name `8873`. Other HP business laptops with the same DSDT layout may work if you add their board ID, but that is untested.

All EC access goes through the kernel ACPI EC driver (`ec_read`/`ec_write`), so it cannot collide with firmware ACPI accesses.

Use at your own risk.
