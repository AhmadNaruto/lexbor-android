# Proguard rules for lexbor-jni library.
# These rules are automatically consumed by app modules using this library.

# Prevent obfuscation or removal of native methods and their class definitions
-keepclasseswithmembernames class * {
    native <methods>;
}

# Explicitly keep our wrapper classes and their members so JNI_OnLoad caching works
-keep class com.example.lexbor.HtmlDocument { *; }
-keep class com.example.lexbor.NodeList { *; }
-keep class com.example.lexbor.Node { *; }
