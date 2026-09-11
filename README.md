______________________
libse compilation:
______________________


cmake . -DLEGACY=ON/OFF -DMULTI_THREADING=ON/OFF
make
make clean_all (remove all files generated)

______________________
se_hal compilation:
______________________
by default GP-SPI


aidl/Android.bp
===================================
[GP MODE]

    srcs: [
        "SecureElement.cpp",
        "ThalesService.cpp",
    ],


    local_include_dirs: [
        "se-thales",
    ],

    shared_libs: [
        "libbinder_ndk",
        "android.hardware.secure_element-V1-ndk",
        "android.hardware.secure_element.thales.libse-thales",


aidl/Android.bp
===================================

[LEGACY MODE]

   srcs: [
        "SecureElement-legacy.cpp",
        "ThalesService.cpp",
    ],


    local_include_dirs: [
        "se-thales-legacy",
    ],

    shared_libs: [
        "libbinder_ndk",
        "android.hardware.secure_element-V1-ndk",
        "android.hardware.secure_element.thales.libse-thales-legacy",
