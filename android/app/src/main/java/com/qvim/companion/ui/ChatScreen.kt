package com.qvim.companion.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.qvim.companion.ChatViewModel
import com.qvim.companion.model.SessionInfo
import com.qvim.companion.model.UiMessage
import com.qvim.companion.net.ConnectionState

@Composable
fun ChatScreen(vm: ChatViewModel, onEndpointSaved: (String) -> Unit, modifier: Modifier = Modifier) {
    val messages by vm.messages.collectAsStateWithLifecycle()
    val state by vm.connectionState.collectAsStateWithLifecycle()
    val endpoint by vm.endpoint.collectAsStateWithLifecycle()
    val sessions by vm.sessions.collectAsStateWithLifecycle()
    val selectedSession by vm.selectedSession.collectAsStateWithLifecycle()

    var draft by remember { mutableStateOf("") }
    val listState = rememberLazyListState()

    LaunchedEffect(messages.size) {
        if (messages.isNotEmpty()) listState.animateScrollToItem(messages.size - 1)
    }

    val choosing = state == ConnectionState.Connected && selectedSession == null && sessions.isNotEmpty()

    Column(modifier = modifier.fillMaxSize().padding(12.dp)) {
        EndpointBar(
            endpoint = endpoint,
            state = state,
            onEndpointChange = vm::setEndpoint,
            onConnect = {
                onEndpointSaved(endpoint)
                vm.connect()
            },
        )

        if (choosing) {
            SessionPicker(
                sessions = sessions,
                onSelect = vm::selectSession,
                modifier = Modifier.weight(1f).fillMaxWidth(),
            )
        } else {
            if (selectedSession != null && sessions.isNotEmpty()) {
                Row(modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp)) {
                    OutlinedButton(onClick = vm::switchSession) { Text("Sessions") }
                }
            }

            LazyColumn(
                state = listState,
                modifier = Modifier.weight(1f).fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                items(messages, key = { it.id }) { msg -> MessageBubble(msg) }
            }

            Row(
                modifier = Modifier.fillMaxWidth().padding(top = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                OutlinedTextField(
                    value = draft,
                    onValueChange = { draft = it },
                    modifier = Modifier.weight(1f),
                    singleLine = true,
                    placeholder = { Text("Message") },
                )
                Button(
                    onClick = {
                        vm.send(draft)
                        draft = ""
                    },
                    modifier = Modifier.padding(start = 8.dp),
                ) { Text("Send") }
            }
        }
    }
}

@Composable
private fun SessionPicker(sessions: List<SessionInfo>, onSelect: (SessionInfo) -> Unit, modifier: Modifier = Modifier) {
    Column(modifier = modifier, verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(
            text = "Select a session",
            style = MaterialTheme.typography.titleMedium,
            modifier = Modifier.padding(bottom = 4.dp),
        )
        LazyColumn(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            items(sessions, key = { "${it.hostId}|${it.resource}" }) { session ->
                OutlinedButton(
                    onClick = { onSelect(session) },
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Column(modifier = Modifier.fillMaxWidth()) {
                        if (session.hostLabel.isNotEmpty()) {
                            Text(session.hostLabel, style = MaterialTheme.typography.labelSmall)
                        }
                        Text(session.title, style = MaterialTheme.typography.bodyLarge)
                        Text(
                            text = session.resource.substringAfterLast('/'),
                            style = MaterialTheme.typography.labelSmall,
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun EndpointBar(
    endpoint: String,
    state: ConnectionState,
    onEndpointChange: (String) -> Unit,
    onConnect: () -> Unit,
) {
    Column(modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value = endpoint,
                onValueChange = onEndpointChange,
                modifier = Modifier.weight(1f),
                singleLine = true,
                label = { Text("AHP server") },
                placeholder = { Text("host:port") },
            )
            OutlinedButton(
                onClick = onConnect,
                enabled = endpoint.isNotBlank(),
                modifier = Modifier.padding(start = 8.dp),
            ) { Text("Connect") }
        }
        Text(
            text = "Status: ${state.name}",
            style = MaterialTheme.typography.labelMedium,
            modifier = Modifier.padding(top = 4.dp),
        )
    }
}

@Composable
private fun MessageBubble(msg: UiMessage) {
    val isUser = msg.role == "user"
    val header =
        when {
            isUser -> "you"
            msg.kind == "message" -> "assistant"
            else -> msg.kind
        }
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors =
            CardDefaults.cardColors(
                containerColor = bubbleColor(msg),
                contentColor = Color(0xFF101010),
            ),
    ) {
        Column(modifier = Modifier.padding(10.dp)) {
            Text(
                text = header,
                style = MaterialTheme.typography.labelSmall,
            )
            Text(
                text = msg.text.ifEmpty { if (msg.streaming) "…" else "" },
                style = MaterialTheme.typography.bodyLarge,
                textAlign = TextAlign.Start,
            )
        }
    }
}

/** Distinct background per AHP part kind so each box is visually separable. */
private fun bubbleColor(msg: UiMessage): Color =
    when {
        msg.role == "user" -> Color(0xFFBBDEFB)
        else ->
            when (msg.kind) {
                "markdown" -> Color(0xFFC8E6C9)
                "reasoning" -> Color(0xFFE1BEE7)
                "toolCall" -> Color(0xFFB2DFDB)
                "inputRequest" -> Color(0xFFFFE0B2)
                "error" -> Color(0xFFFFCDD2)
                "systemNotification" -> Color(0xFFCFD8DC)
                "contentRef" -> Color(0xFFB3E5FC)
                "resource" -> Color(0xFFDCEDC8)
                "message" -> Color(0xFFECEFF1)
                else -> Color(0xFFEEEEEE)
            }
    }
