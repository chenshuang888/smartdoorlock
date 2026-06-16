package com.smartdoorlock.app.ui

import androidx.compose.animation.*
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.smartdoorlock.app.DoorLockApi
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import java.io.IOException

@Composable
fun LoginScreen(
    api: DoorLockApi,
    onLoginSuccess: () -> Unit
) {
    val scope = rememberCoroutineScope()
    var isLoginTab by remember { mutableStateOf(true) }
    var username by remember { mutableStateOf("") }
    var password by remember { mutableStateOf("") }
    var confirmPwd by remember { mutableStateOf("") }
    var loading by remember { mutableStateOf(false) }
    var errorMsg by remember { mutableStateOf<String?>(null) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(Color(0xFF0b0b14))
            .padding(horizontal = 32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        // Logo + Title
        Text(text = "🔐", fontSize = 40.sp)
        Spacer(Modifier.height(4.dp))
        Text("智能门锁", fontSize = 22.sp, fontWeight = FontWeight.Bold, color = Color(0xFF0f9b8e))
        Text("管理系统", fontSize = 13.sp, color = Color(0xFF555555))
        Spacer(Modifier.height(32.dp))

        // Card
        Surface(
            shape = RoundedCornerShape(20.dp),
            color = Color(0xFF161628).copy(alpha = 0.85f),
            tonalElevation = 0.dp,
            shadowElevation = 0.dp
        ) {
            Column(
                modifier = Modifier.fillMaxWidth().padding(24.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                // Tabs
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(Color(0xFF12121e), RoundedCornerShape(10.dp))
                        .padding(3.dp)
                ) {
                    listOf("登录" to true, "注册" to false).forEach { (label, isLogin) ->
                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .background(
                                    if (isLoginTab == isLogin) Color(0xFF0f9b8e) else Color.Transparent,
                                    RoundedCornerShape(8.dp)
                                )
                                .clickable { isLoginTab = isLogin }
                                .padding(vertical = 9.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                label,
                                fontSize = 14.sp,
                                color = if (isLoginTab == isLogin) Color.White else Color(0xFF666666),
                                fontWeight = if (isLoginTab == isLogin) FontWeight.SemiBold else FontWeight.Normal
                            )
                        }
                    }
                }
                Spacer(Modifier.height(20.dp))

                if (isLoginTab) {
                    // Login form
                    OutlinedTextField(
                        value = username, onValueChange = { username = it },
                        label = { Text("用户名") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                        colors = fieldColors(),
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Next)
                    )
                    Spacer(Modifier.height(10.dp))
                    OutlinedTextField(
                        value = password, onValueChange = { password = it },
                        label = { Text("密码") },
                        singleLine = true,
                        visualTransformation = PasswordVisualTransformation(),
                        modifier = Modifier.fillMaxWidth(),
                        colors = fieldColors(),
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
                        keyboardActions = KeyboardActions(onDone = {
                            scope.launch { doAuth(api, username, password, null, onLoginSuccess, { errorMsg = it }, { loading = it }) }
                        })
                    )
                    Spacer(Modifier.height(16.dp))
                    Button(
                        onClick = {
                            scope.launch { doAuth(api, username, password, null, onLoginSuccess, { errorMsg = it }, { loading = it }) }
                        },
                        enabled = !loading,
                        modifier = Modifier.fillMaxWidth().height(48.dp),
                        shape = RoundedCornerShape(10.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF0f9b8e))
                    ) {
                        Text(if (loading) "登录中…" else "登 录")
                    }
                } else {
                    // Register form
                    OutlinedTextField(
                        value = username, onValueChange = { username = it },
                        label = { Text("用户名") },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                        colors = fieldColors(),
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Next)
                    )
                    Spacer(Modifier.height(10.dp))
                    OutlinedTextField(
                        value = password, onValueChange = { password = it },
                        label = { Text("密码（至少6位）") },
                        singleLine = true,
                        visualTransformation = PasswordVisualTransformation(),
                        modifier = Modifier.fillMaxWidth(),
                        colors = fieldColors(),
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Next)
                    )
                    Spacer(Modifier.height(10.dp))
                    OutlinedTextField(
                        value = confirmPwd, onValueChange = { confirmPwd = it },
                        label = { Text("确认密码") },
                        singleLine = true,
                        visualTransformation = PasswordVisualTransformation(),
                        modifier = Modifier.fillMaxWidth(),
                        colors = fieldColors(),
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
                        keyboardActions = KeyboardActions(onDone = {
                            scope.launch { doAuth(api, username, password, confirmPwd, onLoginSuccess, { errorMsg = it }, { loading = it }) }
                        })
                    )
                    Spacer(Modifier.height(16.dp))
                    Button(
                        onClick = {
                            scope.launch { doAuth(api, username, password, confirmPwd, onLoginSuccess, { errorMsg = it }, { loading = it }) }
                        },
                        enabled = !loading,
                        modifier = Modifier.fillMaxWidth().height(48.dp),
                        shape = RoundedCornerShape(10.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF0f9b8e))
                    ) {
                        Text(if (loading) "注册中…" else "注 册")
                    }
                }

                // Error
                errorMsg?.let {
                    Spacer(Modifier.height(8.dp))
                    Text(it, color = Color(0xFFe74c3c), fontSize = 13.sp, textAlign = TextAlign.Center)
                }
            }
        }
    }
}

private suspend fun doAuth(
    api: DoorLockApi, username: String, password: String, confirmPwd: String?,
    onSuccess: () -> Unit, onError: (String) -> Unit, setLoading: (Boolean) -> Unit
) {
    if (username.isBlank() || password.isBlank()) {
        onError("请填写用户名和密码"); return
    }
    if (confirmPwd != null && password != confirmPwd) {
        onError("两次密码不一致"); return
    }
    setLoading(true)
    try {
        kotlinx.coroutines.withContext(Dispatchers.IO) {
            if (confirmPwd != null) api.register(username, password)
            else api.login(username, password)
        }
        onSuccess()
    } catch (e: IOException) {
        onError(e.message ?: "网络错误")
    } catch (e: Exception) {
        onError(e.message ?: "请求失败")
    } finally {
        setLoading(false)
    }
}

@Composable
private fun fieldColors() = OutlinedTextFieldDefaults.colors(
    focusedBorderColor = Color(0xFF0f9b8e),
    unfocusedBorderColor = Color(0xFF2a2a3e),
    cursorColor = Color(0xFF0f9b8e),
    focusedLabelColor = Color(0xFF0f9b8e),
    unfocusedLabelColor = Color(0xFF555555),
    focusedTextColor = Color(0xFFe0e0e0),
    unfocusedTextColor = Color(0xFFe0e0e0),
)
