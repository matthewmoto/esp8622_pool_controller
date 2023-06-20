# ESP8266 Smart Pool Controller

# Obligatory Disclaimer

This project involves proximity to mains (240VAC) electricity which has the potential to be lethal. While everything discussed here is either software or low voltage, the equipment described does interface-with relays that are running mains. If you don't know what you're doing, please enlist the help of a licensed electrician. Both the software and the hardware linked/described here are provided without warranty, express or implied, and your use of the same is at your sole risk and responsibility. 

**tl;dr:** If you don't know what you're doing and you electrocute yourself, (please don't), it isn't my fault.

# What is this?

The code and docs here describe my particular instance of replacing my dead Jandy Aqualink RS pool controller (with wired remotes) with an ESP8266 and a COTS relay board to make a smart pool controller I can control from my phone (or a terminal with curl, shown here).

If you're my flavor of crazy and decide that you're going to franken-swap your pool controller with a low-cost ESP8266 microcontroller too, the code in here might be a good head-start.

## Why?

Put simply, I had most of the stuff for this lying around and pool controllers, by and large, are *obscenely* expensive microcontrollers (>$1000) and mine wasn't broken because anything important was wrong. My high-voltage relays were mint, my transformers were rust-free, etc. Just the aging "brain" of the box decided to give up.

## What this repo/document is *not*

Your pool controller may be different than mine, so I'm going to stick to generalizations and high-level discussion here. This is not an all-inclusive "how-to" guide for replacing your pool controller.

If you *do* choose to use my code for something like this and need a hand, feel free to reach out and I'll be happy to answer questions. As of this document being written, the controller has been been operating strong for ~3 years so I'm confident it's stable enough for regular use.

# Getting Started

## Hardware

For my project, I used the following hardware.  My (and most pool controllers) use 12/24VAC actuators for the various valves and 24VDC for the low-voltage side of the big relays that drive the pump and lights.

Here are some (hopefully not dead), links to the things I used:
* [DS18B20 waterproof digital temperature sensors (I used 2)](https://www.amazon.com/gp/product/B00CHEZ250)
* [24VDC Waterproof power supply](https://www.amazon.com/gp/product/B07MCJQ9HX) (used to power the big relays for my controller)
* [ESP8266 NodeMCU boards](https://www.amazon.com/gp/product/B01IK9GEQG)
* [74HC595 8-Bit Shift Register](https://www.amazon.com/gp/product/B06WD3W8Q3) (used because the NodeMCU doesn't have enough pins to drive 8 relays)
* [An 8-channel relay module](https://www.amazon.com/SainSmart-101-70-102-8-Channel-Relay-Module/dp/B0057OC5WK) (The exact one I used isn't sold anymore, but this is basically it).
* [A DC-DC step down (buck) converter](https://www.amazon.com/gp/product/B014Y3OT6Y/) (This is used to convert the 24VDC from my my big power supply to 3.3V to power the NodeMCU)

## Wiring

### Pin definitions

All the PINs used for the various inputs and outputs of the NodeMCU (ESP8266) board are found in `Constants.h`. Likewise, the rest of the magic numbers can be found there too (default timezone offset, update intervals, thermistor values, etc). 

**Please ensure your PINs from Constants.h line up with your actual installation.**

### Power
Essentially, I power the whole thing from a 24VDC LED power supply (60W). This voltage is important since it drives the heavy relays that power on my 1.5HP pool pump or activate my pool/spa lights. I take the 24VDC from this as well and run it through a small buck converter to get a nicer (and cleaner) 3.3V voltage for my NodeMCU board.

### Sensors

#### Pool temperature
The NodeMCU has a single (terrible) analog input that I use for the pool temperature thermistor. These are usually probes that are drilled into a PVC pipe and are a 10K thermistor. I say "terrible" because the ADC on the ESP8266 board is massively inaccurate and imprecise so I had to do some heavy filtering and trial and error with a voltage-divider resistor to find a reasonable range for it. In hindsight, you'd be better to use a 3'rd DS18B20 and hack a way to get it into a pipe (I've seen others do this with epoxy and such).

#### Other temperatures
In addition to the pool water thermistor, I have an ambient outside temperature sensor (dangling in the shade) and a roof sensor (painted black and mounted on the roof) that are used for solar water heating (controlled via a valve actuator). These are connected on a single Dallas 1-wire bus and are wonderfully easy to integrate.

### Outputs

The NodeMCU has a few shortcomings. Besides the sketchy ADC described above, it also lacks a large number of pins for things like controlling 8 relays (we have 1 relay per valve, pump, light, etc, so you need a few if you have a spa (2 valves), light, solar heater like I do.  To get around this, I used a shift 74HC595 shift register. This lets us turn 3 pins into 8 for controlling our big 8-channel relay board.

## Building/Uploading the code


### Initial set-up

Assuming you've read the above and updated `Constants.h` to point at the correct pins for your installation, you'll also want to update default wifi details to be *your* wifi details (in the same file):
```
...
  "wifi":{
    "ssid": "YOUR_WIFI_SSID",
    "pw": "YOUR_WIFI_PASSWORD",
    "ntp_server": "us.pool.ntp.org",
    "tz_offset": -4
  },
...
```

Likewise, rename the relays to whatever you have connected there (the defaults are my pool equipment, so that might help as an example).

I used [PlatformIO](https://platformio.org/) to build this project.  The file `build_and_upload_ota.sh` has the commands for OTA updates to update the controller in-situ, but for the bench, simply run
```
$ pio run --target=upload --upload-port=/dev/TTYUSB0 #or whatever your serial port is
```

**NOTE:** For OTA uploads, you'll also want to update the password "REDACTED" in the codebase to one you choose (I just put "REDACTED" in there to avoid sharing my actual pw). This can be changed in `Constants.h` as **OTA_PASSWORD** and in `platformio.ini` as the argument to **--auth=**) to a password you select. Both of those should be the same since one is running on the firmware to accept OTA updates and the other is the build/upload script on your computer to push updates.

## Interfacing with the controller

Assuming you've gotten this far and cobbled together a controller, updated the pins/constants and haven't blown anything important up yet (congratulations, by the way), you'll probably want to know how to interface with the controller.

This is actually the easy part, since the controller has essentially a RESTful interface. It communicates with GET/POST requests using JSON. I've included examples below for manually interacting with the controller (and that is 100% fine if you don't want to go further).  I'm currently running it underneath [Home Assistant](https://www.home-assistant.io/) with only configuration files to power the integration to great effect. 

For this example, we'll assume your controller is powered on and was assigned the IP address 192.168.1.132.

### Getting the current state of the controller (everything)

This is the default (what is going on) request to make. We'll use [curl](https://curl.se/), but you can use anything that lets you GET/POST data (wget, your web browser, etc).

Assuming you're running some flavor of Linux/Unix:
```bash
$ curl http://192.168.1.132/everything
{"wifi":{"ssid":"bknet_EXT","ntp_server":"us.pool.ntp.org","tz_offset":-4},"relays":[{"name":"pump","state":"on","schedule":[{"on":"10:00:00","off":"17:00:00"}]},{"name":"light","state":"off","schedule":[]},{"name":"aux_1","state":"off","schedule":[]},{"name":"aux_2","state":"off","schedule":[]},{"name":"solar_valve","state":"off (manual)","schedule":[]},{"name":"spa_drain","state":"off","schedule":[]},{"name":"spa_fill","state":"off","schedule":[]},{"name":"aux_valve_1","state":"off","schedule":[]}],"sensors":[{"name":"28FFCFE4021502A2","role":"ambient_air_temp","temp_f":75.7625,"type":"DS1820 Digital Sensor"},{"name":"28FFEF5F031502F4","role":"solar_roof_temp","temp_f":70.475,"type":"DS1820 Digital Sensor"},{"name":"analog","role":"water_temp","temp_f":85.44561,"type":"Analog Thermistor"}],"solar":{"enabled":"on","state":"bypass","target_temp":90},"general":{"mode":"run_schedule","time":"13:04:17","last_time_update":3631652031,"last_status_update":3631678800,"pool_water_sensor_name":"analog","roof_sensor_name":"28FFEF5F031502F4","ambient_air_sensor_name":"28FFCFE4021502A2","errors":[]},"now":3631679910}
```

Oof, unformatted JSON is pretty ugly, let's pipe that through [jq](https://jqlang.github.io/jq/):
```bash
$ curl http://192.168.1.132/everything |jq
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100  1118  100  1118    0     0  42509      0 --:--:-- --:--:-- --:--:-- 43000
{
  "wifi": {
    "ssid": "bknet_EXT",
    "ntp_server": "us.pool.ntp.org",
    "tz_offset": -4
  },
  "relays": [
    {
      "name": "pump",
      "state": "on",
      "schedule": [
        {
          "on": "10:00:00",
          "off": "17:00:00"
        }
      ]
    },
    {
      "name": "light",
      "state": "off",
      "schedule": []
    },
    {
      "name": "aux_1",
      "state": "off",
      "schedule": []
    },
    {
      "name": "aux_2",
      "state": "off",
      "schedule": []
    },
    {
      "name": "solar_valve",
      "state": "off (manual)",
      "schedule": []
    },
    {
      "name": "spa_drain",
      "state": "off",
      "schedule": []
    },
    {
      "name": "spa_fill",
      "state": "off",
      "schedule": []
    },
    {
      "name": "aux_valve_1",
      "state": "off",
      "schedule": []
    }
  ],
  "sensors": [
    {
      "name": "28FFCFE4021502A2",
      "role": "ambient_air_temp",
      "temp_f": 74.75,
      "type": "DS1820 Digital Sensor"
    },
    {
      "name": "28FFEF5F031502F4",
      "role": "solar_roof_temp",
      "temp_f": 69.8,
      "type": "DS1820 Digital Sensor"
    },
    {
      "name": "analog",
      "role": "water_temp",
      "temp_f": 86.17639,
      "type": "Analog Thermistor"
    }
  ],
  "solar": {
    "enabled": "on",
    "state": "bypass",
    "target_temp": 90
  },
  "general": {
    "mode": "run_schedule",
    "time": "13:05:30",
    "last_time_update": 3631652031,
    "last_status_update": 3631751308,
    "pool_water_sensor_name": "analog",
    "roof_sensor_name": "28FFEF5F031502F4",
    "ambient_air_sensor_name": "28FFCFE4021502A2",
    "errors": []
  },
  "now": 3631752622
}
```
There it is, everything! This is an actual dump of my controller as I'm writing this document over a lunch hour.

### More Granular information

If you don't want to parse the "everything" JSON block, you can get each individual part by GET'ing the following URLs:
* Sensors: http://YOUR_IP_ADDR/sensors
* Relays: http://YOUR_IP_ADDR/relays
* Solar Heating: http://YOUR_IP_ADDR/solar
* General Info: http://YOUR_IP_ADDR/general

### Resetting to Default
If you break something and want to reset your controller to its defaults, you can GET http://YOUR_IP_ADDR/reset to do just that.


### Turning Relays on and off

To turn a relay on or off, you'll make a POST request to the relays endpoint.  For example, let's assume we have a file called pool_pump.json containing:
```
{
    "relays":[
        {
            "name":"pump",
            "state":"off"
        }
    ]
}
```

We can turn the pool pump off by running:
```bash
$ curl -X POST -H "Content-Type: application/json" --data @pool_pump.json http://192.168.1.132/relays
```


### Relay Schedules

The controller firmware has the capacity to enable daily schedules for relays (like the pool pump, in my case). As seen in the example above, my pump relay is set to run from 10:00 to 17:00 daily:
```
  "relays": [
    {
      "name": "pump",
      "state": "on",
      "schedule": [
        {
          "on": "10:00:00",
          "off": "17:00:00"
        }
      ]
    },
...
```

To set these schedules (you can have multiple on/off times per day if you want to use less power at the expense of your relay longevity), you simply pass them to the same endpoint for turning relays on/off as seen above. Here's an example to run the "pump" relay from 10:00 -> 11:00 and again from 14:00 -> 15:00. 
Let's assume we have this as a file called pump_sched.json
```
{
    "relays":[
        {
            "name":"pump",
            "schedule": [
              {
                "on": "10:00:00",
                "off": "11:00:00"
              },
              {
                "on": "14:00:00",
                "off": "15:00:00"
              }
            ]
        }
    ]
}
```

Like turning the pump on/off before, we can upload the schedule hitting the same endpoint:
```bash
$ curl -X POST -H "Content-Type: application/json" --data @pump_sched.json http://192.168.1.132/relays
```

#### A note about manual on/off during a schedule

Like most light/outlet timers, if you have a relay that is scheduled to be on at the current time and you turn it off, it will simply pick up the schedule at the next "on" interval (likely the next day). The same applies of it was off and you turn it on. It will stay on until it's next scheduled to be "off" again.

This really sounds more complicated in text than it is. Essentially, the relays will always obey your commands, but their next scheduled on/off will override any manual setting when it hits.

### Solar Heating configuration

I have a valve that diverts my pump water to my roof solar heater. It's a single relay, but instead of having a daily schedule, the pool controller has some smarts built into it to use the temperature sensors to heat your pool (if it's useful to do so) to your desired temperature.

#### Enabling/Disabling solar heating

Just like above with the relays, setting the solar settings looks almost identical to getting the solar settings, except it's a POST request instead of a GET.

Here's an example to enable solar heating and set the target temperature to 85 deg. F.

Let's prep a JSON file called solar.json:
```
{
    "enabled": "on",
    "target_temp": 85
}
```
Then we simply POST it to the solar endpoint:
```bash
$ curl -X POST -H "Content-Type: application/json" --data @solar.json http://192.168.1.132/solar
```
