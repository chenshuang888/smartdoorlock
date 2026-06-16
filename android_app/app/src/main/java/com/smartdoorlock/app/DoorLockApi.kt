package com.smartdoorlock.app

import android.content.Context
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONObject
import java.io.IOException
import java.util.concurrent.TimeUnit

class DoorLockApi(context: Context) {
    companion object {
        private const val BASE_URL = "https://blog.chenshuang.fun/api"
    }

    private val prefs = context.getSharedPreferences("doorlock", Context.MODE_PRIVATE)
    private val client = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(10, TimeUnit.SECONDS)
        .build()
    private val jsonMedia = "application/json; charset=utf-8".toMediaType()

    fun getToken(): String? = prefs.getString("token", null)

    fun saveToken(token: String) { prefs.edit().putString("token", token).apply() }

    fun clearToken() { prefs.edit().remove("token").remove("username").apply() }

    fun getUsername(): String? = prefs.getString("username", null)

    private fun saveUsername(name: String) { prefs.edit().putString("username", name).apply() }

    private fun api(method: String, path: String, body: JSONObject? = null): JSONObject {
        val url = "$BASE_URL$path"
        val rb = Request.Builder().url(url)
        val token = getToken()
        if (token != null) rb.addHeader("Authorization", "Bearer $token")
        val reqBody = if (body != null) body.toString().toRequestBody(jsonMedia) else null
        val request = rb.method(method, reqBody).build()
        val response = client.newCall(request).execute()
        val bodyStr = response.body?.string() ?: "{}"
        val json = JSONObject(bodyStr)
        if (!response.isSuccessful) {
            val detail = json.optString("detail", "请求失败 ($response)")
            throw IOException(detail)
        }
        return json
    }

    fun login(username: String, password: String): JSONObject {
        val body = JSONObject().apply {
            put("username", username)
            put("password", password)
        }
        val res = api("POST", "/login", body)
        saveToken(res.getString("token"))
        saveUsername(res.optString("username", username))
        return res
    }

    fun register(username: String, password: String): JSONObject {
        val body = JSONObject().apply {
            put("username", username)
            put("password", password)
        }
        val res = api("POST", "/register", body)
        saveToken(res.getString("token"))
        saveUsername(res.optString("username", username))
        return res
    }

    fun me(): JSONObject = api("GET", "/me")

    fun getKey(): JSONObject = api("GET", "/key")

    fun saveKey(key: String): JSONObject {
        return api("POST", "/key", JSONObject().apply { put("key", key) })
    }

    fun deleteKey(): JSONObject = api("DELETE", "/key")

    fun addLog(action: String, detail: String = ""): JSONObject {
        return api("POST", "/logs", JSONObject().apply {
            put("action", action)
            put("detail", detail)
        })
    }

    // BLE 密钥本地存储
    fun getBleKey(): String? = prefs.getString("ble_key", null)

    fun saveBleKey(key: String) { prefs.edit().putString("ble_key", key).apply() }

    fun deleteBleKey() { prefs.edit().remove("ble_key").apply() }
}
