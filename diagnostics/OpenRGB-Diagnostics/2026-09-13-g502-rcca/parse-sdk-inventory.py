import base64
import json
from pathlib import Path
import struct
import sys


class Description:
    def __init__(self, data):
        self.data = data
        self.offset = 0

    def number(self, fmt):
        value = struct.unpack_from('<' + fmt, self.data, self.offset)[0]
        self.offset += struct.calcsize('<' + fmt)
        return value

    def string(self):
        size = self.number('H')
        value = self.data[self.offset:self.offset + size]
        self.offset += size
        assert len(value) == size and value.endswith(b'\0')
        return value[:-1].decode('utf-8')

    def skip(self, size):
        self.offset += size
        assert self.offset <= len(self.data)


source = Path(sys.argv[1]) if len(sys.argv) > 1 else max(
    (p for p in Path(__file__).parent.glob('sdk-inventory-*.json')
     if not p.name.endswith('-parsed.json')), key=lambda p: p.stat().st_mtime)
receipt = json.loads(source.read_text(encoding='utf-8-sig'))
assert receipt['Protocol'] == 0
controllers = []
for item in receipt['Controllers']:
    d = Description(base64.b64decode(item['Data']))
    assert d.number('I') == len(d.data)
    d.number('I')
    name = d.string()
    for _ in range(4):
        d.string()
    mode_count, active_mode = d.number('H'), d.number('I')
    modes = []
    for _ in range(mode_count):
        modes.append(d.string())
        d.skip(9 * 4)
        d.skip(d.number('H') * 4)
    zones = []
    for _ in range(d.number('H')):
        zone_name = d.string()
        values = [d.number('I') for _ in range(4)]
        zones.append({'Name': zone_name, 'LEDs': values[3]})
        d.skip(d.number('H'))
    led_count = d.number('H')
    for _ in range(led_count):
        d.string()
        d.number('I')
    colors = []
    for _ in range(d.number('H')):
        value = d.number('I')
        colors.append(f'{value & 255:02X}{(value >> 8) & 255:02X}{(value >> 16) & 255:02X}')
    assert d.offset == len(d.data), (name, d.offset, len(d.data))
    controllers.append({'Index': item['Index'], 'Name': name,
                        'Mode': modes[active_mode], 'Zones': zones,
                        'LEDs': led_count, 'Colors': colors})
result = {key: receipt[key] for key in ('Time', 'ServerPid')}
result['Controllers'] = controllers
target = source.with_name(source.stem + '-parsed.json')
target.write_text(json.dumps(result, indent=2) + '\n')
print(target.name)
for controller in controllers:
    print(controller['Index'], controller['Name'], controller['Mode'],
          'zones=', len(controller['Zones']), 'leds=', controller['LEDs'],
          'colors=', sorted(set(controller['Colors'])))
