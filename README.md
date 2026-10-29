# Adafruit Sprinkler Receiver

This sets up an Adafruit WiFi Feather board to act as a remote sprinkler receiver. A client attached to the 24VAC trigger of a sprinkler controller sends this receiver an `HTTP GET /on` when the controller turns on the zone and a `HTTP GET /off` when the controller turns it off. To get status of the current run state or last run start time and duration sent `HTTP GET /`.

This module turns on a latching relay when `on` and turns off the relay when `off`. The relay controls 24VAC to the sprinkler valve.

Alse see [the client code](https://github.com/alexbr/arduino-sprinkler-client).
