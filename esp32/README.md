# ESP32 Side

经典 ESP32 上的 ESP-IDF 5.4.4 工程，负责 BLE GATT 服务和 STM32 UART 安全链路。

## 当前范围

- UART2：GPIO17 TX、GPIO16 RX、115200 8N1。
- NimBLE 单连接 GATT Server，提供加密命令写入与加密状态读取/通知。
- 已实现绑定、RPA 隐私、单绑定和 BOOT/GPIO0 长按 3 s 打开的 60 s 换绑窗口。
- 当前只接受 STOP；格式正确的 DRIVE 也会被拒绝。
- UART 只有在 STOP ACK 和其后的零 PWM 新鲜状态都成立时才标记 ONLINE。

手机侧已经验证扫描、配对、加密读取、状态通知、STOP、断连/重连，以及坏 CRC、错误长度和 DRIVE 拒绝。第二部手机换绑及配对窗口边界仍待完整实测。

## 构建

```powershell
idf.py set-target esp32
idf.py build
```

不要提交生成的 `sdkconfig`；需要的 NimBLE 配置已保存在 `sdkconfig.defaults`。

完整 BLE 协议见 [`docs/STEP10_BLE_PROTOCOL.md`](docs/STEP10_BLE_PROTOCOL.md)。

