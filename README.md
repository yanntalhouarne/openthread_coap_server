# openthread_coap_server

*** using nRF SDK Connect v2.4 ***

1. nRF Connect Build Configuration
   - Configuration:
      * prj.conf
   - Kconfig fragments:
      * overlay-usb.conf
      * overlay-logging.conf (optional)
   - Extra CMake arguments:
      * -DDTC_OVERLAY_FILE:STRING=usb.overlay

2. SRP Client Service Registering
   - For each new device flashed, a unique hostname and service instance but be set in "ot_srp_config.h"
   - If hostname is not unique, the SRP server (on the border router dongle) will reject the registering of the service
   - Openthread saves the SRP key to non-volatile memory. If the device is erased (e.g. ot factoryreset), the key will erased and the SRP client on the device will have issues updating its service with the SRP server

3. Remaining work
   - Set a different SRP client hostname if it is already taken (the SRP Client callback in coap_server.c will be called with aError = DUPLICATE)
   - Move the SRP stuff out of coap_server.c

4. Ping the device
   - ping -6 SRP_CLIENT_HOSTNAME.local
   - coap-client -m get coap://[SRP_CLIENT_HOSTNAME.local]/temperature -N
   - example: 
      * ping -6 nrf52840dongle.local
      * coap-client -m get coap://nrf52840dongle.local/temperature -N -v 9

5. To flash nRF52840 Dongle:
   - generate DFU package from .hex file
      $ nrfutil pkg generate --hw-version 52 --sd-req 0x00 --application-version 1 --application /PATH_TO_THIS_REPO/build_1/zephyr/zephyr.hex nrfDongle_dfu_package.zip
   - flash Dongle (make sure it is set in bootloader mode by holding the side switch while connecting it to the USB port):
      $ nrfutil dfu usb-serial -pkg nrfDongle_dfu_package.zip -p /dev/ttyACM0