package com.qvim.companion.model

/**
 * A selectable AHP session, projected from a host `listSessions` summary.
 *
 * [hostId] / [hostLabel] identify the owning cptower host when several are merged into
 * one picker; both are empty for a single direct connection.
 */
data class SessionInfo(val resource: String, val title: String, val hostId: String = "", val hostLabel: String = "")
