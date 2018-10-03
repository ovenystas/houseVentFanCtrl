# houseVentFanCtrl

Z-Wave whole house ventilation fan speed controller

## Features:
* Control the speed of a whole house ventilation fan with 6 distinct speed values.
Off, very low, low, mid, high, full.
* Speed is controlled via relays by connecting one of five outputs from a transformer to the fan motor.
* Five out of six relays on a six relay module board are used.
* Speed is set remotely via Z-Wave protocol using a Z-Uno Arduino board.
* Last set speed is remembered and used to reset relays to same state after a power outage.
