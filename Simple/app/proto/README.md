# Sparkplug B protobuf

`sparkplug_b.proto` er en delmængde af Sparkplug B v1.0 (samme feltnumre og datatyper).
`sparkplug_b.pb.c`/`.h` er genereret med nanopb og er committet, så buildet ikke
kræver `protoc`. Regenerér efter ændringer i `.proto` eller `.options`:

```bash
python -m venv /tmp/spvenv && /tmp/spvenv/bin/pip install grpcio-tools protobuf
cd app/proto
/tmp/spvenv/bin/python ~/ncs/v3.4.0/modules/lib/nanopb/generator/nanopb_generator.py sparkplug_b.proto
```
