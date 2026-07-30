# OBU vehicle client on OpenVela

This directory is prepared to be copied into:

`/home/book/vela-opensource/apps/examples/obu_vehicle_client`

or, if your Ubuntu image uses that layout:

`/home/ubuntu/vela-opensource/apps/examples/obu_vehicle_client`

## 1. Recommended environment

The tutorial in `vela/1_学习手册/openvela快速入门与工程实践_v1.pdf` says:

- use the prepared Ubuntu VM
- source code is in `/home/book/vela-opensource`
- build from `vendor/allwinnertech/lichee`

The package `vela/4_工具软件/8_搭建好环境的Ubuntu镜像/两个Ubuntu的不同.md` also says both prepared Ubuntu images use the same build method:

```sh
cd ~/vela-opensource/vendor/allwinnertech/lichee/
source vela_env.sh
source envsetup.sh
lunch_nuttx
m
pack
```

For a beginner, the prepared Ubuntu VM is the correct path. Do not try to rebuild the full OpenVela cross toolchain on Windows first.

## 2. Copy the app into OpenVela

In Ubuntu, copy this folder into `apps/examples`:

```sh
cp -r /path/to/obu_vehicle_client_openvela_app /home/book/vela-opensource/apps/examples/obu_vehicle_client
```

If your VM uses `/home/ubuntu/vela-opensource`, replace the path.

After copying, this directory should contain:

- `Kconfig`
- `Make.defs`
- `Makefile`
- `CMakeLists.txt`
- `obu_main.c`
- the other `.c/.h` files

## 3. Enable the app in menuconfig

The tutorial gives this configuration flow:

```sh
cd ~/vela-opensource
./build.sh vendor/allwinnertech/boards/r528/r528s3-velaevb1/configs/nsh/ distclean
./build.sh vendor/allwinnertech/boards/r528/r528s3-velaevb1/configs/nsh/ menuconfig
```

Inside `menuconfig`, enable:

- `Application Configuration`
- `Examples`
- `obu vehicle client`

Save and exit.

If you cannot find `obu vehicle client` in `Examples`, your OpenVela source tree likely needs the examples index updated. In that case, compare how your tree includes other example `Kconfig` files and add this new directory in the same place.

## 4. Build and pack

The tutorial gives this exact board flow:

```sh
cd ~/vela-opensource/vendor/allwinnertech/lichee/
source vela_env.sh
source envsetup.sh
lunch_nuttx
```

When prompted, choose:

```text
2. r528s3-velaevb1
```

Then build:

```sh
m
pack
```

This performs:

- board config selection
- cross compilation with the built-in ARM toolchain
- packaging into a flashable image

## 4.1 Commands that actually worked in this environment

The `m` wrapper in this Ubuntu image does not safely accept extra make-style
arguments such as `-j1` or `V=1`.

For example:

- `m -j1 V=1` is wrong here, because `m` treats `-j1` like a config name
- passing `-v` through this wrapper can end up printing `make` version instead of building

The stable direct build command is:

```sh
cd ~/vela-opensource
./build.sh vendor/allwinnertech/boards/r528/r528s3-velaevb1/configs/nsh/ -e -Wno-error -j1
```

If you want the normal faster build after things are stable, change `-j1` to your CPU count.

## 4.2 Important pack workaround for this board tree

In this tree, the generated `nuttx.bin` becomes a sparse file with a very large
apparent size, and the default packaging flow copies it to:

- `vendor/allwinnertech/lichee/board/r528s3/velaevb1_nand/configs/nsh.fex`

That makes `pack` fail with:

- `ERROR: dl file nsh.fex size too large`

The practical workaround is to replace `nsh.fex` with a stripped `nuttx`
image before running `pack`:

```sh
/home/ubuntu/vela-opensource/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin/arm-none-eabi-strip \
  --strip-unneeded ~/vela-opensource/nuttx/nuttx \
  -o ~/vela-opensource/vendor/allwinnertech/lichee/board/r528s3/velaevb1_nand/configs/nsh.fex
```

Then run `pack`.

This produced a valid package in the tested VM.

## 5. Which image to flash

The tutorial says PhoenixSuit can flash:

- the image you compiled just now
- or the sample image in `vela/4_工具软件/5_开发板系统镜像`

The sample image there is:

- `rtos_nuttx_r528s3-velaevb1_uart0_256Mnand.img`

For your own code, use the image produced by your `pack` step, not the sample one.

If you are unsure where `pack` placed the output, search in Ubuntu:

```sh
find ~/vela-opensource/vendor/allwinnertech/lichee -name "*.img"
```

Use the newest `r528s3-velaevb1` image.

## 6. Install Windows flash driver

The tutorial pages 11-14 say:

1. unpack `vela/4_工具软件/2_全志线刷烧录驱动`
2. connect two USB cables
3. hold `FEL`, then short press `RST`
4. Device Manager shows an unknown device
5. manually install the driver from the unpacked `UsbDriver` directory
6. after success, Device Manager shows `USB Device (VID_1f3a_efe8)`

## 7. Flash with PhoenixSuit

The tutorial pages 14-16 say:

1. open:
   `vela/4_工具软件/1_全志线刷烧录工具/PhoenixSuit.exe`
2. switch to the one-click flash page
3. browse and select your `.img`
4. choose `全盘擦除升级`
5. do not click random extra buttons
6. with the board already connected, hold `FEL`, then press `RST`
7. the board enters flash mode and flashing starts automatically
8. after flash completes and the tool asks whether to upgrade again, choose `N`

## 7.1 If PhoenixSuit driver fails on modern Windows

On this machine, the line-flash path is currently blocked by the legacy Allwinner USB driver, not by your app code.

What was confirmed:

- the board enumerates as `USB\\VID_1F3A&PID_EFE8`
- the Allwinner driver package installs as `oem177.inf`
- the kernel service is `usbUDisc`
- Windows binds the device, but the driver fails to start
- device status becomes `CM_PROB_DRIVER_FAILED_LOAD`

The practical conclusion is:

- your build result may still be fine
- but PhoenixSuit line flashing can fail on recent Windows builds because this old driver is unstable there

If that happens, use the TF-card flashing path instead of spending more time on the USB driver.

## 7.2 TF-card flashing fallback

The tool package already exists here:

- `vela/4_工具软件/3_全志TF卡烧录工具/PhoenixCard-V2.8.zip`

Recommended fallback flow:

1. unpack `PhoenixCard-V2.8.zip` on Windows
2. insert a TF or microSD card into the PC
3. open PhoenixCard as administrator
4. select the `.img` generated by your successful `pack`
5. select the TF card device carefully
6. choose the startup or product burn mode used by the tool's default recommended flow
7. click burn to write the card
8. insert the written card into the board
9. boot the board from the TF card and watch the serial log

This avoids the failing `usbUDisc` driver completely.

## 8. Open the serial console

The tutorial page 16 says to use MobaXterm serial settings:

- Port: the board COM port
- Speed: `1500000`
- Flow Control: `none`

Flow control must be `none`.

## 8.1 Important build sanity check

Do not flash an image produced after a failed `m`.

The safe rule is:

1. `m` must finish successfully
2. then run `pack`
3. then flash the new image

If `m` failed and `pack` still ran afterward, treat that image as unreliable.

## 9. Verify on the board

After boot, at the NSH prompt:

1. configure Wi-Fi manually first:

```sh
ifup wlan0
wapi mode wlan0 2
wapi psk wlan0 <your_password> 3 2
wapi essid wlan0 <your_ssid> 1
renew wlan0
ifconfig
```

2. run the app:

```sh
obu_vehicle_client
```

3. if you want one-board SPaT verification, set in `config.h` before rebuild:

```c
#define OBU_SPAT_SELF_TEST 1
```

Then rebuild, repack, reflash. Serial logs should show:

- MQTT connect
- SPaT subscribe
- three self-test SPaT injections
- cached green/yellow/red states

## 10. What you are really compiling

You are not compiling this project with desktop `cmake` for the board.

You are:

1. putting the code into the OpenVela `apps/examples` tree
2. enabling it with `menuconfig`
3. letting OpenVela's cross build system compile it for `r528s3-velaevb1`
4. using `pack` to produce the flash image
5. using PhoenixSuit to burn that image
