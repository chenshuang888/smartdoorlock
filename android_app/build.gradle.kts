buildscript {
    dependencies {
        classpath("jakarta.xml.bind:jakarta.xml.bind-api:4.0.0")
    }
}

plugins {
    id("com.android.application") version "8.2.2" apply false
    id("org.jetbrains.kotlin.android") version "1.9.22" apply false
}
