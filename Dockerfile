# Dockerfile
# =========================================================================
# Dockerfile Unificado para Aplicação NDN (Orquestrador/Semáforo)
# Instala NFD via PPA, compila a aplicação e prepara o ambiente.
# =========================================================================

# --- Estágio 1: Build da Aplicação ---
FROM ubuntu:22.04 AS builder

# 1. Instala dependências de compilação e o PPA do NDN
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    libboost-all-dev \
    libssl-dev \
    libsqlite3-dev \
    libpcap-dev \
    libsystemd-dev \
    software-properties-common \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*


RUN git clone --branch ndn-cxx-0.9.0 https://github.com/named-data/ndn-cxx.git && \
    cd ndn-cxx && \
    ./waf configure && \
    ./waf && \
    ./waf install && \
    ldconfig

RUN git clone --branch NFD-24.07 https://github.com/named-data/NFD.git && \
    cd NFD && \
    git submodule update --init && \
    ./waf configure && \
    ./waf && \
    ./waf install && \
    ldconfig

WORKDIR /app
COPY . .

RUN mkdir build && \
    cd build && \
    cmake .. && \
    make

FROM ubuntu:22.04

RUN apt-get update && apt-get install -y software-properties-common
RUN apt-get update && apt-get install -y \
      cmake \
    git \
    libssl-dev \
    libsqlite3-dev \
    libpcap-dev \
    libboost-all-dev \
    libboost-system1.74.0 \
    sudo \
    python3 \
    python3-pip \
    wget \
    jq \
    && rm -rf /var/lib/apt/lists/*

RUN wget https://github.com/mikefarah/yq/releases/latest/download/yq_linux_amd64 -O /usr/bin/yq && \
    chmod +x /usr/bin/yq

COPY --from=builder /usr/local/ /usr/local/

RUN ldconfig

RUN mkdir -p /usr/local/etc/ndn/
COPY --from=builder /usr/local/etc/ndn/nfd.conf.sample /usr/local/etc/ndn/nfd.conf

RUN pip3 install pyyaml

WORKDIR /app
COPY --from=builder /app/build/orchestrator /usr/local/bin/
COPY --from=builder /app/build/trafficLight /usr/local/bin/
COPY ./entrypoint.sh /usr/local/bin/
RUN chmod +x /usr/local/bin/entrypoint.sh
RUN mkdir /app/metrics

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]