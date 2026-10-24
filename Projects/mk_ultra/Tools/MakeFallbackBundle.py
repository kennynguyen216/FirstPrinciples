"""Saves today's real space weather as the offline fallback bundle in Content/SpaceWeather/Fallback.

The game replays this bundle (labelled with its real date) whenever a live request fails, so it
must only ever hold real responses. Files keep the live formats so one parser reads both; the
solar wind file is only trimmed to the fields the game reads.

Run from anywhere: python3 Projects/mk_ultra/Tools/MakeFallbackBundle.py [NASA_API_KEY]
"""
import datetime
import json
import os
import sys
import urllib.request

KEY = sys.argv[1] if len(sys.argv) > 1 else 'DEMO_KEY'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Content', 'SpaceWeather', 'Fallback')


def get(url, attempts=4):
    for attempt in range(attempts):
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                return response.read().decode('utf-8')
        except OSError:
            if attempt == attempts - 1:
                raise


def main():
    os.makedirs(OUT, exist_ok=True)
    today = datetime.datetime.now(datetime.timezone.utc).date()
    yesterday = today - datetime.timedelta(days=1)

    wind = json.loads(get('https://services.swpc.noaa.gov/json/rtsw/rtsw_wind_1m.json'))
    wind = [{'time_tag': w['time_tag'], 'active': True, 'proton_speed': w['proton_speed']}
            for w in wind if w.get('active') and w.get('proton_speed') is not None]
    files = {
        'SolarWind.json': json.dumps(wind),
        'Kp.json': get('https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json'),
        'Flybys.json': get(f'https://api.nasa.gov/neo/rest/v1/feed?start_date={yesterday}&end_date={today}&api_key={KEY}'),
    }
    for name, text in files.items():
        with open(os.path.join(OUT, name), 'w') as f:
            f.write(text)
    print(f'Saved {len(wind)} solar wind readings and {len(files)} files to {os.path.normpath(OUT)}')


if __name__ == '__main__':
    main()
