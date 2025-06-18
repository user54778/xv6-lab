FROM ubuntu:20.04

# Claude
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    gdb-multiarch \
    qemu-system-riscv64 \
    gcc-9-riscv64-linux-gnu \
    gcc-riscv64-linux-gnu \
    binutils-riscv64-linux-gnu \
    tmux \
    qemu \
    python3 \
    python3-pip \
    curl \
    tcpdump \
    netcat \
    git

RUN ln -s /usr/bin/python3 /usr/bin/python

# Layer and below all Claude
WORKDIR /xv6

RUN useradd -ms /bin/bash student
USER student

CMD ["/bin/bash"]
