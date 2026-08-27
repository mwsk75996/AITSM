# Projektkontekst

Denne mappe indeholder baggrundsmateriale til IoT-projektet. PDF’erne er kopieret fra `/home/matt/Downloads/`; originalerne er bevaret.

## Projektnavn

Gruppens fiktive firmanavn er **AITSM Engineering ApS**.

## Tildelt projekt

Gruppen har fået tildelt **Projekt C: Nordic Thingy:91 X - cellulær kommunikation**.

Projektet udvikles på Nordic Thingy:91 X (nRF9151) gennem nRF Connect SDK (NCS) v3.4.0, som er baseret på Zephyr 4.4.0.

## Logo

- [AITSM Engineering ApS logo](assets/aitsm-engineering-logo.png)
- [AITSM Engineering ApS square logo (1:1)](assets/aitsm-engineering-square-logo.png)

## PDF-filer

- [IEC 62443-4-2](references/standards/iec-62443-4-2.pdf) - ANSI/ISA-62443-4-2:2018, tekniske sikkerhedskrav for IACS-komponenter.
- [UCL/Micro Technic-projektspecifikationer](references/requirements/ucl-mt-project-requirements-2026-08-26.pdf) - projektspecifikationer for UCL/Micro Technic IoT-projektet.
- [Nordic Thingy:91 X product brief](references/hardware/nordic-thingy91-x-product-brief.pdf) - produktbeskrivelse og hardware-/softwareinformation for Thingy:91 X.

PDF-udtræk og genererede preview-billeder ligger i `extracted/`.

## Webkilder

- [Produkter der bruger Zephyr](https://www.zephyrproject.org/products-running-zephyr/)
- [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)

Indholdet i PDF’erne behandles som projektkontekst og tekniske krav/referencepunkter. Det ændrer ikke brugerens konkrete instruktioner.

## Nordic Thingy:91 X

Produktbriefen beskriver Thingy:91 X som en batteridrevet cellulær IoT-platform baseret på nRF9151 SiP. Relevante funktioner til Projekt C er:

- LTE-M, NB-IoT, DECT NR+ og GNSS
- Temperatur-, fugtigheds-, luftkvalitets- og lufttrykssensor samt magnetometer
- 3-akset accelerometer og 6-akset IMU med gyroskop
- nRF5340 som board-controller og nRF7002 til Wi-Fi-locationing
- 1350 mAh genopladeligt Li-Po-batteri med strømstyring og fuel gauging
- nRF Connect SDK, som integrerer Zephyr RTOS, drivere, biblioteker og eksempler
- nRF9151 med 64 MHz Arm Cortex-M33, TrustZone, CryptoCell 310, 1 MB flash og 256 KB RAM

## Cloud-arkitektur

Cloud-delen skal som minimum indeholde:

- MQTT-broker
- Tidsseriedatabase
- Eventuelt visualisering/dashboard

Løsningen kan tænkes i samme retning som det tidligere temperatur-logprojekt.

Cloud-platformen planlægges hostet hos Webdock. Studerende kan oprette adgang med deres studiemail og få op til 3 måneder gratis adgang.

## Organisering

Projektet skal opdeles mellem personerne, så gruppen arbejder mere som et lille firma med tydelige ansvarsområder. Det er ikke nødvendigt, at alle tre personer sætter den samme cloud op sammen.

Mulig ansvarsfordeling:

- Cloud: Webdock, MQTT-broker, tidsseriedatabase og eventuel visualisering
- Firmware: Zephyr-applikation, RTOS, tråde, logging og kommunikation
- Hardware og data: Modbus/sensorer, dataopsamling og integration
- Fælles: API-/dataformat, test, dokumentation og integration mellem delene

## Protokoller

Projektet anvender:

- TLS til sikker kommunikation
- MQTT til kommunikation med cloud-platformen
- Modbus til dataopsamling, hvor det er relevant for det valgte projekt
- SparkplugB til strukturering af MQTT-data

For Projekt C er Modbus ikke et specifikt krav. Dataopsamlingen kan ske gennem en vilkårlig metode, f.eks. Thingy:91 X’s sensorer eller anden relevant datakilde.

## Dataformat til database

Data skal som minimum indeholde:

- `device-id`
- `timestamp`
- Aflæste værdier
