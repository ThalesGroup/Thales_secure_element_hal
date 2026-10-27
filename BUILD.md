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

---

## 🧪 Tests

### Available Test Suites

| Test Suite | Binary | Location |
|------------|--------|----------|
| **SecureElement Unit Tests (GP)** | `SecureElement_GP_API_Test.bin` | `secure_element/tests/unitary_tests/` |
| **SecureElement Unit Tests (Legacy)** | `SecureElement_Legacy_API_Test.bin` | `secure_element/tests/unitary_tests/` |
| **libse-thales API (GP)** | `libse_gp_stress_api.bin` | `secure_element/libse-thales_tests/` |
| **libse-thales API (Legacy)** | `libse_legacy_stress_api.bin` | `secure_element/libse-thales_tests/` |
| **libse-thales Use Cases (GP)** | `libse_GP_Use_Cases.bin` | `secure_element/libse-thales_tests/` |
| **libse-thales Use Cases (Legacy)** | `libse_Legacy_Use_Cases.bin` | `secure_element/libse-thales_tests/` |

### Build Tests

#### SecureElement Unit Tests
```bash
cd secure_element/tests/unitary_tests/build
cmake ..
make
```

#### libse-thales Tests
```bash
cd secure_element/libse-thales_tests/build
cmake ..
make
```

### Run Tests

#### Run all tests : HAL + library
```bash
ctest

ctest -V #for verbose mode
```

#### SecureElement Unit Tests
```bash
cd secure_element/tests/unitary_tests/build

# Normal output
./SecureElement_GP_API_Test.bin
./SecureElement_Legacy_API_Test.bin

# Verbose output
./SecureElement_GP_API_Test.bin -v
./SecureElement_Legacy_API_Test.bin -v
```

#### libse-thales Stress Tests
```bash
cd secure_element/libse-thales_tests/build/bin

# Normal output
./libse_gp_stress_api.bin
./libse_legacy_stress_api.bin

# Verbose output
./libse_gp_stress_api.bin -v
./libse_legacy_stress_api.bin -v
```

#### libse-thales Use Cases
```bash
cd secure_element/libse-thales_tests/build/bin
./libse_GP_Use_Cases.bin
./libse_Legacy_Use_Cases.bin
```

### Clean Tests

#### SecureElement Unit Tests
```bash
cd secure_element/tests/unitary_tests/build
make clean_hal_tests
```

#### libse-thales Tests
```bash
cd secure_element/libse-thales_tests/build
make clean_tests
```

### Clean all

#### Clean tests + HAL + library
```bash
make clean_all
```