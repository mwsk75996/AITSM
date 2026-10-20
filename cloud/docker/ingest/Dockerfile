# Runtime for cloud/mqtt/ingest.py. Koden bind-mountes fra værten, så
# deploy-helperen kan udskifte ingest.py atomisk uden et nyt image.
FROM python:3.12.15-slim@sha256:a6e34c598f2467ed0e9a8d349809fcd8b5c603269512df273a0bb1784edc11b1

# Samme version som VPS'ens tidligere systemd-service; >=2.0 kræves for manual_ack (#88).
RUN pip install --no-cache-dir paho-mqtt==2.1.0

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1
WORKDIR /app
CMD ["python", "/app/ingest.py"]
