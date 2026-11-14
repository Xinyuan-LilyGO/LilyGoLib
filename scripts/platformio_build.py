Import("env")

from os.path import join


framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
env.Append(
    CPPPATH=[
        join(framework_dir, "libraries", "SPI", "src"),
        join(framework_dir, "libraries", "Wire", "src"),
    ]
)
