Read UPS status with ESP2 protocol. For UPSes like Liebert/Emerson GXT2, NXe, others maybe.

This was later contributed to NUT (NetworkUPS Tools) (https://github.com/networkupstools/nut/blob/master/drivers/liebert-esp2.c)

```
  upsesp2 version 0.4, 05-Apr-2010:
  Usage:
		  upsesp2 <serial device>
  or              upsesp2 <serial device> <UPS parameter>
  example:        upsesp2 /dev/ttyS1 LOAD_WATTS
  The 1st syntax will report a list of supported parameters
```
