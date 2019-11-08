sBoot - An example spiffs based boot loader for the ESP8266
-----------------------------------------------------------
by Richard A Burton, richardaburton@gmail.com
http://richard.burtons.org/


sBoot is a demontration of a bootloader that can use spiffs to flash a new rom
image. You could use this to perform OTA updates by downloading a new rom image,
from within your running application, to the spiffs filesystem and rebooting
to have it applied. Note: the rom is not run directly from spiffs!

The ota image should be gzip compressed. On each boot, if a specified ota image
is found in the spiffs, the crc will be compared with that of the installed rom
and if they do not match the ota rom in spiffs will be installed and booted.
Before flashing the ota image will be tested to ensure it can be extracted
without fault. If an ota image is not found the existing image will be booted as
normal.

sBoot compiles to a little under 16k, so will need to occupy the first 4 sectors
of the flash. You will need to install your user rom to somewhere beyond this,
and adjust your linker script to set the irom0_0_seg org address accordingly.
It runs entirely from iram and uses the stage 2.5 bootloader borrowed from rBoot
to start the user rom.

