# CubeOS
A generic asset processor for N64 games


# Dependencies for WSL Ubuntu
```
sudo apt-get install cmake ninja-build libbz2-dev
```

# Compile

```
cmake -H. -Bbuild-cmake -GNinja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cmake -j
```
