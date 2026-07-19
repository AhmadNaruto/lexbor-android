// build.gradle.kts
//
// Android Library build configuration for lexbor-jni.
// Add this module as an Android library or integrate these blocks
// into your existing app/library build.gradle.kts.

plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.example.lexbor"
    compileSdk = 34 // Adjust based on your target SDK

    defaultConfig {
        minSdk = 21 // Minimal SDK targeting modern devices
        
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        consumerProguardFiles("consumer-rules.pro")

        externalNativeBuild {
            cmake {
                // Pass the path to your Lexbor source tree.
                // By default, it expects the lexbor folder to be sibling to lexbor-jni.
                arguments("-DLEXBOR_ROOT=${project.rootDir}/../lexbor")
                
                // Targets arm64-v8a specifically as requested.
                // You can add others (like "armeabi-v7a", "x86_64" for emulator) if needed.
                abiFilters("arm64-v8a")
                
                // Add compiler flags if needed
                cppFlags("-std=c++17 -Wall -O2")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    externalNativeBuild {
        cmake {
            path = file("CMakeLists.txt")
            version = "3.22.1" // Adjust to the CMake version installed in your SDK Manager
        }
    }

    kotlinOptions {
        jvmTarget = "1.8"
    }
}

dependencies {
    // Only standard stdlib is needed; thin wrapper has no 3rd-party JVM dependencies.
    implementation("org.jetbrains.kotlin:kotlin-stdlib:1.9.20")
}
