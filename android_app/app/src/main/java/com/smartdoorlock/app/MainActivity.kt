package com.smartdoorlock.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import com.smartdoorlock.app.ui.LoginScreen
import com.smartdoorlock.app.ui.MainScreen

private val DarkColors = darkColorScheme(
    primary = Color(0xFF0f9b8e),
    onPrimary = Color.White,
    surface = Color(0xFF161628),
    onSurface = Color(0xFFe0e0e0),
    background = Color(0xFF0b0b14),
    onBackground = Color(0xFFe0e0e0),
    error = Color(0xFFe74c3c),
)

class MainActivity : ComponentActivity() {
    private lateinit var api: DoorLockApi
    private lateinit var bleManager: BleManager

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        api = DoorLockApi(this)
        bleManager = BleManager(this)

        setContent {
            MaterialTheme(colorScheme = DarkColors) {
                Column(modifier = Modifier.background(Color(0xFF0b0b14))) {
                    AppContent(api, bleManager)
                }
            }
        }
    }

    override fun onDestroy() {
        bleManager.disconnect()
        super.onDestroy()
    }
}

@Composable
fun AppContent(api: DoorLockApi, bleManager: BleManager) {
    var loggedIn by remember { mutableStateOf(api.getToken() != null) }

    if (loggedIn) {
        MainScreen(
            api = api,
            bleManager = bleManager,
            onLogout = {
                bleManager.disconnect()
                api.clearToken()
                loggedIn = false
            }
        )
    } else {
        LoginScreen(
            api = api,
            onLoginSuccess = { loggedIn = true }
        )
    }
}
