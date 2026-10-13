# Keep the JNI entry points: the native library binds to these names by hand,
# so obfuscating them would break the bridge silently at runtime.
-keepclasseswithmembernames class com.browserx.mobile.PerformanceCore {
    native <methods>;
}
-keep class com.browserx.mobile.PerformanceCore { *; }

# WebViewClient/WebChromeClient subclasses are created anonymously; keep them.
-keep class * extends android.webkit.WebViewClient { *; }
-keep class * extends android.webkit.WebChromeClient { *; }
