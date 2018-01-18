***
Just for demo How to use libusb to send SCSI Vendor command to Mass storage device

**
limition: only vaild Bulk only device
the result like the below
```
----------------------------------------------------------------------------
Opening device 1B3F:30FE...

Reading device descriptor:
            length: 18
      device class: 0
               S/N: 0
           VID:PID: 1B3F:30FE
         bcdDevice: 0100
   iMan:iProd:iSer: 1:2:0
          nb confs: 1

Reading first configuration descriptor:
             nb interfaces: 1
              interface[0]: id = 0
interface[0].altsetting[0]: num endpoints = 2
   Class.SubClass.Protocol: 08.06.50
       endpoint[0].address: 81
           max packet size: 0040
          polling interval: 00
       endpoint[1].address: 02
           max packet size: 0040
          polling interval: 00

Claiming interface 0...

Reading string descriptors:
   String (0x01): "UltraNET "
   String (0x02): "USB-WATCH"
   sent 6 CDB bytes
   received 36 bytes
   Vendor:Product:Revision "UltraNet":" USB-Wat":" 3.0"
   Mode: W5S
   Mass Storage Status: 00 (Success)
Read Settings:
   sent 2 CDB bytes
   Name:PedoWatch W5S V2, Received size: 256
   Year:2018, Month: 1, Day:18
   Hour:12, Minute: 40, Second:0
   Display mode:12Hrs
   Control:00
   Sleep monitor start: 23h:20
   Sleep monitor stop: 07h:00
   Alram: 07h:00
   Smart LED: disabled
   Height:170cm, Weight 65.00Kg, Stride: 70cm
   Mass Storage Status: 00 (Success)
--------------------------------------------------------
    00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
--------------------------------------------------------
00| 50 65 64 6F 57 61 74 63 68 20 57 35 53 20 56 32
10| 12 01 12 0C 28 00 00 00 12 01 12 0C 26 0F 24 00
20| 04 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
30| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
40| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
50| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
60| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
70| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
80| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
90| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
A0| 00 17 14 07 00 07 00 00 00 00 00 00 00 00 00 00
B0| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
C0| 00 00 04 00 AA 19 64 00 46 00 00 00 00 00 00 00
D0| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
E0| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
F0| 00 00 00 00 00 00 00 00 00 00 00 00 22 6A 72 AD
write Settings:
   sent 2 CDB bytes
   Mass Storage Status: 00 (Success)
Read Settings:
   sent 2 CDB bytes
   Name:PedoWatch W5S V2, Received size: 256
   Year:2018, Month: 1, Day:18
   Hour:12, Minute: 40, Second:0
   Display mode:12Hrs
   Control:00
   Sleep monitor start: 23h:00
   Sleep monitor stop: 07h:00
   Alram: 07h:00
   Smart LED: disabled
   Height:170cm, Weight 65.00Kg, Stride: 70cm
   Mass Storage Status: 00 (Success)
--------------------------------------------------------
    00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
--------------------------------------------------------
00| 50 65 64 6F 57 61 74 63 68 20 57 35 53 20 56 32
10| 12 01 12 0C 28 00 00 00 12 01 12 0C 28 00 24 00
20| 04 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
30| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
40| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
50| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
60| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
70| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
80| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
90| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
A0| 00 17 00 07 00 07 00 00 00 00 00 00 00 00 00 00
B0| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
C0| 00 00 04 00 AA 19 64 00 46 00 00 00 00 00 00 00
D0| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
E0| 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
F0| 00 00 00 00 00 00 00 00 00 00 00 00 13 6C 72 A0
Read Steps: 96
   sent 6 CDB bytes
   Received size: 512
   Year:2000, Month: 0, Day:0, Hour:0
```


