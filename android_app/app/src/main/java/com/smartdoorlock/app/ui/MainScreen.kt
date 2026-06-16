package com.smartdoorlock.app.ui

import android.content.pm.PackageManager
import android.Manifest
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import com.smartdoorlock.app.BleManager
import com.smartdoorlock.app.DoorLockApi
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

private sealed class UiState {
    object Disconnected : UiState()
    object Scanning : UiState()
    object Connected : UiState()
    object Authenticated : UiState()
    data class Failed(val reason: String) : UiState()
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen(
    api: DoorLockApi,
    bleManager: BleManager,
    onLogout: () -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }

    var uiState by remember { mutableStateOf<UiState>(UiState.Disconnected) }
    var bleKey by remember { mutableStateOf(api.getBleKey()) }
    var deviceName by remember { mutableStateOf("") }
    var username by remember { mutableStateOf(api.getUsername() ?: "") }
    var showPwdDialog by remember { mutableStateOf(false) }
    var pwdInput by remember { mutableStateOf("") }

    // BLE 权限请求
    val permLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestMultiplePermissions()
    ) { granted ->
        if (granted.all { it.value }) {
            checkLocationAndScan(bleManager, context, scope, snackbarHostState)
        } else {
            scope.launch { snackbarHostState.showSnackbar("需要蓝牙权限才能连接门锁") }
        }
    }

    // 启动时从服务端同步密钥
    LaunchedEffect(Unit) {
        try {
            withContext(Dispatchers.IO) {
                val res = api.getKey()
                val key = res.optString("key", "")
                if (key.length >= 32) {
                    api.saveBleKey(key)
                    bleKey = key
                }
            }
        } catch (_: Exception) {}
    }

    // BLE 回调
    LaunchedEffect(bleManager) {
        bleManager.onStartScan = { uiState = UiState.Scanning }
        bleManager.onScanFailed = { msg ->
            scope.launch { snackbarHostState.showSnackbar(msg) }
            uiState = UiState.Disconnected
        }
        bleManager.onDeviceFound = { }
        bleManager.onDeviceNotFound = {
            scope.launch { snackbarHostState.showSnackbar("未找到门锁，请确保门锁处于广播状态") }
            uiState = UiState.Disconnected
        }
        bleManager.onConnected = {
            uiState = UiState.Connected
            scope.launch {
                kotlinx.coroutines.delay(500)
                bleManager.send("[HELLO]\n")
            }
        }
        bleManager.onDisconnected = {
            uiState = UiState.Disconnected
            deviceName = ""
        }
        bleManager.onDataReceived = { text ->
            handleBleData(text, bleManager, api, { uiState = it }, { k -> bleKey = k; api.saveBleKey(k) }, { msg -> scope.launch { snackbarHostState.showSnackbar(msg) } }, { d -> deviceName = d })
        }
    }

    // 清理
    DisposableEffect(Unit) {
        onDispose { bleManager.disconnect() }
    }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbarHostState) },
        containerColor = Color(0xFF0b0b14)
    ) { pad ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(pad)
                .padding(horizontal = 20.dp)
                .verticalScroll(rememberScrollState()),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            // ── Top Bar ──
            Row(
                modifier = Modifier.fillMaxWidth().padding(top = 16.dp, bottom = 20.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("🔐 智能门锁", fontSize = 20.sp, fontWeight = FontWeight.Bold, color = Color(0xFF0f9b8e), modifier = Modifier.weight(1f))
                Text(username, fontSize = 13.sp, color = Color(0xFF555555))
                Spacer(Modifier.width(12.dp))
                TextButton(onClick = onLogout) {
                    Text("退出", color = Color(0xFFe74c3c), fontSize = 12.sp)
                }
            }

            // ── Status Card ──
            Surface(
                shape = RoundedCornerShape(16.dp),
                color = Color(0xFF161628).copy(alpha = 0.85f)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(20.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    val dotColor = when (uiState) {
                        is UiState.Authenticated -> Color(0xFF2ecc71)
                        is UiState.Connected, is UiState.Scanning -> Color(0xFF0f9b8e)
                        else -> Color(0xFF555555)
                    }
                    Box(modifier = Modifier.size(10.dp).background(dotColor, RoundedCornerShape(5.dp)))
                    Spacer(Modifier.width(14.dp))
                    Column(Modifier.weight(1f)) {
                        val statusText = when (uiState) {
                            is UiState.Disconnected -> "未连接"
                            is UiState.Scanning -> "正在扫描…"
                            is UiState.Connected -> "已连接，正在认证…"
                            is UiState.Authenticated -> "已认证"
                            is UiState.Failed -> "认证失败"
                        }
                        Text(statusText, fontSize = 14.sp, color = Color(0xFF888888))
                        if (deviceName.isNotEmpty()) Text(deviceName, fontSize = 11.sp, color = Color(0xFF333333))
                    }
                    if (uiState is UiState.Disconnected) {
                        TextButton(onClick = { checkAndConnect(bleManager, permLauncher, context, scope, snackbarHostState) }) {
                            Text("连接", color = Color(0xFF0f9b8e), fontSize = 12.sp)
                        }
                    } else {
                        TextButton(onClick = { bleManager.disconnect(); uiState = UiState.Disconnected }) {
                            Text("断开", color = Color(0xFFe74c3c), fontSize = 12.sp)
                        }
                    }
                }
            }

            Spacer(Modifier.height(24.dp))

            // ── Lock Icon ──
            val isUnlocked = uiState is UiState.Authenticated
            Text(if (isUnlocked) "🔓" else "🔒", fontSize = 72.sp)
            Spacer(Modifier.height(6.dp))

            val lockText = when (uiState) {
                is UiState.Disconnected -> "未连接"
                is UiState.Scanning -> "正在搜索…"
                is UiState.Connected -> "正在认证…"
                is UiState.Authenticated -> "已解锁"
                is UiState.Failed -> "认证失败"
            }
            val lockColor = when (uiState) {
                is UiState.Authenticated -> Color(0xFF2ecc71)
                is UiState.Failed -> Color(0xFFe74c3c)
                is UiState.Connected -> Color(0xFF0f9b8e)
                else -> Color(0xFF666666)
            }
            Text(lockText, fontSize = 22.sp, fontWeight = FontWeight.Bold, color = lockColor)
            if (deviceName.isNotEmpty()) Text(deviceName, fontSize = 11.sp, color = Color(0xFF333333))

            Spacer(Modifier.height(8.dp))

            // ── Main Button ──
            val btnText = when (uiState) {
                is UiState.Disconnected -> "连接门锁"
                is UiState.Scanning -> "正在搜索…"
                else -> "断开连接"
            }
            val btnColor = if (uiState is UiState.Authenticated || uiState is UiState.Connected)
                Color(0xFFc0392b) else Color(0xFF0f9b8e)
            Button(
                onClick = {
                    if (uiState is UiState.Disconnected || uiState is UiState.Scanning) {
                        checkAndConnect(bleManager, permLauncher, context, scope, snackbarHostState)
                    } else {
                        bleManager.disconnect()
                        uiState = UiState.Disconnected
                    }
                },
                modifier = Modifier.fillMaxWidth(0.7f).height(52.dp),
                shape = RoundedCornerShape(12.dp),
                colors = ButtonDefaults.buttonColors(containerColor = btnColor)
            ) { Text(btnText, fontSize = 16.sp, fontWeight = FontWeight.SemiBold) }

            Spacer(Modifier.height(20.dp))

            // ── Action Grid ──
            AnimatedVisibility(visible = uiState is UiState.Authenticated) {
                Column {
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        ActionCard(icon = "🔑", label = "查询密码", modifier = Modifier.weight(1f), onClick = {
                            if (bleManager.send("[PWD_QUERY]\n")) scope.launch { snackbarHostState.showSnackbar("正在查询…") }
                        })
                        ActionCard(icon = "✏️", label = "修改密码", modifier = Modifier.weight(1f), onClick = { showPwdDialog = true; pwdInput = "" })
                    }
                    Spacer(Modifier.height(10.dp))
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        ActionCard(icon = "🗑", label = "解绑门锁", modifier = Modifier.weight(1f), isDanger = true, onClick = {
                            if (bleManager.send("[UNPAIR]\n")) scope.launch { snackbarHostState.showSnackbar("正在解绑…") }
                        })
                    }
                }
            }
        }
    }

    // ── 修改密码对话框 ──
    if (showPwdDialog) {
        AlertDialog(
            onDismissRequest = { showPwdDialog = false },
            shape = RoundedCornerShape(16.dp),
            containerColor = Color(0xFF161628),
            title = { Text("✏️ 修改密码", textAlign = TextAlign.Center, modifier = Modifier.fillMaxWidth(), color = Color(0xFFe0e0e0), fontWeight = FontWeight.Bold) },
            text = {
                OutlinedTextField(
                    value = pwdInput, onValueChange = { if (it.length <= 6) pwdInput = it },
                    modifier = Modifier.fillMaxWidth(),
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
                    placeholder = { Text("6位数字", color = Color(0xFF555555)) },
                    singleLine = true,
                    textStyle = LocalTextStyle.current.copy(textAlign = TextAlign.Center, fontSize = 22.sp, letterSpacing = 10.sp, color = Color(0xFFe0e0e0)),
                    colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = Color(0xFF0f9b8e), unfocusedBorderColor = Color(0xFF2a2a3e), cursorColor = Color(0xFF0f9b8e))
                )
            },
            confirmButton = {
                Button(onClick = {
                    if (pwdInput.length != 6 || !pwdInput.all { it.isDigit() }) {
                        scope.launch { snackbarHostState.showSnackbar("请输入6位数字") }; return@Button
                    }
                    showPwdDialog = false
                    if (bleManager.send("[PWD_SET:$pwdInput]\n")) scope.launch { snackbarHostState.showSnackbar("正在设置…") }
                }, colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF0f9b8e))) { Text("确认", color = Color.White) }
            },
            dismissButton = { TextButton(onClick = { showPwdDialog = false }) { Text("取消", color = Color(0xFF999999)) } }
        )
    }
}

@Composable
private fun ActionCard(icon: String, label: String, modifier: Modifier = Modifier, isDanger: Boolean = false, onClick: () -> Unit) {
    Surface(
        modifier = modifier.clickable(onClick = onClick),
        shape = RoundedCornerShape(12.dp),
        color = Color(0xFF161628).copy(alpha = 0.7f)
    ) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(vertical = 20.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(icon, fontSize = 28.sp)
            Spacer(Modifier.height(6.dp))
            Text(label, fontSize = 13.sp, color = if (isDanger) Color(0xFFe74c3c) else Color(0xFFaaaaaa))
        }
    }
}

private fun checkLocationAndScan(
    bleManager: BleManager,
    context: android.content.Context,
    scope: CoroutineScope,
    snackbarHostState: SnackbarHostState
) {
    // Android 需要位置服务开启才能进行 BLE 扫描（大多数国产手机强依赖）
    val locManager = context.getSystemService(android.content.Context.LOCATION_SERVICE) as? android.location.LocationManager
    val locEnabled = locManager?.isProviderEnabled(android.location.LocationManager.GPS_PROVIDER) == true ||
                    locManager?.isProviderEnabled(android.location.LocationManager.NETWORK_PROVIDER) == true
    if (!locEnabled) {
        scope.launch { snackbarHostState.showSnackbar("请在系统设置中开启位置服务，否则搜不到蓝牙设备") }
    }
    bleManager.startScan()
}

private fun checkAndConnect(
    bleManager: BleManager,
    permLauncher: ActivityResultLauncher<Array<String>>,
    context: android.content.Context,
    scope: CoroutineScope,
    snackbarHostState: SnackbarHostState
) {
    if (!bleManager.isBluetoothEnabled()) {
        scope.launch { snackbarHostState.showSnackbar("请先开启蓝牙") }
        return
    }
    val perms = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.ACCESS_FINE_LOCATION)
    } else {
        arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
    }
    if (perms.all { ContextCompat.checkSelfPermission(context, it) == PackageManager.PERMISSION_GRANTED }) {
        checkLocationAndScan(bleManager, context, scope, snackbarHostState)
    } else {
        permLauncher.launch(perms)
    }
}

private fun handleBleData(
    text: String, bleManager: BleManager, api: DoorLockApi,
    setUiState: (UiState) -> Unit, setBleKey: (String) -> Unit,
    showMsg: (String) -> Unit, setDeviceName: (String) -> Unit
) {
    when {
        text.startsWith("[BOND] ") -> {
            val hex = text.removePrefix("[BOND] ").trim()
            if (hex.length < 32) {
                showMsg("⚠️ [BOND] 密钥不完整: len=${hex.length} hex=$hex")
                return
            }
            setBleKey(hex)
            showMsg("收到配对密钥")
            try { api.saveKey(hex) } catch (_: Exception) {}
            bleManager.send("[AUTH] $hex\n")
            logBleAction(api, "配对", "收到新密钥")
        }
        text.startsWith("[READY]") -> {
            var key = api.getBleKey()
            if (key == null) {
                key = try {
                    val res = api.getKey()
                    val k = res.optString("key", "")
                    if (k.length >= 32) { api.saveBleKey(k); k } else null
                } catch (_: Exception) { null }
            }
            if (key != null) {
                bleManager.send("[AUTH] $key\n")
            } else {
                showMsg("未配对，请按门锁 # 键进入配对模式")
            }
        }
        text.startsWith("[OK]") -> {
            setUiState(UiState.Authenticated)
            showMsg("门锁已解锁 ✓")
            logBleAction(api, "开锁", "认证成功")
            setDeviceName(BleManager.DEVICE_NAME)
        }
        text.startsWith("[FAIL]") -> {
            setUiState(UiState.Failed("密钥不匹配"))
            showMsg("认证失败，密钥不匹配")
            logBleAction(api, "开锁失败", "密钥不匹配")
        }
        text.startsWith("[UNPAIRED]") -> {
            api.deleteBleKey()
            showMsg("门锁已解绑")
            bleManager.disconnect()
            setUiState(UiState.Disconnected)
            logBleAction(api, "解绑", "")
        }
        text.startsWith("[PWD_DATA:") -> {
            val pwd = text.removeSurrounding(prefix = "[PWD_DATA:", suffix = "]")
            showMsg("当前密码: $pwd")
            logBleAction(api, "查询密码", "")
        }
        text.startsWith("[PWD_OK]") -> {
            showMsg("密码修改成功 ✓")
            logBleAction(api, "修改密码", "成功")
        }
        text.startsWith("[PWD_ERR]") -> {
            showMsg("密码修改失败 ✗")
            logBleAction(api, "修改密码", "失败")
        }
    }
}

private fun logBleAction(api: DoorLockApi, action: String, detail: String) {
    kotlinx.coroutines.GlobalScope.launch(Dispatchers.IO) {
        try { api.addLog(action, detail) } catch (_: Exception) {}
    }
}
