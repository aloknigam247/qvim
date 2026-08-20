package com.qvim.companion

import android.content.Context
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.core.content.edit
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.qvim.companion.net.NsdMirrorDiscovery
import com.qvim.companion.ui.ChatScreen
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {

    private val vm: ChatViewModel by viewModels {
        object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : ViewModel> create(modelClass: Class<T>): T =
                ChatViewModel(discovery = NsdMirrorDiscovery(applicationContext)) as T
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        prefs.getString(KEY_ENDPOINT, null)?.let { vm.setEndpoint(it) }

        // Discovery runs only while the activity is at least STARTED; the block is
        // cancelled on STOP so the multicast lock and NSD session are released, and
        // re-entered (idempotently) on the next STARTED after a config change.
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                vm.startDiscovery()
                try {
                    awaitCancellation()
                } finally {
                    vm.stopDiscovery()
                }
            }
        }

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
