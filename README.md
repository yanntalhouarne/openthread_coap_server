# openthread_coap_server

using nRF SDK Connect v2.4

nRF Connect Build Configuration
 - Configuration:
    * prj.conf
 - Kconfig fragments:
    * overlay-usb.conf
    * overlay-logging.conf (optional)
 - Extra CMake arguments:
    * -DDTC_OVERLAY_FILE:STRING=usb.overlay
