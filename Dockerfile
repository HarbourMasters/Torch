FROM ubuntu:22.04 as build

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y \
        cmake \
        ninja-build \
        libbz2-dev

RUN mkdir /torch
WORKDIR /torch

CMD echo 'Usage: docker run -it --rm -v .:/torch torch cmake -H. -Bbuild-cmake -GNinja -DCMAKE_BUILD_TYPE=Debug && cmake --build build-cmake -j\n'\
         'See https://github.com/HarbourMasters/Torch/blob/master/README.md for more information'