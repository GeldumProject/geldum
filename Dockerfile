FROM ubuntu:16.04

RUN apt update && apt install -y \
    build-essential cmake libboost-all-dev \
    miniupnpc libunbound-dev graphviz doxygen \
    libunwind8-dev pkg-config libssl-dev git \
    libminiupnpc-dev liblzma-dev libldns-dev \
    libexpat1-dev doxygen graphviz libgtest-dev

ADD . /usr/src/geldum

WORKDIR /usr/src/geldum

RUN make -j 8 release-static && \
    mkdir /geldum && \
    cp -fv build/release/bin/* /geldum

WORKDIR /geldum

VOLUME [ "/geldum" ]

RUN apt purge -y build-essential cmake libboost-all-dev \
    miniupnpc libunbound-dev graphviz doxygen \
    libunwind8-dev pkg-config libssl-dev git \
    libminiupnpc-dev liblzma-dev libldns-dev \
    libexpat1-dev doxygen graphviz libgtest-dev && \
    apt clean

ENTRYPOINT [ "/geldum/geldumd" ]
