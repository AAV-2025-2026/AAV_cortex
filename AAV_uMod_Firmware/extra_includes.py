Import("env")

env.Append(CPPPATH=[
    env.subst("$PROJECT_PACKAGES_DIR/framework-arduinoespressif32@3.20009.0/libraries/WiFi/src"),
    env.subst("$PROJECT_PACKAGES_DIR/framework-arduinoespressif32@3.20009.0/libraries/WiFiClientSecure/src"),
])
