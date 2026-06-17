"""最小 BLE 连通性测试"""
import asyncio
import logging

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
logger = logging.getLogger("ble_test")

DEVICE_NAME = "ESP32-SmartLock"
TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"


async def main():
    from bleak import BleakScanner, BleakClient

    logger.info("=== 扫描 %s ===", DEVICE_NAME)
    device = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
    if device is None:
        logger.error("没找到设备")
        return
    logger.info("找到: %s", device.address)

    logger.info("=== 连接 ===")
    cli = BleakClient(device.address)
    try:
        await asyncio.wait_for(cli.connect(), timeout=15)
    except asyncio.TimeoutError:
        logger.error("连接超时")
        return
    except Exception as e:
        logger.error("连接失败: %s", e)
        return

    if not cli.is_connected:
        logger.error("连接后 is_connected=False")
        return
    logger.info("连接成功!")

    # 列出所有服务和特征值
    logger.info("=== 服务列表 ===")
    for svc in cli.services:
        logger.info("Service: %s", svc.uuid)
        for ch in svc.characteristics:
            logger.info("  Char %s: %s", ch.uuid, ch.properties)

    # 注册 Notify
    logger.info("=== 注册 Notify (TX) ===")

    def rx_cb(handle, data):
        logger.info(">>> 收到数据: %s", data.hex(" "))

    try:
        await cli.start_notify(TX_UUID, rx_cb)
        logger.info("Notify 注册成功!")
    except Exception as e:
        logger.error("start_notify 失败: %s", e)

    # 写测试数据
    logger.info("=== 发送数据 ===")
    try:
        await cli.write_gatt_char(RX_UUID, b"Hello from PC!", response=False)
        logger.info("发送成功!")
    except Exception as e:
        logger.error("发送失败: %s", e)

    # 保持 15 秒观察
    logger.info("=== 保持连接 15 秒观察 ===")
    for i in range(15):
        await asyncio.sleep(1)
        if not cli.is_connected:
            logger.error("!!! 第 %d 秒断开 !!!", i + 1)
            break
        if i % 5 == 4:
            logger.info("...还在连接中 (%d/15)", i + 1)

    logger.info("=== 断开 ===")
    await cli.disconnect()
    logger.info("=== 完成 ===")


if __name__ == "__main__":
    asyncio.run(main())
