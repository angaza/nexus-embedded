FROM ubuntu@sha256:376209074d481dca0a9cf4282710cd30a9e7ff402dea8261acdaaf57a18971dd
# ubuntu 20.04 latest as of July 2021
LABEL maintainer="iot@angaza.com"

# 2020-06-16 -> 4.8.3
ARG CONDA_VERSION="py38_4.8.3"  
ARG CONDA_DIR="/opt/conda"
ARG CONDA_MD5="d63adf39f2c220950a063e0529d4ff74"

# Resolves a Ruby error while running ceedling
# "ArgumentError: invalid byte sequence in US-ASCII"
ENV PATH="$CONDA_DIR/bin:$PATH" \
    LANG="C.UTF-8" \
    LC_ALL="C.UTF-8"

# References:
# https://hub.docker.com/r/frolvlad/alpine-miniconda3/dockerfile
# https://hub.docker.com/r/continuumio/miniconda3/dockerfile
# Install conda and GCC-10 (there isn't a Conda GCC 10 out, yet)
# XXX conda version of gcc-arm-none-eabi doesnt find -lgcc or -lc_nano
RUN echo "**** install dev packages ****" && \
    apt-get update --fix-missing && \
    apt-get install -y wget \
    bzip2 \
    build-essential \
    ca-certificates \
    curl \
    gcc-10 \
    g++-10 \
    gcc-multilib \
    gcc-arm-none-eabi \
    git \
    unzip && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# set default GCC (/usr/bin/gcc) to point to GCC 10
# Priority 100 is arbitrary, there are no other alternatives set up
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 100

RUN echo "**** get and install Miniconda ****" && \
    wget "http://repo.continuum.io/miniconda/Miniconda3-${CONDA_VERSION}-Linux-x86_64.sh" -O ~/miniconda.sh && \
    echo "$CONDA_MD5  /root/miniconda.sh" | md5sum -c && \
    /bin/bash ~/miniconda.sh -b -p "$CONDA_DIR" && \
    rm ~/miniconda.sh && \
    /opt/conda/bin/conda clean -tipsy && \
    ln -s /opt/conda/etc/profile.d/conda.sh /etc/profile.d/conda.sh && \
    echo ". /opt/conda/etc/profile.d/conda.sh" >> ~/.bashrc && \
    /opt/conda/bin/conda update --all --yes && \
    /opt/conda/bin/conda config --set auto_update_conda False

# Relies on Dockerfile being built fron a context of repository root
COPY support/condaenv.yml /root/condaenv.yml

# Necessary for Conda to properly hook into bash
RUN conda init bash

# NOTE: This directory access assumes some file setup has already occurred...
# Resolves a Ruby error while running ceedling
# "ArgumentError: invalid byte sequence in US-ASCII"
RUN echo "**** activate custom environment ****" && \
    /opt/conda/bin/conda env create -f /root/condaenv.yml && \
    echo "conda activate nexusemb" >> /root/.bashrc && \
    echo "export LANG=C.UTF-8" >> /root/.bashrc && \
    echo "export LC_ALL=C.UTF-8" >> /root/.bashrc
    
RUN echo "**** cleanup ****" && \
    find "$CONDA_DIR" -follow -type f \( -iname '*.a' -o -iname '*.pyc' -o -iname '*.js.map' \) -delete

# Create an artifacts directory
RUN mkdir /buildkite_artifacts

# Activate environment created from /base/support/condaenv.yml
WORKDIR /base
ENTRYPOINT ["bash"]
