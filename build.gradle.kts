plugins {
    alias(libs.plugins.android.library)
    id("org.jetbrains.kotlin.android") version "2.4.10"
}

import com.android.build.api.dsl.LibraryExtension

extensions.configure<LibraryExtension> {
    namespace = "io.github.lexbor_jni"
    compileSdk = 34
    ndkVersion = "29.0.14206865"

    defaultConfig {
        minSdk = 21
        consumerProguardFiles("consumer-rules.pro")

        externalNativeBuild {
            cmake {
                // Pass the path to your Lexbor source tree.
                // By default, it expects the lexbor folder to be sibling to lexbor-jni.
                arguments("-DLEXBOR_ROOT=${projectDir}/lexbor")
                
                // Targets arm64-v8a specifically as requested.
                // You can add others (like "armeabi-v7a", "x86_64" for emulator) if needed.
                abiFilters("arm64-v8a")
                
                // Add compiler flags if needed
                cppFlags("-std=c++17 -Wall -O2")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("CMakeLists.txt")
            version = "3.22.1" // Adjust to the CMake version installed in your SDK Manager
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_11)
    }
}

dependencies {
    implementation(libs.kotlin.stdlib)
}

