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
    git

RUN ln -s /usr/bin/python3 /usr/bin/python

# Layer and below all Claude
WORKDIR /xv6

RUN useradd -ms /bin/bash student
RUN chown -R student:student /xv6

RUN mkdir -p /home/student/.config/gdb
RUN echo "set auto-load safe-path /xv6" > /home/student/.config/gdb/gdbinit

# Claude
USER student
RUN echo 'alias gdb-multiarch="gdb-multiarch -iex \"add-auto-load-safe-path /xv6\""' >> ~/.bashrc

CMD ["/bin/bash"]
