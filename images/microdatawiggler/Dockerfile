FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

COPY build/Debug/*.deb /tmp/microdatawiggler.deb
RUN apt-get update \
    && apt-get install -y --no-install-recommends python3 python3-pip /tmp/microdatawiggler.deb \
    && rm -rf /var/lib/apt/lists/* /tmp/microdatawiggler.deb \
    && apt-get clean

COPY python/microdatawiggler/dist/*.whl /tmp/
RUN pip install --break-system-package pyelftools \
    && pip install --break-system-packages /tmp/*.whl \
    && rm /tmp/*.whl

ENTRYPOINT ["microdatawiggler"]
