package com.qvim.companion

import com.qvim.companion.net.MirrorDiscovery
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test

/**
 * Pins the endpoint-selection policy the ViewModel applies to discovered mirrors:
 * a discovered endpoint auto-fills and surfaces, but a manual edit always wins.
 *
 * (These run on the JVM via `gradlew test`. There is no Android CI on this repo, so
 * they are not exercised by the qvim C++/Qt CI pipeline.)
 */
@OptIn(ExperimentalCoroutinesApi::class)
class ChatViewModelDiscoveryTest {

    private val dispatcher = UnconfinedTestDispatcher()

    @Before
    fun setUp() {
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    @Test
    fun discoveredEndpointAutoFills() {
        val vm = ChatViewModel(
            discovery = MirrorDiscovery { flowOf("ws://10.0.0.5:8765") },
        )

        vm.startDiscovery()

        assertEquals("ws://10.0.0.5:8765", vm.discovered.value)
        assertEquals("ws://10.0.0.5:8765", vm.endpoint.value)
    }

    @Test
    fun manualEditWinsOverDiscovery() {
        val vm = ChatViewModel(
            discovery = MirrorDiscovery { flowOf("ws://10.0.0.5:8765") },
        )

        vm.setEndpoint("ws://192.168.1.50:8765")
        vm.startDiscovery()

        // The user's endpoint is not overwritten, but discovery still surfaces the find.
        assertEquals("ws://192.168.1.50:8765", vm.endpoint.value)
        assertEquals("ws://10.0.0.5:8765", vm.discovered.value)
    }

    @Test
    fun blankEndpointDefault() {
        val vm = ChatViewModel()

        // With nothing discovered and no manual entry, the endpoint stays blank so
        // the UI disables Connect rather than dialing a bogus default.
        assertEquals("", vm.endpoint.value)
    }
}
