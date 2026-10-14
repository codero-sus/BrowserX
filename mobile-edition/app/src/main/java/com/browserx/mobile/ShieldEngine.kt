// BrowserX — (c) 2026 codero-sus. All rights reserved.
// Personal, non-commercial use only. No modification, no redistribution or resale.
// Source-available, not open source — see LICENSE. https://the-codero.web.app

package com.browserx.mobile

import android.content.Context
import android.net.Uri
import android.util.Log
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import java.io.ByteArrayInputStream

/**
 * The tracker shield on Android.
 *
 * `WebViewClient.shouldInterceptRequest` is the one place Android lets us inspect
 * a subresource before it is fetched, so that is where the shared core's
 * blocklist is consulted. The decisions themselves come from
 * `src/core/blocklist.cc` over JNI — same rules, same allowlist, same categories
 * as the desktop edition.
 *
 * Two Android-only details:
 *  * A blocked request must return a *response*, never null: null means "no
 *    opinion, please continue", which would silently let the tracker through.
 *  * In LOW RAM / ECO modes the shield additionally blocks images, fonts and
 *    media, because the mode explicitly asked for it (the desktop passes the same
 *    intent to Chromium as --blink-settings / resource policy).
 */
class ShieldEngine(private val context: Context) {

    interface Listener {
        fun onBlocked(host: String, category: String)
        fun onBlockedCountChanged(count: Int, savedKb: Int)
    }

    var listener: Listener? = null
    var enabled: Boolean = true
        set(value) {
            field = value
            if (!value) reset()
        }

    /** Set by ModeController from the resolved plan. */
    var blockImages = false
    var blockMedia = false
    var blockFonts = false

    /**
     * Third-party heavy resources only - images, media and fonts. Scripts and
     * stylesheets are never cut by this flag, because most sites serve both from a
     * CDN and blocking them breaks the page instead of saving anything. Mirrors
     * `core::DecideRequest`.
     */
    var blockThirdParty = false

    private val allowlisted = mutableSetOf<String>()
    private val blockedByHost = mutableMapOf<String, Int>()
    private var blockedTotal = 0
    private var savedBytes = 0L
    private val decisionCache = mutableMapOf<String, ShieldVerdict>()

    fun allowlist(host: String) {
        allowlisted += host
        decisionCache.clear()
        reset()
    }

    fun removeAllowlist(host: String) {
        allowlisted -= host
        decisionCache.clear()
    }

    fun reset() {
        blockedTotal = 0
        savedBytes = 0
        blockedByHost.clear()
        notifyCount()
    }

    fun blockedCount(): Int = blockedTotal
    fun savedKb(): Int = (savedBytes / 1024).toInt()

    /** The WebViewClient hook. Returns null when the request may proceed. */
    fun intercept(documentUrl: String?, request: WebResourceRequest, requestType: String): WebResourceResponse? {
        if (!enabled) return null
        val url = request.url ?: return null
        // Never touch our own pages, about: URLs or the allowlisted site itself.
        val scheme = url.scheme ?: return null
        if (scheme != "http" && scheme != "https") return null
        val requestHost = url.host ?: return null
        val documentHost = documentUrl?.let { Uri.parse(it).host } ?: ""
        if (requestHost in allowlisted) return null

        if (documentHost.isNotEmpty() && documentHost == requestHost) return null

        val thirdParty = documentHost.isNotEmpty() && !requestHost.endsWith(documentHost)

        // The mode's resource policy, in the same order as core::DecideRequest:
        // block_images/media/fonts stop every request of that type (a request that is
        // never sent is the only one that really saves data), while block_third_party
        // reaches third-party copies of the heavy types and nothing else.
        if (shouldBlockByType(requestType)) {
            note(requestHost, requestType)
            return emptyResponse()
        }
        if (blockThirdParty && thirdParty && isHeavyType(requestType)) {
            note(requestHost, "$requestType (third party)")
            return emptyResponse()
        }

        val cacheKey = "$documentHost|$requestHost|${url.path}"
        val verdict = decisionCache.getOrPut(cacheKey) {
            PerformanceCore.shieldDecision(documentHost, requestHost, url.path ?: "", requestType)
                .verdict
        }
        if (verdict == ShieldVerdict.BLOCK) {
            note(requestHost, "tracker")
            return emptyResponse()
        }
        return null
    }

    private fun shouldBlockByType(requestType: String): Boolean = when (requestType) {
        "image" -> blockImages
        "media" -> blockMedia
        "font" -> blockFonts
        else -> false
    }

    /** The types a mode may cut when they come from somewhere else. */
    private fun isHeavyType(requestType: String): Boolean =
        requestType == "image" || requestType == "media" || requestType == "font"

    private fun note(host: String, category: String) {
        blockedTotal++
        blockedByHost[host] = (blockedByHost[host] ?: 0) + 1
        savedBytes += estimateBytes(category)
        listener?.onBlocked(host, category)
        notifyCount()
    }

    private fun notifyCount() {
        listener?.onBlockedCountChanged(blockedTotal, savedKb())
    }

    /**
     * A blocked subresource answers with an empty 200 and no cache: the page sees
     * a successful-but-empty fetch, which is what most blocked requests should look
     * like to keep JS from treating it as a network failure.
     */
    private fun emptyResponse(): WebResourceResponse = WebResourceResponse(
        "text/plain",
        "utf-8",
        200,
        "OK",
        mapOf("Cache-Control" to "no-store", "Access-Control-Allow-Origin" to "*"),
        ByteArrayInputStream(ByteArray(0)),
    )

    private fun estimateBytes(category: String): Long = when (category) {
        "image" -> 90_000
        "media" -> 400_000
        "script" -> 45_000
        "font" -> 30_000
        else -> 6_000
    }

    /** Per-host breakdown, shown in the shield panel. */
    fun topBlockedHosts(limit: Int = 8): List<Pair<String, Int>> =
        blockedByHost.entries.sortedByDescending { it.value }.take(limit).map { it.key to it.value }

    companion object {
        private const val TAG = "BrowserXShield"

        /** Maps an Android request type onto the core's ResourceType id. */
        fun typeFor(webViewType: String): String = when (webViewType) {
            "image" -> "image"
            "media" -> "media"
            "font" -> "font"
            "script" -> "script"
            "stylesheet" -> "stylesheet"
            "document" -> "document"
            else -> "other"
        }

        fun log(host: String, category: String) {
            Log.d(TAG, "blocked $host ($category)")
        }
    }
}
