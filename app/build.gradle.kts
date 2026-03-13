plugins {
    alias(libs.plugins.android.application)
}

// x86_64 x86 下编译 bhook 需要做:
apply(from = rootProject.file("gradle/prefab_bypass.gradle"))

android {
    namespace = "com.example.demo_so"
    compileSdk {
        version = release(36) {
            minorApiLevel = 1
        }
    }

    ndkPath = "/opt/android-ndk"
    ndkVersion = "29.0.14206865"  // 改成实际的 r29 版本号

    defaultConfig {
        applicationId = "com.example.demo_so"
        minSdk = 24
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        ndk {  // 删除 "x86" 和 "armeabi-v7a"
            abiFilters.clear()  // 清除默认的
            abiFilters += listOf("x86_64", "arm64-v8a", "armeabi-v7a", "x86")
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
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    buildFeatures {
        viewBinding = true

        // bhook README要求
        prefab = true
    }

    /*作用：解决 libbytehook.so 重复打包冲突。*/
    /*为什么必须：
        ByteHook 库本身包含 libbytehook.so，
        你的项目也会生成 libdemo_so.so 和 libso2.so，
        Gradle 可能会检测到重复的 so 文件而报错。
    */
    packaging {
        jniLibs.pickFirsts.add("**/libbytehook.so")
    }

}

dependencies {
    // bhook
    implementation("com.bytedance:bytehook:1.1.1")
    implementation(libs.appcompat)
    implementation(libs.material)
    implementation(libs.constraintlayout)
    testImplementation(libs.junit)
    androidTestImplementation(libs.ext.junit)
    androidTestImplementation(libs.espresso.core)
}