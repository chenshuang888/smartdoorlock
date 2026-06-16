buildscript {
    dependencies {
        classpath("javax.xml.bind:jaxb-api:2.3.1")
        classpath("org.glassfish.jaxb:jaxb-runtime:2.3.8")
    }
}

plugins {
    id("com.android.application") version "8.2.2" apply false
    id("org.jetbrains.kotlin.android") version "1.9.22" apply false
}
