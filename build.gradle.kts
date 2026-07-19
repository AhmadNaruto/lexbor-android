plugins {
    alias(libs.plugins.android.library)
}

import com.android.build.api.dsl.LibraryExtension

extensions.configure<LibraryExtension> {
    namespace = "io.github.lexbor_jni"
    compileSdk = 37
    ndkVersion = "29.0.14206865"

    defaultConfig {
        minSdk = 30
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
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }
}

kotlin {
    jvmToolchain(21)
}

dependencies {
    // stdlib dependency is added automatically by KGP
}

