# Build Instructions

## libse-thales Library

### Build
```bash
cd libse-thales #(or not)
cmake . -DLEGACY=ON/OFF -DMULTI_THREADING=ON/OFF
make
```

### Clean
```bash
make clean_all
```

---

## SecureElement HAL

### Configuration

**Default mode:** GP-SPI

#### GP Mode Configuration
Edit `aidl/Android.bp`:
```
srcs: [
    "SecureElement.cpp",
    "ThalesService.cpp",
]

local_include_dirs: [
    "se-thales",
]

shared_libs: [
    "libbinder_ndk",
    "android.hardware.secure_element-V1-ndk",
    "android.hardware.secure_element.thales.libse-thales",
]
```

#### Legacy Mode Configuration
Edit `aidl/Android.bp`:
```
srcs: [
    "SecureElement-legacy.cpp",
    "ThalesService.cpp",
]

local_include_dirs: [
    "se-thales-legacy",
]

shared_libs: [
    "libbinder_ndk",
    "android.hardware.secure_element-V1-ndk",
    "android.hardware.secure_element.thales.libse-thales-legacy",
]
```