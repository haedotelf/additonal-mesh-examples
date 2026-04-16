**Similar to Bluetooth Mesh NLC: Dimming Control/Scene Selector sample.**

https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/samples/bluetooth/mesh/light_dimmer/README.html#testing

Checked on NCS 3.2.4

Works on nrf52840dongle/nrf52840/bare, nrf54l15dk/nrf54l15/cpuapp, nrf52840dk/nrf52840. Ie. it can be built using the following build commands: (more configuration files are used in the build but should be found automagically; mentioning them in the build command shouldn't be necessary)

west build -b nrf52840dongle/nrf52840/bare --pristine -d [my_build_folder]
west build -b nrf54l15dk/nrf54l15/cpuapp --pristine -d [my_build_folder]
west build -b nrf52840dk/nrf52840 --pristine -d [my_build_folder]

Works alongside the default fixture project. 

For provisioning and configuration instructions see (using a phone for this is likely easiest): https://docs.nordicsemi.com/bundle/ncs-3.2.0-preview3/page/nrf/samples/bluetooth/mesh/light_dimmer/README.html#provisioning_the_device

When built, flashed, provisioned, and configured, one can use the following commands over serial:
on (note that the LED otherwise turned off by the button1 on the light fixture gets turned on)
off (note that the LED otherwise turned off by the button1 on the light fixture gets turned off)
dim up [x percentage] (eg. "dim up 10" note that the LED gets slightly more visible)
dim down [x percentage] (note that the LED gets slightly less visible)
