**Chuẩn bị**

- ZB-GW03 V1.4

- USB to UART (A serial to USB adapter (for example FTDI) that can provide 3.3V RX/TX signals)

- Dây jumper 

- Máy tính cài đặt esptool v5.3.0
```
    sudo apt update
    sudo apt install pipx
    pipx ensurepath
    pip install intelhex --break-system-packages
    pipx install esptool
```
## Connect the wires to your serial to USB adapter

Make sure that your adapter uses 3.3V for the RX/TX pins that you will connect to the device. Some of these adapters allow you to switch between 3.3V and 5V using a switch or a jumper. Do not use an adapter that only provides 5V output. Reason for this, is that the ESP32 chip works at 3.3V. I have seen the chips accept 5V serial input (I did flash the lamp successfully like that), but I am not sure if they are actually 5V tolerant. Better safe than sorry in such case!

The wires must be connected as follows:

| **Soldering point** | Serial USB Adapter name |
| :-: | :-: |
| GND, I0 ( `GPIO0)` | GND |
| TX | RX |
| RX | TX |
| 3.3V | 3.3V |

To be able to flash the device, `GPIO0` must be connected to ground (`GND`) while the device boots up. Flashing will *not* work if you connect these wires *after* the device has already been booted up. In the following images, you will see one solution, using male dupont wires.

You can now connect the serial to USB adapter to your computer. Pay special attention to the cross-over of the TX/RX pair (TX connects to RX and vice versa).Because `GPIO0` is connected to `GND`, the device should start up in download mode.



## Flash  firmware ESP32


1. Kết nối uart với **ZB-GW03 v1.4 **đến máy tính bằng usb-uart, flash firmware tasmota32-ZB-GW03-EN.factory.bin


2. Cấp quyền cho cổng 
 ```
 ls /dev/ttyUSB* /dev/ttyACM*
 sudo usermod -a -G dialout $USER
 sudo systemctl stop brltty-udev.service
 sudo systemctl mask brltty-udev.service
 ```

3. Xóa firmware cũ
 ```esptool --port /dev/ttyUSB0 erase-flash```


4. Nạp firmware mới

```esptool --port /dev/ttyUSB0 --baud 921600 write-flash 0x0 tasmota32-ZB-GW03-EN.factory.bin```


**Flash  firmware zigbee chip**


1. Rút các dây của usb uart ra khỏi **ZB-GW03**, cắm dây usb-c vào bo mạch để cấp nguồn, cắm dây mạng vào  **ZB-GW03**


2. Sử dụng phần mềm để lấy địa chỉ **IP **của bo mạch trong mạng **LAN**, sử dụng trình duyệt vô địa chỉ tìm được

3. Go to **Firmware Upgrade** and next to “Upgrade by file upload” use the *Choose File* button and select Zigbee module firmware you downloaded ( ncp-uart-sw\_6.7.10\_115200.ota )

Click on **Start upgrade**, be patient and wait for a few minutes until flashing is complete.

Restart ZB-GW03 để qua trình update diễn ra

4. Khi khởi động xong , vào console để theo dõi log , để xác nhận đã flash thành công


## Update  firmware ESP32

1. Go to **Firmware Upgrade** and next to “Upgrade by file upload” use the *Choose File* button and select Zigbee module firmware you downloaded ( tasmota32-zigbeebridge.bin )

2. Click on **Start upgrade**, be patient and wait for a few minutes until flashing is complete.

3. Khi khởi động xong , vào console để theo dõi log , để xác nhận đã flash thành công

## Set IP tĩnh cho ETH

Tasmota dùng lệnh riêng cho cài đặt Ethernet:
Vào http://luckfox_ip/cs hoặc vào menu Console, gõ từng lệnh:

```
EthIPAddress 192.168.1.135
```

Thiết bị đã nhận địa chỉ IP mới
Cắm trực tiếp thiết bị vào máy tính để cài đặt tiếp các mục phía dưới

```
EthGateway 192.168.1.1
EthSubnetmask 255.255.255.0
EthDNSServer1 8.8.8.8
Restart 1
```




