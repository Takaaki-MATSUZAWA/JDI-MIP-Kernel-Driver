# JDI MIP LCD Kernel Driver

**Note**: I did not write this driver. I only modified it to clean up compiler warnings/errors. The original(Sharp Memory LCD Kernel Driver) can be found here:
(http://www.librecalc.com/en/wp-content/uploads/sites/4/2014/10/sharp.c)

More information can be found here:
(http://www.librecalc.com/en/downloads/)

The original code appears to be LCD made by shap.
Therefore, we used this code as a reference for JDI MIP LCD:(https://github.com/a8ksh4/JDI-LCD-Kernel-Driver)

Also, when installing on Radxa zero, I used this code as a reference for the Device Tree Overlay:(https://github.com/imnotjames/Sharp-Memory-LCD-Kernel-Driver/tree/radxa-zero-hacks)

This driver is for the LPM027M128B. 

## Hookup Guide
Connect the following pins:

Display | Radxa Zero Pin |
------- | ---------
SCLK    | 23 (SPI_B_SCLK)       
SI      | 19 (SPI_B_MOSI)       
SCS     | 24 (SPI_B_SS0)
EXTCOMIN| 16 (GPIOX_10) 
DISP    | 18 (GPIOX_8) 
VDDA    | GND
VDD     | GND      
EXTMODE | +3.3V       
VSSA    | +3.3V     
VSS     | +3.3V

If you want to change the connection pins, change jdi_mip.dtsi.

## Compile/Install the driver
Verify that you have the linux kernel headers for your platform. 
```
sudo apt-get install linux-headers-$(uname -r)
```

To compile the driver, run:
```
make
```

To install the driver, run:
```
sudo make modules_install
```

Update the module dependencies and test if they are loaded correctly:
```
sudo depmod -a
sudo modprobe jdi_mip
```

If you want the module to load at boot you'll need to add it to the /etc/modules file, like:
```
...
# This file contains...
# at boot time...
jdi_mip
```

## Compile/Install the Device Tree Overlay
When you make, mji_mip.dtbo should be generated from mji_mip.dtsi through mji_mip.dts.

To load it at runtime, copy it to /boot/dtbs/$(uname -r)/amlogic/overlay/:
```
sudo cp jdi_mip.dtbo /boot/dtbs/$(uname -r)/amlogic/overlay/
```

And then modify the following line to /boot/uEnv.txt:
```
overlays=meson-g12a-uart-ao-a-on-gpioao-0-gpioao-1 jdi_mip
param_spidev_spi_bus=1
param_spidev_max_freq=20000000
```

## Console on Display
If the module is loaded correctly, the framebuffer will be recognized as /dev/fb0 and the console will be assigned /dev/tty1.

However, if /dev/fb1 is subsequently added because of HDMI, tty1 will become inactive.

To activate tty1 again, execute the following command
```
sudo chvt 1
```

If you want to activate it automatically at boot time, use systemd or similar.

Create `/etc/systemd/system/chvt1.service`

```
[Unit].
Description=Switch to /dev/tty1

[Service] ExecStart=/bin/bash
ExecStart=/bin/bash -c "sleep 5 && chvt 1"
Type=oneshot

[Install]
WantedBy=multi-user.target
```

Here, the command on the `ExecStart` line will wait for 5 seconds before executing `chvt 1`.
After saving this file, make systemd aware of the new service with the following command:
```
sudo systemctl daemon-reload
```

Then, activate this new service with the following command so that it will run automatically at system startup:
```
sudo systemctl enable chvt1
```

Finally, reboot the system to verify that the configuration works correctly:
```
sudo reboot
```
