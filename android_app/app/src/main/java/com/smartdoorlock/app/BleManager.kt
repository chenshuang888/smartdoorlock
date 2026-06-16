package com.smartdoorlock.app

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.Looper
import java.util.UUID

class BleManager(private val context: Context) {

    companion object {
        const val SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
        const val RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
        const val TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
        private const val CCCD_UUID = "00002902-0000-1000-8000-00805f9b34fb"
        const val DEVICE_NAME = "ESP32-SmartLock"
    }

    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        val mgr = context.getSystemService(Context.BLUETOOTH_SERVICE) as? android.bluetooth.BluetoothManager
        mgr?.adapter
    }

    private var gatt: BluetoothGatt? = null
    private var rxChar: BluetoothGattCharacteristic? = null
    private var txChar: BluetoothGattCharacteristic? = null
    private var isScanning = false
    private var pendingCccdWrite = false  // 等待 descriptor 写入完成再触发 onConnected
    private val mainHandler = Handler(Looper.getMainLooper())

    var onConnected: (() -> Unit)? = null
    var onDisconnected: (() -> Unit)? = null
    var onDataReceived: ((String) -> Unit)? = null
    var onDataSent: ((String) -> Unit)? = null
    var onStartScan: (() -> Unit)? = null
    var onScanFailed: ((String) -> Unit)? = null
    var onDeviceNotFound: (() -> Unit)? = null
    var onDeviceFound: ((String) -> Unit)? = null

    val isConnected: Boolean get() = gatt?.services?.isNotEmpty() == true

    fun isBluetoothEnabled(): Boolean =
        bluetoothAdapter?.isEnabled == true

    fun enableBluetooth() {
        val intent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        context.startActivity(intent)
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            val name = result.scanRecord?.deviceName ?: device?.name ?: ""
            if (name.isNotEmpty()) onDeviceFound?.invoke(name)

            if (name.equals(DEVICE_NAME, ignoreCase = true)) {
                stopScan()
                device?.let { connect(it) }
            }
        }

        override fun onScanFailed(errorCode: Int) {
            isScanning = false
            val msg = when (errorCode) {
                SCAN_FAILED_ALREADY_STARTED -> "扫描已开始"
                SCAN_FAILED_APPLICATION_REGISTRATION_FAILED -> "注册失败"
                SCAN_FAILED_FEATURE_UNSUPPORTED -> "不支持此功能"
                SCAN_FAILED_INTERNAL_ERROR -> "内部错误"
                else -> "扫描失败 ($errorCode)"
            }
            onScanFailed?.invoke(msg)
        }
    }

    @SuppressLint("MissingPermission")
    fun startScan() {
        val adapter = bluetoothAdapter ?: return
        if (!adapter.isEnabled) return
        if (isScanning) return
        isScanning = true
        onStartScan?.invoke()

        val scanner = adapter.bluetoothLeScanner ?: run {
            isScanning = false
            onScanFailed?.invoke("蓝牙扫描器不可用")
            return
        }

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        scanner.startScan(null, settings, scanCallback)

        // 15秒超时
        mainHandler.postDelayed({
            if (isScanning) {
                stopScan()
                onDeviceNotFound?.invoke()
            }
        }, 15000)
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        if (isScanning) {
            try {
                bluetoothAdapter?.bluetoothLeScanner?.stopScan(scanCallback)
            } catch (_: Exception) {}
            isScanning = false
        }
    }

    @SuppressLint("MissingPermission")
    private fun connect(device: BluetoothDevice) {
        gatt = device.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        stopScan()
        mainHandler.removeCallbacksAndMessages(null)
        try {
            gatt?.disconnect()
            gatt?.close()
        } catch (_: Exception) {}
        gatt = null
        rxChar = null
        txChar = null
        pendingCccdWrite = false
    }

    @Suppress("DEPRECATION")
    fun send(data: String): Boolean {
        val char = rxChar ?: return false
        val ok = try {
            char.value = data.toByteArray(Charsets.UTF_8)
            gatt?.writeCharacteristic(char) ?: false
        } catch (_: Exception) { false }
        onDataSent?.invoke(data.trim())
        return ok
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    pendingCccdWrite = false
                    gatt.discoverServices()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    this@BleManager.gatt?.close()
                    this@BleManager.gatt = null
                    rxChar = null
                    txChar = null
                    pendingCccdWrite = false
                    onDisconnected?.invoke()
                }
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) return
            val service = gatt.getService(UUID.fromString(SERVICE_UUID)) ?: return
            rxChar = service.getCharacteristic(UUID.fromString(RX_UUID))
            txChar = service.getCharacteristic(UUID.fromString(TX_UUID))

            // 请求 MTU 升级，确保大消息能一次性通知送达
            gatt.requestMtu(517)

            val tx = txChar
            if (tx != null) {
                // 启用通知：writeDescriptor 是异步的，等它完成后才触发 onConnected
                gatt.setCharacteristicNotification(tx, true)
                val desc = tx.getDescriptor(UUID.fromString(CCCD_UUID))
                if (desc != null) {
                    pendingCccdWrite = true
                    desc.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    gatt.writeDescriptor(desc)
                } else {
                    // 没有 CCCD，直接触发（某些设备不需要通知启用）
                    onConnected?.invoke()
                }
            } else {
                // 没有 TX 特征值，直接触发
                onConnected?.invoke()
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt, mtu: Int, status: Int) {
            android.util.Log.i("ble_mtu", "MTU negotiated: $mtu (status=$status)")
        }

        override fun onDescriptorWrite(
            gatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            if (pendingCccdWrite && descriptor.uuid.toString().equals(CCCD_UUID, ignoreCase = true)) {
                pendingCccdWrite = false
                onConnected?.invoke()
            }
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            val data = String(characteristic.value ?: return, Charsets.UTF_8).trim()
            if (data.isNotEmpty()) onDataReceived?.invoke(data)
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            val data = String(value, Charsets.UTF_8).trim()
            if (data.isNotEmpty()) onDataReceived?.invoke(data)
        }
    }
}
