package com.qvim.companion

import android.content.Context
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.core.content.edit
import com.qvim.companion.ui.ChatScreen

class MainActivity : ComponentActivity() {

    private val vm: ChatViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        prefs.getString(KEY_ENDPOINT, null)?.let { vm.setEndpoint(it) }

        setContent {
            MaterialTheme {
                Surface {
                    ChatScreen(
                        vm = vm,
                        onEndpointSaved = { endpoint ->
                            prefs.edit { putString(KEY_ENDPOINT, endpoint) }
                        },
                    )
                }
            }
        }
    }

    private companion object {
        const val PREFS = "qvim"
        const val KEY_ENDPOINT = "endpoint"
    }
}
